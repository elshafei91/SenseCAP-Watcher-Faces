#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <regex.h>
#include <mbedtls/base64.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "time.h"
#include "app_time.h"
#include "app_wifi.h"
#include "event_loops.h"
#include "data_defs.h"
#include "portmacro.h"
#include "uhash.h"
#include "at_cmd.h"
#include "cJSON.h"
#include "app_device_info.h"

#define TAG "AT_CMD"
/*------------------system basic DS-----------------------------------------------------*/
StreamBufferHandle_t xStreamBuffer;

SemaphoreHandle_t AT_response_semaphore;
QueueHandle_t AT_response_queue;
TaskHandle_t xTaskToNotify_AT = NULL;

const char *pattern = "^AT\\+([a-zA-Z0-9]+)(\\?|=(\\{.*\\}))?\r\n$";
command_entry *commands = NULL; // Global variable to store the commands
static StaticTask_t at_task_buffer;
static StackType_t *at_task_stack = NULL;
static void hex_to_string(uint8_t *hex, int hex_size, char *output);
static void __view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);
/*------------------network DS----------------------------------------------------------*/
SemaphoreHandle_t wifi_stack_semaphore;
static int network_connect_flag;
static wifi_ap_record_t current_connected_wifi;
static int task_flow_resp;
/*------------------critical DS for task_flow-------------------------------------------*/

typedef struct
{
    int package;
    int sum;
    char *data;
} Task;

Task *tasks = NULL;
static int num_jsons = 0;

/*------------------------------------emoji DS---------------------------------------------*/
Task *emoji_tasks = NULL;

/*-----------------------------------bind index--------------------------------------------*/
static int bind_index;

/*----------------------------------------------------------------------------------------*/

/**
 * @brief Initialize the Wi-Fi stack semaphore.
 *
 * This function creates a mutex semaphore for the Wi-Fi stack.
 * A semaphore is a synchronization primitive used to control access
 * to a shared resource in a concurrent system such as a multitasking
 * operating system. In this case, the semaphore is used to manage
 * access to the Wi-Fi stack to ensure thread safety.
 */
void wifi_stack_semaphore_init()
{
    wifi_stack_semaphore = xSemaphoreCreateMutex();
}

/**
 * @brief Initialize the Wi-Fi stack with a specified capacity.
 *
 * This function initializes a Wi-Fi stack by allocating memory for the stack entries
 * and setting its initial size and capacity. The memory is allocated from a specific
 * heap region suitable for large allocations.
 *
 * @param stack A pointer to the Wi-Fi stack structure to be initialized.
 * @param capacity The maximum number of entries the Wi-Fi stack can hold.
 */
void initWiFiStack(WiFiStack *stack, int capacity)
{
    stack->entries = (WiFiEntry *)heap_caps_malloc(capacity * sizeof(WiFiEntry), MALLOC_CAP_SPIRAM);
    stack->size = 0;
    stack->capacity = capacity;
}

/**
 * @brief Push a new Wi-Fi entry onto the Wi-Fi stack.
 *
 * This function adds a new Wi-Fi entry to the stack. It ensures thread safety
 * by using a semaphore to protect the critical section where the stack is modified.
 * If the stack's capacity is exceeded, the stack's capacity is doubled and the memory
 * for the entries is reallocated.
 *
 * @param stack A pointer to the Wi-Fi stack where the entry will be added.
 * @param entry The Wi-Fi entry to be added to the stack.
 */
void pushWiFiStack(WiFiStack *stack, WiFiEntry entry)
{
    // Acquire the semaphore to protect the critical section
    xSemaphoreTake(wifi_stack_semaphore, portMAX_DELAY);

    if (stack->size >= stack->capacity)
    {
        stack->capacity *= 2;
        stack->entries = (WiFiEntry *)realloc(stack->entries, stack->capacity * sizeof(WiFiEntry));
    }
    stack->entries[stack->size++] = entry;

    // Release the semaphore to allow other tasks to access the critical section
    xSemaphoreGive(wifi_stack_semaphore);
}

/**
 * @brief Free the memory allocated for the Wi-Fi stack.
 *
 * This function releases the memory allocated for the Wi-Fi stack entries and
 * resets the stack's attributes to indicate that it is empty and uninitialized.
 *
 * @param stack A pointer to the Wi-Fi stack to be freed.
 */
void freeWiFiStack(WiFiStack *stack)
{
    free(stack->entries);
    stack->entries = NULL;
    stack->size = 0;
    stack->capacity = 0;
}

/**
 * @brief Create a JSON object representing a Wi-Fi entry.
 *
 * This function creates a JSON object from a given Wi-Fi entry. The JSON object
 * contains the SSID, RSSI, and encryption type of the Wi-Fi entry.
 *
 * @param entry A pointer to the Wi-Fi entry to be converted to JSON.
 * @return A cJSON object representing the Wi-Fi entry.
 */
cJSON *create_wifi_entry_json(WiFiEntry *entry)
{
    cJSON *wifi_json = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi_json, "ssid", entry->ssid);
    cJSON_AddStringToObject(wifi_json, "rssi", entry->rssi);
    cJSON_AddStringToObject(wifi_json, "encryption", entry->encryption);
    return wifi_json;
}

/**
 * @brief Create a JSON object representing the scanned and connected Wi-Fi stacks.
 *
 * This function creates a JSON object that contains two arrays: one for the
 * scanned Wi-Fi entries and another for the connected Wi-Fi entries.
 *
 * @param stack_scnned_wifi A pointer to the Wi-Fi stack containing scanned Wi-Fi entries.
 * @param stack_connected_wifi A pointer to the Wi-Fi stack containing connected Wi-Fi entries.
 * @return A cJSON object representing both the scanned and connected Wi-Fi stacks.
 */
