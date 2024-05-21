#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <regex.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "app_wifi.h"
#include "event_loops.h"
#include "data_defs.h"
#include "portmacro.h"
#include "uhash.h"
#include "at_cmd.h"
#include "cJSON.h"
#include "app_device_info.h"

#ifdef DEBUG_AT_CMD
char *test_strings[] = { "\rAT+type1?\n", "\rAT+wifi={\"Ssid\":\"Watcher_Wifi\",\"Password\":\"12345678\"}\n",
    "\rAT+type3\n", // Added a test string without parameters
    NULL };
// array to hold task status
TaskStatus_t pxTaskStatusArray[TASK_STATS_BUFFER_SIZE];
// task number
UBaseType_t uxArraySize, x;
// total run time
uint32_t ulTotalRunTime;
#endif




SemaphoreHandle_t wifi_stack_semaphore;
static int network_connect_flag;
static wifi_ap_record_t current_connected_wifi;







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
    cJSON_AddStringToObject(wifi_json, "Ssid", entry->ssid);
    cJSON_AddStringToObject(wifi_json, "Rssi", entry->rssi);
    cJSON_AddStringToObject(wifi_json, "Encryption", entry->encryption);
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
    cJSON_AddItemToObject(root, "Connected_Wifi", connected_array);
    cJSON_AddItemToObject(root, "Scanned_Wifi", scanned_array);
    return root;
}

/*--------------------------------test for tf engin set function only for debug---------------------*/
esp_err_t tf_engine_flow_set(const char *p_str, size_t len);