cJSON *create_wifi_stack_json(WiFiStack *stack_scnned_wifi, WiFiStack *stack_connected_wifi)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *scanned_array = cJSON_CreateArray();
    cJSON *connected_array = cJSON_CreateArray();
    for (int i = 0; i < stack_connected_wifi->size; i++)
    {
        cJSON_AddItemToArray(connected_array, create_wifi_entry_json(&stack_connected_wifi->entries[i]));
    }

    for (int i = 0; i < stack_scnned_wifi->size; i++)
    {
        cJSON_AddItemToArray(scanned_array, create_wifi_entry_json(&stack_scnned_wifi->entries[i]));
    }
    cJSON_AddItemToObject(root, "connected_wifi", connected_array);
    cJSON_AddItemToObject(root, "scanned_wifi", scanned_array);
    return root;
}

// AT command system
/*----------------------------------------------------------------------------------------------------*/

void create_AT_response_queue();
void init_AT_response_semaphore();
void send_at_response(AT_Response *AT_Response);
AT_Response create_at_response(const char *message);

/**
 * @brief Add a command to the hash table of commands.
 *
 * This function creates a new command entry and adds it to the hash table of commands.
 *
 * @param commands A pointer to the hash table of commands.
 * @param name The name of the command.
 * @param func A pointer to the function that implements the command.
 */
void add_command(command_entry **commands, const char *name, void (*func)(char *params))
{
    // command_entry *entry = (command_entry *)malloc(sizeof(command_entry)); // Allocate memory for the new entry
    command_entry *entry = (command_entry *)heap_caps_malloc(sizeof(command_entry), MALLOC_CAP_SPIRAM);
    strcpy(entry->command_name, name);            // Copy the command name to the new entry
    entry->func = func;                           // Assign the function pointer to the new entry
    HASH_ADD_STR(*commands, command_name, entry); // Add the new entry to the hash table
}

/**
 * @brief Execute a command from the hash table.
 *
 * This function searches for a command by name in the hash table and executes it
 * with the provided parameters. If the query character is '?', the command is treated
 * as a query command.
 *
 * @param commands A pointer to the hash table of commands.
 * @param name The name of the command to execute.
 * @param params The parameters to pass to the command function.
 * @param query The query character that modifies the command behavior.
 */
void exec_command(command_entry **commands, const char *name, char *params, char query)
{
    command_entry *entry;
    char full_command[128];
    snprintf(full_command, sizeof(full_command), "%s%c", name, query); // Append the query character to the command name
    HASH_FIND_STR(*commands, full_command, entry);
    if (entry)
    {
        if (query == '?') // If the query character is '?', then the command is a query command
        {
            entry->func(NULL);
        }
        else
        {
            entry->func(params);
        }
    }
    else
    {
        ESP_LOGI(TAG, "Command not found\n");
    }
}

/**
 * @brief Register the AT commands.
 *
 * This function adds various AT commands to the hash table of commands.
 */
void AT_command_reg()
{
    // Register the AT commands
    add_command(&commands, "deviceinfo?", handle_deviceinfo_command);
    add_command(&commands, "wifi=", handle_wifi_set);
    add_command(&commands, "wifi?", handle_wifi_query);
    add_command(&commands, "wifitable?", handle_wifi_table);
    add_command(&commands, "devicecfg=", handle_deviceinfo_cfg_command);
    add_command(&commands, "taskflow?", handle_taskflow_query_command);
    add_command(&commands, "taskflow=", handle_taskflow_command);
    add_command(&commands, "cloudservice=", handle_cloud_service_command);
    add_command(&commands, "cloudservice?", handle_cloud_service_qurey_command);
    add_command(&commands, "emoji=", handle_emoji_command);
    add_command(&commands, "bind=", handle_bind_command);
}

/**
 * @brief Handle the bind command by parsing input parameters, extracting the bind index,
 *        posting an event to the app event loop, and creating a JSON response.
 *
 * This function takes a JSON string as input, parses it to extract a "code" value, and posts
 * an event with this value to the application event loop. It then creates a JSON response
 * indicating the success of the bind command and sends this response.
 *
 * @param params JSON string containing the "code" key with an integer value to bind.
 */
void handle_bind_command(char *params)
{
    ESP_LOGI(TAG, "handle_bind_command\n");
    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "code");
    bind_index = data->valueint;
    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONFIG_SYNC, &bind_index, sizeof(bind_index), portMAX_DELAY);
    ESP_LOGI(TAG, "bind_index: %d\n", bind_index);

    cJSON_Delete(json);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    cJSON *data_rep = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "bind");
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data_rep);
    cJSON_AddStringToObject(data_rep, "bind", "");
    char *json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON String: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/*-----------------------------------------------------------------------------------------------------------*/