// AT command system layer
/*----------------------------------------------------------------------------------------------------*/
SemaphoreHandle_t AT_response_semaphore;
QueueHandle_t AT_response_queue;
void create_AT_response_queue();
void init_AT_response_semaphore();
void send_at_response(AT_Response *AT_Response);
AT_Response create_at_response(const char *message);
// const char *pattern = "^AT\\+([a-zA-Z0-9]+)(\\?|=([^\\n]*))?\r\n$";
const char *pattern = "^AT\\+([a-zA-Z0-9]+)(\\?|=(\\{.*\\}))?\r\n$";
command_entry *commands = NULL; // Global variable to store the commands







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
    command_entry *entry = (command_entry *)malloc(sizeof(command_entry)); // Allocate memory for the new entry
    strcpy(entry->command_name, name);                                     // Copy the command name to the new entry
    entry->func = func;                                                    // Assign the function pointer to the new entry
    HASH_ADD_STR(*commands, command_name, entry);                          // Add the new entry to the hash table
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
        printf("Command not found\n");
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
    add_command(&commands, "type1=", handle_type_1_command);
    add_command(&commands, "deviceinfo?", handle_deviceinfo_command);
    add_command(&commands, "wifi=", handle_wifi_set);
    add_command(&commands, "wifi?", handle_wifi_query);
    add_command(&commands, "eui=", handle_eui_command);
    add_command(&commands, "token=", handle_token);
    add_command(&commands, "wifitable?", handle_wifi_table);
    add_command(&commands, "devicecfg=", handle_deviceinfo_cfg_command);
    add_command(&commands, "taskflow=", handle_taskflow_command);
    // add_command(&commands, "deviceinfo?", handle_deviceinfo_command);
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
    printf("handle_deviceinfo_cfg_command\n");


    cJSON *json = cJSON_Parse(params);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
        return;
    }

    cJSON *data = cJSON_GetObjectItemCaseSensitive(json, "data");
    if (cJSON_IsObject(data))
    {
        // Get the "Time_Zone" item
        cJSON *time_zone = cJSON_GetObjectItemCaseSensitive(data, "Time_Zone");
        if (cJSON_IsNumber(time_zone))
        {
            int timezone = time_zone->valueint;
            struct view_data_time_cfg time_cfg;
            time_cfg.zone = timezone;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_TIME_ZONE, &time_cfg, sizeof(time_cfg), portMAX_DELAY);
        }
    }
    else
    {
        printf("Time_Zone not found or not a valid string in JSON\n");
    }


    cJSON_Delete(json);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    cJSON *data_rep = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "timezone");
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data_rep);
    cJSON_AddStringToObject(data_rep, "timezone", "");
    cJSON_AddStringToObject(data_rep, "wakeword", "");
    cJSON_AddStringToObject(data_rep, "volume", "");
    char *json_string = cJSON_Print(root);
    printf("JSON String: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}




void handle_type_1_command(char *params)
{
    printf("Handling type 1 command\n");
    printf("Params: %s\n", params);
}

void handle_deviceinfo_command(char *params)
{
    printf("handle_deviceinfo_command\n");
    char *software_version = get_software_version(AT_CMD_CALLER);
    char *himax_version = get_himax_software_version(AT_CMD_CALLER);
    // uint8_t *hardwareversion = get_hardware_version();
   
    cJSON *root = cJSON_CreateObject();

    
    cJSON_AddStringToObject(root, "name", "deviceinfo?");


    cJSON_AddNumberToObject(root, "code", 0);

  
    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);


    cJSON_AddStringToObject(data, "Eui", "1");
    cJSON_AddStringToObject(data, "Token", "1");
    cJSON_AddStringToObject(data, "Ble_Mac", "123");
    cJSON_AddStringToObject(data, "Version", "1");
    cJSON_AddStringToObject(data, "Time_Zone", "01");

    // add Himax_Software_Versionfield
    cJSON_AddStringToObject(data, "Himax_Software_Version", (const char *)himax_version);

    cJSON_AddStringToObject(data, "Esp32_Software_Version", (const char *)software_version);


    char *json_string = cJSON_Print(root);


    // vTaskDelay(1000 / portTICK_PERIOD_MS);
    printf("JSON String: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);

    printf("Handling device command\n");
}

void handle_wifi_set(char *params)
{
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
    cJSON *json_ssid = cJSON_GetObjectItemCaseSensitive(json, "Ssid");
    cJSON *json_password = cJSON_GetObjectItemCaseSensitive(json, "Password");
    if (cJSON_IsString(json_ssid) && (json_ssid->valuestring != NULL))
    {
        strncpy(ssid, json_ssid->valuestring, sizeof(ssid));
        printf("SSID in json: %s\n", ssid);
    }
    else
    {
        printf("SSID not found in JSON\n");
    }

    if (cJSON_IsString(json_password) && (json_password->valuestring != NULL))
    {
        strncpy(password, json_password->valuestring, sizeof(password));
        printf("Password in json : %s\n", password);
    }
    else
    {
        printf("Password not found in JSON\n");
    }
    printf("Handling wifi command\n");
    cJSON *root = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();

    wifi_config *config = (wifi_config *)heap_caps_malloc(sizeof(wifi_config), MALLOC_CAP_SPIRAM);
    if (config == NULL)
    {
        ESP_LOGE("AT_CMD_CALLER", "Failed to allocate memory for wifi_config");
        return;
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

    ESP_LOGE("AT_CMD_CALLER die01_ssid", "base:%s, memcpy:%s", json_ssid->valuestring, config->ssid);
    ESP_LOGE("AT_CMD_CALLER die01_password", "base:%s, memcpy:%s", json_password->valuestring, config->password);

    // int code = ! current_connected_wifi.is_connected;  // read doc
    set_wifi_config(config);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    cJSON_AddStringToObject(root, "name", config->ssid);
    cJSON_AddNumberToObject(root, "code", wifi_connect_failed_reason);
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "Ssid", ssid);
    cJSON_AddStringToObject(data, "Rssi", "2");
    cJSON_AddStringToObject(data, "Encryption", "WPA");
    char *json_string = cJSON_Print(root);
    printf("JSON String: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}

void handle_wifi_query(char *params)
{
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
    cJSON_AddStringToObject(data, "Ssid", ssid_string);
    char rssi_str[10];
    snprintf(rssi_str, sizeof(rssi_str), "%d", current_connected_wifi.rssi);
    cJSON_AddStringToObject(data, "Rssi", rssi_str);

    printf("current_connected_wifi.ssid: %s\n", current_connected_wifi.ssid);
    printf("current_connected_wifi.rssi: %d\n", current_connected_wifi.rssi);
    // cJSON_AddStringToObject(data, "Encryption", "WPA");
    char *json_string = cJSON_Print(root);
    printf("JSON String: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
    printf("Handling wifi query command\n");
}

void handle_wifi_table(char *params)
{
    initWiFiStack(&wifiStack_scanned, 6);
    xTaskNotifyGive(xTask_wifi_config_entry);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { "Network6", "-120", "WPA2" });
    cJSON *json = create_wifi_stack_json(&wifiStack_scanned, &wifiStack_connected);
    char *json_str = cJSON_Print(json);

    AT_Response response = create_at_response(json_str);
    printf("JSON String: %s\n", json_str);
    send_at_response(&response);
    printf("Handling wifi table command\n");
    freeWiFiStack(&wifiStack_scanned);
}

void handle_token(char *params)
{
    printf("Handling token command\n");
}

void handle_eui_command(char *params)
{
    printf("Handling eui command\n");
}

/*------------------critical command for task_flow-------------------------------------------*/

#define DATA_LENGTH 4096

typedef struct {
    char *name;
    int package;
    int sum;
    char *data;
} Task;

Task *tasks = NULL;
int num_jsons = 0;

void parse_json_and_concatenate(char *json_string) {
    
    printf("Params: %s\n", json_string);
    cJSON *json = cJSON_Parse(json_string);
    if (json == NULL) {
        printf("Error parsing JSON\n");
        return;
    }

    cJSON *name = cJSON_GetObjectItem(json, "name");
    cJSON *package = cJSON_GetObjectItem(json, "package");
    cJSON *sum = cJSON_GetObjectItem(json, "sum");
    cJSON *data = cJSON_GetObjectItem(json, "data");

    if (!cJSON_IsString(name) || !cJSON_IsNumber(package) || !cJSON_IsNumber(sum) || !cJSON_IsString(data)) {
        printf("Invalid JSON format\n");
        cJSON_Delete(json);
        return;
    }

    if (num_jsons == 0) {
        num_jsons = sum->valueint;
        tasks = (Task *)heap_caps_malloc(num_jsons * sizeof(Task), MALLOC_CAP_SPIRAM);
        if (tasks == NULL) {
            printf("Failed to allocate memory for tasks\n");
            cJSON_Delete(json);
            return;
        }

        // init
        for (int i = 0; i < num_jsons; i++) {
            tasks[i].name = NULL;
            tasks[i].data = NULL;
        }
    }

    int index = package->valueint;
    tasks[index].name = (char *)heap_caps_malloc(strlen(name->valuestring) + 1, MALLOC_CAP_SPIRAM);
    if (tasks[index].name == NULL) {
        printf("Failed to allocate memory for name\n");
        cJSON_Delete(json);
        return;
    }
    strcpy(tasks[index].name, name->valuestring);

    tasks[index].package = package->valueint;
    tasks[index].sum = sum->valueint;
    tasks[index].data = (char *)heap_caps_malloc(DATA_LENGTH + 1, MALLOC_CAP_SPIRAM);
    if (tasks[index].data == NULL) {
        printf("Failed to allocate memory for data\n");
        free(tasks[index].name);
        cJSON_Delete(json);
        return;
    }
    strncpy(tasks[index].data, data->valuestring, DATA_LENGTH);
    tasks[index].data[DATA_LENGTH] = '\0'; // end

    cJSON_Delete(json);
}

void concatenate_data(char *result) {
    // sort
    for (int i = 0; i < num_jsons - 1; i++) {
        for (int j = 0; j < num_jsons - 1 - i; j++) {
            if (tasks[j].package > tasks[j + 1].package) {
                Task temp = tasks[j];
                tasks[j] = tasks[j + 1];
                tasks[j + 1] = temp;
            }
        }
    }

    // concatenate_data
    result[0] = '\0';
    for (int i = 0; i < num_jsons; i++) {
        strcat(result, tasks[i].data);
        free(tasks[i].data); 
        free(tasks[i].name); 
    }

    free(tasks); 
    tasks = NULL;
}




void handle_taskflow_command(char *params)
{
    esp_err_t code=ESP_OK;
    printf("Handling taskflow command\n");
    printf("Params: %s\n", params);
    parse_json_and_concatenate(params);

    int all_received = 1;
    for (int j = 0; j < num_jsons; j++) {
        if (tasks[j].data == NULL) {
            all_received = 0;
            break;
        }
    }
    if (all_received) {
        char *result = (char *)heap_caps_malloc(DATA_LENGTH * num_jsons + 1, MALLOC_CAP_SPIRAM); // 根据sum动态分配内存
        if (result == NULL) {
            printf("Failed to allocate memory for result\n");
            for (int k = 0; k < num_jsons; k++) {
                free(tasks[k].name);
                free(tasks[k].data);
            }
            free(tasks);
            return;
        }

        concatenate_data(result);

        printf("Final data: %s\n", result);

        free(result); 
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
    cJSON *root = cJSON_CreateObject();
    cJSON *data_rep = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "taskflow");
    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddItemToObject(root, "data", data_rep);
    char *json_string = cJSON_Print(root);
    printf("JSON String: %s\n", json_string);
    AT_Response response = create_at_response(json_string);
    send_at_response(&response);
    cJSON_Delete(root);
}










static void hex_to_string(uint8_t *hex, int hex_size, char *output)
{
    esp_log_buffer_hex("HEX TAG1", hex, hex_size);
    for (int i = 0; i <= hex_size; i++)
    {
        output[i] = (char)hex[i];
    }
    output[hex_size] = '\0';
}






esp_event_loop_handle_t at_event_loop_handle;

ESP_EVENT_DEFINE_BASE(AT_EVENTS);

void task_handle_AT_command(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    size_t memory_size = MEMORY_SIZE;
    message_event_t *msg_at = (message_event_t *)event_data;
    char *test_strings = (char *)heap_caps_malloc(memory_size, MALLOC_CAP_SPIRAM);
    if (test_strings == NULL)
    {
        printf("Memory allocation failed\n");
        return;
    }
    if (base == AT_EVENTS && id == AT_EVENTS_COMMAND_ID)
    {
        printf("AT command received\n");
        //esp_log_buffer_hex("HEX TAG3", msg_at->msg, msg_at->size);
        hex_to_string(msg_at->msg, msg_at->size, test_strings);
        printf("recv: %.*s\n", 1024, test_strings);
    }

    regex_t regex;
    int ret;
    ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret)
    {
        printf("Could not compile regex\n");
    }
    regmatch_t matches[4];
    ret = regexec(&regex, test_strings, 4, matches, 0);
    if (!ret)
    {
        printf("recv_in match: %.*s\n", 1024, test_strings);
        char command_type[20];
        snprintf(command_type, sizeof(command_type), "%.*s", (int)(matches[1].rm_eo - matches[1].rm_so), test_strings + matches[1].rm_so);

        // char params[100] = "";
        size_t data_size = 100 * 1024; // 100K
        char *params = (char *)heap_caps_malloc(data_size + 1, MALLOC_CAP_SPIRAM);
        if (matches[3].rm_so != -1)
        {
            int length = (int)(matches[3].rm_eo - matches[3].rm_so);
            snprintf(params, length+1, "%.*s", (int)(matches[3].rm_eo - matches[3].rm_so), test_strings + matches[3].rm_so);
            //snprintf(params, length + 1, "%.*s", length, test_strings + matches[1].rm_so);
            printf("Matched string: %.50s... (total length: %d)\n", params, length);
        }
        char query_type = test_strings[matches[1].rm_eo] == '?' ? '?' : '=';
        exec_command(&commands, command_type, params, query_type);
    }
    else if (ret == REG_NOMATCH)
    {
        printf("No match: %s\n", test_strings);
    }
    else
    {
        char errbuf[100];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        printf("Regex match failed: %s\n", errbuf);
    }
    free(test_strings);
    regfree(&regex);
    vTaskDelay(500 / portTICK_PERIOD_MS); // delay 5s
}

void init_event_loop_and_task(void)
{
    esp_event_loop_args_t loop_args = { .queue_size = 20, .task_name = "task_AT_command", .task_priority = uxTaskPriorityGet(NULL), .task_stack_size = 2048 * 2, .task_core_id = tskNO_AFFINITY };

    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &at_event_loop_handle));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(at_event_loop_handle, AT_EVENTS, ESP_EVENT_ANY_ID, task_handle_AT_command, NULL, NULL));

    ESP_LOGE(AT_EVENTS_TAG, "Event loop created and handler registered");
}