static int emoji_index = 1;
static char *emoji_name_prefix;
static char *emoji_name_final;
void parse_json_and_concatenate_emoji(char *json_string)
{
    cJSON *json = cJSON_Parse(json_string);
    if (json == NULL)
    {
        ESP_LOGI(TAG, "Error parsing emoji JSON\n");
    }

    cJSON *name = cJSON_GetObjectItem(json, "name");
    cJSON *package = cJSON_GetObjectItem(json, "package");
    cJSON *sum = cJSON_GetObjectItem(json, "sum");
    cJSON *data = cJSON_GetObjectItem(json, "data");
    cJSON *total_size = cJSON_GetObjectItem(json, "totalsize");
    cJSON *emoji_index_cjson = cJSON_GetObjectItem(json, "emoji_index");
    if (!cJSON_IsString(name) || !cJSON_IsNumber(package) || !cJSON_IsNumber(sum) || !cJSON_IsString(data) || !cJSON_IsNumber(total_size))
    {
        ESP_LOGI(TAG, "Invalid JSON format in parse_json_and_concatenate_emoji\n");
        cJSON_Delete(json);
    }

    emoji_index = emoji_index_cjson->valueint;
    int prefix_size = 256;
    if (name && name->valuestring)
    {
        emoji_name_prefix[prefix_size - 1] = '\0';
    }
    else
    {
        fprintf(stderr, "name or name->valuestring is null\n");
        emoji_name_prefix[0] = '\0';
    }
    int emoji_name_length = snprintf(NULL, 0, "emoji name is %s%d", emoji_name_prefix, emoji_index) + 1;
    char *emoji_name_final = (char *)heap_caps_malloc(emoji_name_length, MALLOC_CAP_SPIRAM);
    snprintf(emoji_name_final, emoji_name_length, "emoji name is %s%d", emoji_name_prefix, emoji_index);

    total_size = total_size->valueint;
    int index = package->valueint;

    if (num_jsons == 0)
    {
        num_jsons = sum->valueint;
        emoji_tasks = (Task *)heap_caps_malloc(num_jsons * sizeof(Task), MALLOC_CAP_SPIRAM);
        if (emoji_tasks == NULL)
        {
            ESP_LOGI(TAG, "Failed to allocate memory for tasks\n");
            cJSON_Delete(json);
        }
        for (int i = 0; i < num_jsons; i++)
        {
            emoji_tasks[i].data = NULL;
        }
    }

    if (emoji_tasks == NULL || index < 0 || index >= num_jsons)
    {
        ESP_LOGI(TAG, "emoji Tasks array is not properly allocated or index out of range\n");
        cJSON_Delete(json);
    }

    emoji_tasks[index].package = package->valueint;
    emoji_tasks[index].sum = sum->valueint;
    emoji_tasks[index].data = (char *)heap_caps_malloc(DATA_LENGTH + 1, MALLOC_CAP_SPIRAM);
    if (emoji_tasks[index].data == NULL)
    {
        ESP_LOGI(TAG, "Failed to allocate memory for data\n");
        cJSON_Delete(json);
    }
    strncpy(emoji_tasks[index].data, data->valuestring, DATA_LENGTH);
    emoji_tasks[index].data[DATA_LENGTH] = '\0'; // end

    cJSON_Delete(json);
}
void concatenate_data_emoji(char *result)
{
    // sort
    for (int i = 0; i < num_jsons - 1; i++)
    {
        for (int j = 0; j < num_jsons - 1 - i; j++)
        {
            if (emoji_tasks[j].package > emoji_tasks[j + 1].package)
            {
                Task temp = emoji_tasks[j];
                emoji_tasks[j] = emoji_tasks[j + 1];
                emoji_tasks[j + 1] = temp;
            }
        }
    }

    // concatenate_data
    result[0] = '\0';
    for (int i = 0; i < num_jsons; i++)
    {
        strcat(result, emoji_tasks[i].data);
        free(emoji_tasks[i].data);
    }
    num_jsons = 0;
    free(emoji_tasks);
    emoji_tasks = NULL;
}

void write_to_file(const char *file_path, const char *data)
{
    FILE *file = fopen(file_path, "w");
    if (file == NULL)
    {
        ESP_LOGE("write to file", "Failed to open file for writing: %s", file_path);
        return;
    }

    if (fputs(data, file) == EOF)
    {
        ESP_LOGE("write to file", "Failed to write data to file: %s", file_path);
        fclose(file);
        return;
    }
    fclose(file);
    ESP_LOGI("write to file", "Successfully wrote data to file: %s", file_path);
}

void handle_emoji_command(char *params)
{
    ESP_LOGI(TAG, "handle_emoji_command\n");
    ESP_LOGI(TAG, "emoji Params: %s\n", params);
    parse_json_and_concatenate_emoji(params);

    int all_received = 1;
    for (int j = 0; j < num_jsons; j++)
    {
        if (emoji_tasks[j].data == NULL)
        {
            all_received = 0;
            break;
        }
    }
    if (all_received)
    {
        char *result = (char *)heap_caps_malloc(MEMORY_SIZE, MALLOC_CAP_SPIRAM);
        if (result == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for result in emoji handle\n");
            for (int k = 0; k < num_jsons; k++)
            {
                free(emoji_tasks[k].data);
            }
            free(emoji_tasks);
        }

        concatenate_data_emoji(result);
        char file_path[256];
        snprintf(file_path, sizeof(file_path), "/spiffs/%s", emoji_name_final);
        ESP_LOGI(TAG, "emoji Final data: %s\n", result);
        // todo write to spiffs
        write_to_file(file_path, result);
    }

    free(emoji_name_final);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    cJSON *data_rep = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "emoji");
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data_rep);
    cJSON_AddStringToObject(data_rep, "emoji", "");
    char *json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON String: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/*-----------------------------------------------------------------------------------------------------------*/

/**
 * @brief Handle the cloud service query command by retrieving the cloud service switch state
 *        and creating a JSON response.
 *
 * This function retrieves the state of the cloud service switch and creates a JSON response
 * with the state information. The response is then sent to the requester.
 *
 * @param params Unused parameter in this function but kept for consistency in the command handler signature.
 */
void handle_cloud_service_qurey_command(char *params)
{
    int cloud_service_switch;
    cloud_service_switch = get_cloud_service_switch(AT_CMD_CALLER);
    ESP_LOGI(TAG, "Handling handle_cloud_service_qurey_command \n");
    vTaskDelay(10 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    cJSON *data_rep = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "cloudservice");
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data_rep);
    cJSON_AddNumberToObject(data_rep, "remotecontrol", cloud_service_switch);
    char *json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON String in cloud service qurey handle: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/**
 * @brief Handle the cloud service command by parsing input parameters, updating the cloud service switch state,
 *        and creating a JSON response.
 *
 * This function takes a JSON string as input, parses it to extract the "remotecontrol" value, and updates
 * the cloud service switch state. It then creates a JSON response indicating the success of the operation
 * and sends this response.
 *
 * @param params JSON string containing the "data" key with a "remotecontrol" integer value.
 */
void handle_cloud_service_command(char *params)
{
    int cloud_service_switch;
    ESP_LOGI(TAG, "handle_cloud_service_command\n");
    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    if (cJSON_IsObject(data))
    {
        cJSON *cloud_service = cJSON_GetObjectItemCaseSensitive(data, "remotecontrol");
        if (cJSON_IsNumber(cloud_service))
        {
            cloud_service_switch = cloud_service->valueint;
            ESP_LOGI(TAG, "Cloud_Service: %d\n", cloud_service_switch);
            set_cloud_service_switch(AT_CMD_CALLER, cloud_service_switch);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Cloud_Service not found or not a valid string in JSON\n");
    }

    cJSON_Delete(json);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    cJSON *data_rep = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "cloudservice");
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data_rep);
    cJSON_AddStringToObject(data_rep, "remotecontrol", "");
    char *json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON String: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/**
 * @brief Handle the device configuration command.
 *
 * This function processes the device configuration command by parsing the JSON string
 * provided in the parameters, extracting the time zone information, and posting an event
 * with the time zone configuration. It also creates a JSON response with default values
 * for other settings and sends it back.
 *
 * @param params A JSON string containing the device configuration data.
 */
void handle_deviceinfo_cfg_command(char *params)
{
    ESP_LOGI(TAG, "handle_deviceinfo_cfg_command\n");
    int time_flag = 0;
    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    if (cJSON_IsObject(data))
    {
        // Get the "Time_Zone" item
        cJSON *time_zone = cJSON_GetObjectItemCaseSensitive(data, "timezone");
        if (cJSON_IsNumber(time_zone))
        {
            int timezone = time_zone->valueint;
            struct view_data_time_cfg time_cfg;
            time_cfg.zone = timezone;
            time_cfg.auto_update = true;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_TIME_ZONE, &time_cfg, sizeof(time_cfg), portMAX_DELAY);
        }
        cJSON *time = cJSON_GetObjectItemCaseSensitive(data, "timestamp");
        if (cJSON_IsString(time))
        {
            long long int value;
            char *time_str = time->valuestring;
            char *endptr;
            value = strtoll(time_str, &endptr, 10);
            if (endptr == time_str)
            {
                ESP_LOGE(TAG, "No digits were found\n");
            }
            else if (*endptr != '\0')
            {
                ESP_LOGE(TAG, "Further characters after number: %s\n", endptr);
            }
            else
            {
                ESP_LOGI(TAG, "The converted value is %lld\n", value);
                struct view_data_time_cfg time_cfg_mannual;
                time_cfg_mannual.time = value;
                time_cfg_mannual.set_time = true;
                time_cfg_mannual.auto_update = false;
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_TIME_CFG_APPLY, &time_cfg_mannual, sizeof(time_cfg_mannual), portMAX_DELAY);
            }
        }
        cJSON *time_auto = cJSON_GetObjectItemCaseSensitive(data, "automatic");
        if (cJSON_IsNumber(time_auto))
        {
            int time_automatic = time_auto->valueint;
            set_time_automatic(AT_CMD_CALLER, time_automatic);
        }
        // get brightness item
        cJSON *brightness = cJSON_GetObjectItemCaseSensitive(data, "brightness");
        if (cJSON_IsNumber(brightness))
        {
            int brightness_value = brightness->valueint;
            set_brightness(AT_CMD_CALLER, brightness_value);
        }

        // get rgb_switch item
        cJSON *rgbswitch = cJSON_GetObjectItemCaseSensitive(data, "rgbswitch");
        if (cJSON_IsNumber(rgbswitch))
        {
            int rgbswitch_value = rgbswitch->valueint;
            set_rgb_switch(AT_CMD_CALLER, rgbswitch_value);
        }
        cJSON *soundvolume = cJSON_GetObjectItemCaseSensitive(data, "volume");
        if (cJSON_IsNumber(soundvolume))
        {
            int volume = soundvolume->valueint;
            set_sound(AT_CMD_CALLER, volume);
        }
    }
    else
    {
        ESP_LOGE(TAG, "failed at config json\n");
    }

    cJSON_Delete(json);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    int brightness_value_resp = get_brightness(AT_CMD_CALLER);
    int sound_value_resp = get_sound(AT_CMD_CALLER);
    cJSON *root = cJSON_CreateObject();
    cJSON *data_rep = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "deviceinfo=");
    cJSON_AddNumberToObject(root, "code", 0);
    char *json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON String in device cfg command: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/**
 * @brief Handles the "deviceinfo" command by generating a JSON response with device information.
 *
 * This function retrieves the software version and Himax software version, constructs a JSON object
 * containing various pieces of device information, and sends the JSON response.
 *
 * @param params A string containing the parameters for the command. This parameter is currently unused.
 *
 * The generated JSON object includes the following fields:
 * - name: "deviceinfo?"
 * - code: 0
 * - data: An object containing:
 *   - Eui: "1"
 *   - Token: "1"
 *   - Ble_Mac: "123"
 *   - Version: "1"
 *   - Time_Zone: "01"
 *   - Himax_Software_Version: The version of the Himax software.
 *   - Esp32_Software_Version: The version of the ESP32 software.
 *
 * The JSON string is then sent as an AT response.
 */