void create_AT_response_queue()
{
    AT_response_queue = xQueueCreate(10, sizeof(AT_Response));
}

void init_AT_response_semaphore()
{
    AT_response_semaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(AT_response_semaphore);
}

void send_at_response(AT_Response *AT_Response)
{
    if (xSemaphoreTake(AT_response_semaphore, portMAX_DELAY))
    {
        if (!xQueueSend(AT_response_queue, AT_Response, 0))
        {
            printf("Failed to send AT response\n");
        }
        xSemaphoreGive(AT_response_semaphore);
    }
}

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
            printf("Failed to allocate memory for AT response\n");

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

void AT_cmd_init()
{
    create_AT_response_queue();
    init_AT_response_semaphore();
    wifi_stack_semaphore_init();
    init_event_loop_and_task();
    initWiFiStack(&wifiStack_scanned, 10);
    initWiFiStack(&wifiStack_connected, 10);
}
void AT_command_free()
{
    command_entry *current_command, *tmp;
    HASH_ITER(hh, commands, current_command, tmp)
    {
        HASH_DEL(commands, current_command);// Delete the entry from the hash table
        free(current_command);               
    }
}

// event_handle

static void __view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    struct view_data_wifi_st *p_cfg;
    switch (id)
    {
        case VIEW_EVENT_WIFI_LIST_REQ:
            p_cfg = (struct view_data_wifi_config *)event_data;
            pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { p_cfg->ssid, "", "" });
        case VIEW_EVENT_WIFI_LIST:
            p_cfg = (struct view_data_wifi_config *)event_data;
            char *authmode_s;
            pushWiFiStack(&wifiStack_connected, (WiFiEntry) { "Network6", "-120", "WPA2" });
            if (p_cfg->authmode == WIFI_AUTH_WEP)
            {
                authmode_s = "WEP";
            }
            else if (p_cfg->authmode == WIFI_AUTH_WPA_PSK)
            {
                authmode_s = "WPA_PSK";
            }
            else if (p_cfg->authmode == WIFI_AUTH_WPA2_PSK)
            {
                authmode_s = "WPA2_PSK";
            }
            else
            {
                authmode_s = "NONE";
            }
            pushWiFiStack(&wifiStack_connected, (WiFiEntry) { p_cfg->ssid, p_cfg->rssi, authmode_s });
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

        default:
            break;
    }
}

#ifdef DEBUG_AT_CMD
void vTaskMonitor(void *para)
{
    while (1)
    {
        //  get the number of tasks
        uxArraySize = uxTaskGetNumberOfTasks();

        // make sure the array size is not greater than the buffer size
        if (uxArraySize > TASK_STATS_BUFFER_SIZE)
        {
            uxArraySize = TASK_STATS_BUFFER_SIZE;
        }

        // get the task status
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

        // output the task status
        for (x = 0; x < uxArraySize; x++)
        {
            printf("Task %s:\n\tState: %u\n\tPriority: %u\n\tStack High Water Mark: %lu\n", pxTaskStatusArray[x].pcTaskName, pxTaskStatusArray[x].eCurrentState, pxTaskStatusArray[x].uxCurrentPriority,
                pxTaskStatusArray[x].usStackHighWaterMark);
        }

        // wait for 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

#endif