void handle_deviceinfo_command(char *params)
{
    ESP_LOGI(TAG, "handle_deviceinfo_command\n");
    char *software_version = get_software_version(AT_CMD_CALLER);
    char *himax_version = get_himax_software_version(AT_CMD_CALLER);

    int brightness_value_resp = get_brightness(AT_CMD_CALLER);
    int sound_value_resp = get_sound(AT_CMD_CALLER);
    int rgb_switch = get_rgb_switch(AT_CMD_CALLER);
    struct view_data_time_cfg cfg;
    get_current_time_cfg(&cfg);
    char timestamp_str[20];
    snprintf(timestamp_str, sizeof(timestamp_str), "%ld", cfg.time);
    ESP_LOGI(TAG, "Current time configuration:\n");
    ESP_LOGI(TAG, "zone: %d\n", cfg.zone);
    // ESP_LOGE(TAG,"get himax version:%s",himax_version);
    int time_auto = get_time_automatic(AT_CMD_CALLER);
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "name", "deviceinfo?");

    cJSON_AddNumberToObject(root, "code", 0);

    cJSON *data = cJSON_CreateObject();

    char eui_rsp[17] = { 0 };
    hex_to_string(get_eui(), 8, (const char *)eui_rsp);
    char bt_mac_rsp[13] = { 0 };
    hex_to_string(get_bt_mac(), 6, (const char *)bt_mac_rsp);
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "eui", (const char *)eui_rsp);
    cJSON_AddStringToObject(data, "blemac", (const char *)bt_mac_rsp);
    cJSON_AddStringToObject(data, "himaxsoftwareversion", (const char *)himax_version);
    cJSON_AddNumberToObject(data, "automatic", time_auto);
    cJSON_AddNumberToObject(data, "rgbswitch", rgb_switch);
    cJSON_AddNumberToObject(data, "sound", sound_value_resp);
    cJSON_AddNumberToObject(data, "brightness", brightness_value_resp);
    cJSON_AddStringToObject(data, "timestamp", timestamp_str);
    cJSON_AddNumberToObject(data, "timezone", cfg.zone);
    // add Himax_Software_Versionfield

    cJSON_AddStringToObject(data, "esp32softwareversion", (const char *)software_version);

    char *json_string = cJSON_Print(root);

    ESP_LOGI(TAG, "JSON Stringin handle_deviceinfo_command: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/**
 * @brief Handles the WiFi configuration command by parsing JSON input, setting the WiFi configuration, and generating a JSON response.
 *
 * This function parses the given parameters in JSON format to extract the SSID and password for WiFi configuration.
 * It then configures the WiFi settings using the extracted values, generates a JSON response containing the SSID and
 * connection status, and sends the response.
 *
 * @param params A JSON string containing the parameters for the WiFi configuration. The expected format is:
 * {
 *     "Ssid": "<your_ssid>",
 *     "Password": "<your_password>"
 * }
 *
 * The generated JSON object includes the following fields:
 * - name: The SSID of the WiFi network.
 * - code: The reason code for WiFi connection failure (if any).
 * - data: An object containing:
 *   - Ssid: The SSID of the WiFi network.
 *   - Rssi: The RSSI value (signal strength).
 *   - Encryption: The type of encryption used (e.g., WPA).
 */
void handle_wifi_set(char *params)
{
    ESP_LOGI(TAG, "Handling wifi command\n");
    char ssid[100];
    char password[100];
    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
    }
    cJSON *json_ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    cJSON *json_password = cJSON_GetObjectItemCaseSensitive(json, "password");
    if (cJSON_IsString(json_ssid) && (json_ssid->valuestring != NULL))
    {
        strncpy(ssid, json_ssid->valuestring, sizeof(ssid));
        ESP_LOGI(TAG, "SSID in json: %s\n", ssid);
    }
    else
    {
        ESP_LOGE(TAG, "SSID not found in JSON\n");
    }

    if (cJSON_IsString(json_password) && (json_password->valuestring != NULL))
    {
        strncpy(password, json_password->valuestring, sizeof(password));
        ESP_LOGI(TAG, "Password in json : %s\n", password);
    }
    else
    {
        ESP_LOGE(TAG, "Password not found in JSON\n");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();

    wifi_config *config = (wifi_config *)heap_caps_malloc(sizeof(wifi_config), MALLOC_CAP_SPIRAM);
    if (config == NULL)
    {
        ESP_LOGE("AT_CMD_CALLER", "Failed to allocate memory for wifi_config");
    }

    if (json_ssid && json_ssid->valuestring)
    {
        strncpy(config->ssid, json_ssid->valuestring, sizeof(config->ssid) - 1);
        config->ssid[sizeof(config->ssid) - 1] = '\0';
    }
    else
    {
        ESP_LOGE("AT_CMD_CALLER", "Invalid JSON SSID");
        config->ssid[0] = '\0';
    }

    if (json_password && json_password->valuestring)
    {
        strncpy(config->password, json_password->valuestring, sizeof(config->password) - 1);
        config->password[sizeof(config->password) - 1] = '\0';
    }
    else
    {
        ESP_LOGE("AT_CMD_CALLER", "Invalid JSON Password");
        config->password[0] = '\0';
    }

    config->caller = AT_CMD_CALLER;

    set_wifi_config(config);
    free(config);
    config = NULL;
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    cJSON_AddStringToObject(root, "name", config->ssid);
    cJSON_AddNumberToObject(root, "code", wifi_connect_failed_reason);
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "ssid", ssid);
    cJSON_AddStringToObject(data, "rssi", "2");
    cJSON_AddStringToObject(data, "encryption", "WPA");
    char *json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON String: %s", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/**
 * @brief Handles the WiFi query command by retrieving the current WiFi configuration and generating a JSON response.
 *
 * This function retrieves the currently connected WiFi network's SSID and RSSI (signal strength), constructs a JSON object
 * containing this information, and sends the JSON response.
 *
 * @param params A string containing the parameters for the command. This parameter is currently unused.
 *
 * The generated JSON object includes the following fields:
 * - name: "Wifi_Cfg"
 * - code: The network connection flag indicating the connection status.
 * - data: An object containing:
 *   - Ssid: The SSID of the currently connected WiFi network.
 *   - Rssi: The RSSI value (signal strength) of the current WiFi connection.
 */
void handle_wifi_query(char *params)
{
    ESP_LOGI(TAG, "Handling wifi query command\n");
    current_wifi_get(&current_connected_wifi);
    static char ssid_string[34];
    strncpy(ssid_string, (const char *)current_connected_wifi.ssid, sizeof(ssid_string) - 1);
    ssid_string[sizeof(ssid_string) - 1] = '\0';
    cJSON *root = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    // add json obj
    cJSON_AddStringToObject(root, "name", "Wifi_Cfg");
    cJSON_AddNumberToObject(root, "code", network_connect_flag); // finish
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "ssid", ssid_string);
    char rssi_str[10];
    snprintf(rssi_str, sizeof(rssi_str), "%d", current_connected_wifi.rssi);
    cJSON_AddStringToObject(data, "rssi", rssi_str);

    ESP_LOGI(TAG, "current_connected_wifi.ssid: %s", current_connected_wifi.ssid);
    ESP_LOGI(TAG, "current_connected_wifi.rssi: %d", current_connected_wifi.rssi);

    char *json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON String: %s", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/**
 * @brief Handles the WiFi table command by initializing the WiFi stack, simulating a WiFi scan, and generating a JSON response.
 *
 * This function initializes the WiFi stack, triggers a WiFi configuration task, waits for a specified duration, and then
 * simulates adding a WiFi network to the scanned WiFi stack. It then creates a JSON object representing the WiFi stack,
 * prints the JSON string, and sends it as an AT response.
 *
 * @param params A string containing the parameters for the command. This parameter is currently unused.
 *
 * The generated JSON object includes information about the WiFi networks that were scanned and the currently connected WiFi network.
 */
void handle_wifi_table(char *params)
{
    ESP_LOGI(TAG, "Handling wifi table command\n");
    initWiFiStack(&wifiStack_scanned, 6);
    xTaskNotifyGive(xTask_wifi_config_entry);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { "Network6", "-120", "WPA2" });
    cJSON *json = create_wifi_stack_json(&wifiStack_scanned, &wifiStack_connected);
    char *json_str = cJSON_Print(json);

    AT_Response response = create_at_response(json_str);
    ESP_LOGI(TAG, "JSON String: %s\n", json_str);
    send_at_response(&response);
    freeWiFiStack(&wifiStack_scanned);
}

void handle_taskflow_query_command(char *params)
{
    ESP_LOGI(TAG, "Handling handle_taskflow_query_command \n");
    vTaskDelay(10 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    cJSON *data_rep = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "taskflow");
    cJSON_AddNumberToObject(root, "code", task_flow_resp);
    char *json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON String: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/**
 * @brief Parses a JSON string and concatenates task information into an array of Task structures.
 *
 * This function is an auxiliary function for the handle_taskflow_command function. It parses a given JSON string
 * to extract task information such as name, package, sum, and data. It then allocates memory and stores this information
 * in an array of Task structures. The function handles the initialization of the task array if it is the first JSON being processed.
 *
 * @param json_string A JSON string containing task information. The expected format is:
 * {
 *     "name": "<task_name>",
 *     "package": <package_number>,
 *     "sum": <total_number_of_tasks>,
 *     "data": "<task_data>"
 * }
 *
 * The JSON object is expected to have the following fields:
 * - name: A string representing the name of the task.
 * - package: A number representing the package index of the task.
 * - sum: A number representing the total number of tasks.
 * - data: A string containing the task data.
 */
static size_t total_size;
void parse_json_and_concatenate(char *json_string)
{
    cJSON *json = cJSON_Parse(json_string);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Error parsing JSON\n");
    }

    cJSON *name = cJSON_GetObjectItem(json, "name");
    cJSON *package = cJSON_GetObjectItem(json, "package");
    cJSON *sum = cJSON_GetObjectItem(json, "sum");
    cJSON *data = cJSON_GetObjectItem(json, "data");
    cJSON *total_size = cJSON_GetObjectItem(json, "totalsize");
    if (!cJSON_IsString(name) || !cJSON_IsNumber(package) || !cJSON_IsNumber(sum) || !cJSON_IsString(data) || !cJSON_IsNumber(total_size))
    {
        ESP_LOGE(TAG, "Invalid JSON format\n");
        cJSON_Delete(json);
    }
    total_size = total_size->valueint;
    int index = package->valueint;

    if (num_jsons == 0)
    {
        num_jsons = sum->valueint;
        tasks = (Task *)heap_caps_malloc(num_jsons * sizeof(Task), MALLOC_CAP_SPIRAM);
        if (tasks == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for tasks\n");
            cJSON_Delete(json);
        }
        for (int i = 0; i < num_jsons; i++)
        {
            tasks[i].data = NULL;
        }
    }

    if (tasks == NULL || index < 0 || index >= num_jsons)
    {
        ESP_LOGE(TAG, "Tasks array is not properly allocated or index out of range\n");
        cJSON_Delete(json);
    }

    tasks[index].package = package->valueint;
    tasks[index].sum = sum->valueint;
    tasks[index].data = (char *)heap_caps_malloc(DATA_LENGTH + 1, MALLOC_CAP_SPIRAM);
    if (tasks[index].data == NULL)
    {
        ESP_LOGI(TAG, "Failed to allocate memory for data\n");
        cJSON_Delete(json);
    }
    strncpy(tasks[index].data, data->valuestring, DATA_LENGTH);
    tasks[index].data[DATA_LENGTH] = '\0'; // end

    cJSON_Delete(json);
}

/**
 * @brief Concatenates task data into a single result string after sorting tasks by their package index.
 *
 * This function is an auxiliary function for the handle_taskflow_command function. It sorts the tasks array
 * based on the package index in ascending order, concatenates the data fields of each task into a single result string,
 * and frees the allocated memory for each task and the tasks array.
 *
 * @param result A character array to store the concatenated result. The array should be pre-allocated with sufficient size
 * to hold the concatenated data from all tasks.
 */
void concatenate_data(char *result)
{
    // sort
    for (int i = 0; i < num_jsons - 1; i++)
    {
        for (int j = 0; j < num_jsons - 1 - i; j++)
        {
            if (tasks[j].package > tasks[j + 1].package)
            {
                Task temp = tasks[j];
                tasks[j] = tasks[j + 1];
                tasks[j + 1] = temp;
            }
        }
    }

    // concatenate_data
    result[0] = '\0';
    for (int i = 0; i < num_jsons; i++)
    {
        strcat(result, tasks[i].data);
        free(tasks[i].data);
    }
    num_jsons = 0;
    free(tasks);
    tasks = NULL;
}

/**
 * @brief Handles the taskflow command by parsing JSON input, managing task data, and generating a JSON response.
 *
 * This function parses the given parameters in JSON format to extract task information, stores the information in an array of Task structures,
 * checks if all tasks have been received, concatenates the task data into a single result string, and generates a JSON response.
 *
 * The function relies on auxiliary functions `parse_json_and_concatenate` to parse the input JSON and store the task information,
 * and `concatenate_data` to concatenate the task data.
 *
 * @param params A JSON string containing task information. The expected format for each task is:
 * {
 *     "name": "<task_name>",
 *     "package": <package_number>,
 *     "sum": <total_number_of_tasks>,
 *     "data": "<task_data>"
 * }
 *
 * The generated JSON response includes the following fields:
 * - name: "taskflow"
 * - code: The result code of the operation.
 * - data: An object representing additional response data.
 */

void base64_decode(const unsigned char *input, size_t input_len, unsigned char **output, size_t *output_len)
{
    size_t olen = 0;
    mbedtls_base64_decode(NULL, 0, &olen, input, input_len);

    *output = (unsigned char *)heap_caps_malloc(olen, MALLOC_CAP_SPIRAM);
    if (*output == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }

    int ret = mbedtls_base64_decode(*output, olen, output_len, input, input_len);
    if (ret != 0)
    {
        fprintf(stderr, "Base64 decoding failed\n");
        free(*output);
        *output = NULL;
    }
}

void handle_taskflow_command(char *params)
{
    esp_err_t code = ESP_OK;
    ESP_LOGI(TAG, "Handling taskflow command\n");
    parse_json_and_concatenate(params);

    int all_received = 1;
    for (int j = 0; j < num_jsons; j++)
    {
        if (tasks[j].data == NULL)
        {
            all_received = 0;
            break;
        }
    }
    if (all_received)
    {
        char *result = (char *)heap_caps_malloc(MEMORY_SIZE, MALLOC_CAP_SPIRAM);
        if (result == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for result\n");
            for (int k = 0; k < num_jsons; k++)
            {
                free(tasks[k].data);
            }
            free(tasks);
        }

        // try to make base64 decode
        concatenate_data(result);
        unsigned char *base64_output;
        size_t output_len;
        base64_decode((const unsigned char *)result, strlen(result), &base64_output, &output_len);
        free(result);
        esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_TASK_FLOW_START_BY_BLE, &base64_output, 4, portMAX_DELAY);
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    cJSON *data_rep = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "taskflow");
    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddItemToObject(root, "data", data_rep);
    char *json_string = cJSON_Print(root);
    ESP_LOGI(TAG, "JSON String: %s", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

/**
 * @brief Converts a hex array to a string and logs the hex data.
 *
 * This function is an auxiliary function for the task_handle_AT_command task. It takes a hex array,
 * converts each hex value to a character, and stores the result in an output string. The function
 * also logs the hex data for debugging purposes.
 *
 * @param hex A pointer to the array of hex values to be converted.
 * @param hex_size The size of the hex array.
 * @param output A pointer to the output buffer where the resulting string will be stored.
 *               The buffer should be large enough to hold the converted string and a null terminator.
 */
static void hex_to_string(uint8_t *in_data, int Size, char *out_data)
{
    for (unsigned char i = 0; i < Size; i++)
    {
        out_data[2 * i] = (in_data[i] >> 4);
        out_data[2 * i + 1] = (in_data[i] & 0x0F);
    }
    for (unsigned char i = 0; i < 2 * Size; i++)
    {
        if ((out_data[i] >= 0) && (out_data[i] <= 9))
        {
            out_data[i] = '0' + out_data[i];
        }
        else if ((out_data[i] >= 0x0A) && (out_data[i] <= 0x0F))
        {
            out_data[i] = 'A' - 10 + out_data[i];
        }
        else
        {
            return;
        }
    }
}
/**
 * @brief A static task that handles incoming AT commands, parses them, and executes the corresponding actions.
 *
 * This function runs in an infinite loop, receiving messages from a stream buffer within bluetooth. It parses the received AT commands,
 * converts the hex data to a string, and uses regular expressions to match and extract command details.
 * The extracted command is then executed. The function relies on auxiliary functions like `hex_to_string` to process
 * the received data.
 *
 * This task is declared static, indicating that it is intended to be used only within the file it is defined in,and placed in PSRAM
 */

QueueHandle_t message_queue;
void task_handle_AT_command()
{
    while (1)
    {
        message_event_t msg_at;
        if (xQueueReceive(message_queue, &msg_at, portMAX_DELAY) == pdPASS) { }
        else
        {
            ESP_LOGE(TAG, "Failed to receive message from queue\n");
        }

        char *test_strings = (char *)heap_caps_malloc(msg_at.size + 1, MALLOC_CAP_SPIRAM);
        memcpy(test_strings, msg_at.msg, msg_at.size);
        free(msg_at.msg);
        msg_at.msg = NULL;
        test_strings[msg_at.size] = '\0';

        if (test_strings == NULL)
        {
            ESP_LOGE(TAG, "Memory allocation failed\n");
        }
        ESP_LOGI(TAG, "AT command received\n");
        regex_t regex;
        int ret;
        ret = regcomp(&regex, pattern, REG_EXTENDED);
        if (ret)
        {
            ESP_LOGI(TAG, "Could not compile regex\n");
        }
        regmatch_t matches[4];
        ret = regexec(&regex, test_strings, 4, matches, 0);
        if (!ret)
        {
            // ESP_LOGI("recv_in match: %.*s\n", test_strings);
            char command_type[20];
            snprintf(command_type, sizeof(command_type), "%.*s", (int)(matches[1].rm_eo - matches[1].rm_so), test_strings + matches[1].rm_so);

            size_t data_size = 10320;
            char *params = (char *)heap_caps_malloc(data_size + 1, MALLOC_CAP_SPIRAM);
            if (matches[3].rm_so != -1)
            {
                int length = (int)(matches[3].rm_eo - matches[3].rm_so);
                snprintf(params, length + 1, "%.*s", (int)(matches[3].rm_eo - matches[3].rm_so), test_strings + matches[3].rm_so);
                printf("Matched string: %.50s... (total length: %d)\n", params, length);
            }
            char query_type = test_strings[matches[1].rm_eo] == '?' ? '?' : '=';
            exec_command(&commands, command_type, params, query_type);
            free(params);
            params = NULL;
        }
        else if (ret == REG_NOMATCH)
        {
            ESP_LOGE(TAG, "No match: %s\n", test_strings);
        }
        else
        {
            char errbuf[100];
            regerror(ret, &regex, errbuf, sizeof(errbuf));
            ESP_LOGE(TAG, "Regex match failed: %s\n", errbuf);
        }
        free(test_strings);
        regfree(&regex);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        xTaskNotifyGive(xTaskToNotify_AT);
    }
}

/**
 * @brief Initializes the AT command handling task by creating a stream buffer and the associated task.
 *
 * This function sets up the necessary resources for handling AT commands. It creates a stream buffer for
 * receiving messages and starts the `task_handle_AT_command` task to process these messages.
 */

void init_at_cmd_task(void)
{
    message_queue = xQueueCreate(MESSAGE_QUEUE_SIZE, sizeof(message_event_t));
    if (message_queue == NULL)
    {
        ESP_LOGI(TAG, "Failed to create queue\n");
        return;
    }
    at_task_stack = (StackType_t *)heap_caps_malloc(10240 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (at_task_stack == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for WiFi task stack\n");
        return;
    }
    TaskHandle_t at_task_handle = xTaskCreateStatic(task_handle_AT_command, "wifi_config_entry", 10240, NULL, 9, at_task_stack, &at_task_buffer);

    if (at_task_handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi task\n");
        free(at_task_handle);
        at_task_handle = NULL;
    }
}

/**
 * @brief Creates a queue for AT command responses.
 *
 * This function initializes a queue that can hold up to 10 AT_Response items.
 * The queue is used to manage the responses to AT commands.
 */
void create_AT_response_queue()
{
    AT_response_queue = xQueueCreate(10, sizeof(AT_Response));
}

/**
 * @brief Initializes a binary semaphore for managing AT command responses.
 *
 * This function creates a binary semaphore and gives it initially to ensure it is available.
 * The semaphore is used to synchronize access to AT command responses.
 */
void init_AT_response_semaphore()
{
    AT_response_semaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(AT_response_semaphore);
}

/**
 * @brief Sends an AT command response to the response queue.
 *
 * This function takes a binary semaphore to ensure exclusive access to the AT response queue,
 * then sends the given AT response to the queue. After sending the response, it gives back the semaphore.
 *
 * @param AT_Response A pointer to the AT_Response structure to be sent to the queue.
 */
void send_at_response(AT_Response *AT_Response)
{
    if (xSemaphoreTake(AT_response_semaphore, portMAX_DELAY))
    {
        if (!xQueueSend(AT_response_queue, AT_Response, 0))
        {
            ESP_LOGI(TAG, "Failed to send AT response\n");
        }
        xSemaphoreGive(AT_response_semaphore);
    }
}

/**
 * @brief Creates an AT command response by appending a standard suffix to the given message.
 *
 * This function takes a message string, appends the standard suffix "\r\nok\r\n" to it,
 * and allocates memory for the complete response. It returns an AT_Response structure
 * containing the formatted response and its length.
 *
 * @param message A constant character pointer to the message to be included in the response.
 *                If the message is NULL, an empty response is created.
 * @return AT_Response A structure containing the formatted response string and its length.
 */
AT_Response create_at_response(const char *message)
{
    AT_Response response;
    if (message)
    {
        const char *suffix = "\r\nok\r\n";
        size_t total_length = strlen(message) + strlen(suffix) + 1;
        response.response = heap_caps_malloc(total_length, MALLOC_CAP_SPIRAM); // +1 for null terminator
        if (response.response)
        {
            strcpy(response.response, message);
            strcat(response.response, suffix);
            response.length = strlen(response.response);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to allocate memory for AT response\n");

            response.response = NULL;
            response.length = 0;
        }
    }
    else
    {
        response.response = NULL;
        response.length = 0;
    }
    return response;
}

/**
 * @brief Initializes the AT command handling system.
 *
 * This function sets up the necessary components for handling AT commands, including creating the response queue,
 * initializing semaphores, and initializing tasks and WiFi stacks.
 */
void app_at_cmd_init()
{
    create_AT_response_queue();
    init_AT_response_semaphore();
    wifi_stack_semaphore_init();
    init_at_cmd_task();
    initWiFiStack(&wifiStack_scanned, 10);
    initWiFiStack(&wifiStack_connected, 10);

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_WIFI_ST, 
                                                    __view_event_handler, 
                                                    NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_TASK_FLOW_STATUS, 
                                                    __view_event_handler, 
                                                    NULL));

}

/**
 * @brief Frees all allocated memory for AT command entries in the hash table.
 *
 * This function iterates over all command entries in the hash table, deletes each entry from the hash table,
 * and frees the allocated memory for each command entry.
 */
void AT_command_free()
{
    command_entry *current_command, *tmp;
    HASH_ITER(hh, commands, current_command, tmp)
    {
        HASH_DEL(commands, current_command); // Delete the entry from the hash table
        free(current_command);
    }
}

/**
 * @brief Handles view events related to WiFi configuration and status updates.
 *
 * This static function processes various WiFi-related events such as WiFi list requests,
 * WiFi list updates, and WiFi status updates. It updates the WiFi stacks and network connection flags accordingly.
 *
 * @param handler_args A pointer to the handler arguments (unused in this function).
 * @param base The event base, typically identifying the module generating the event.
 * @param id The event ID, specifying the particular event being handled.
 * @param event_data A pointer to the event data, providing context-specific information.
 */
static void __view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    struct view_data_wifi_st *p_cfg;
    switch (id)
    {
        case VIEW_EVENT_WIFI_ST:
            static bool fist = true;
            ESP_LOGI("AT_CMD_EVENT_READ:", "event: VIEW_EVENT_WIFI_ST");
            struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
            if (p_st->is_network)
            { // todo
                network_connect_flag = 1;
            }
            else
            {
                network_connect_flag = 0;
            }
            break;
        case VIEW_EVENT_TASK_FLOW_STATUS: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_STATUS");
            struct view_data_taskflow_status *p_cfg = (struct view_data_taskflow_status *)event_data;
            task_flow_resp = p_cfg->engine_status;
            break;
        }
        default:
            break;
    }
}
