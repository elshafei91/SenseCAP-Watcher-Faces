
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
#include "app_wifi.h"
#include "esp_wifi.h"
#include "event_loops.h"
#include "data_defs.h"
#include "portmacro.h"
#include "uhash.h"
#include "at_cmd.h"
#include "cJSON.h"
#include "system_layer.h"

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

// Data Structure
/*-----------------------------------*/
// wifi table




SemaphoreHandle_t wifi_stack_semaphore;
/*-----------------------------------*/

// Data Structure process function
void wifi_stack_semaphore_init()
{
    wifi_stack_semaphore = xSemaphoreCreateMutex();
}
void initWiFiStack(WiFiStack *stack, int capacity)
{
    stack->entries = (WiFiEntry *)heap_caps_malloc(capacity * sizeof(WiFiEntry), MALLOC_CAP_SPIRAM);
    stack->size = 0;
    stack->capacity = capacity;
}

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

void freeWiFiStack(WiFiStack *stack)
{
    free(stack->entries);
    stack->entries = NULL;
    stack->size = 0;
    stack->capacity = 0;
}

cJSON *create_wifi_entry_json(WiFiEntry *entry)
{
    cJSON *wifi_json = cJSON_CreateObject();
    cJSON_AddStringToObject(wifi_json, "Ssid", entry->ssid);
    cJSON_AddStringToObject(wifi_json, "Rssi", entry->rssi);
    cJSON_AddStringToObject(wifi_json, "Encryption", entry->encryption);
    return wifi_json;
}

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

// AT command system layer
/*----------------------------------------------------------------------------------------------------*/
SemaphoreHandle_t AT_response_semaphore;
QueueHandle_t AT_response_queue;
void create_AT_response_queue();
void init_AT_response_semaphore();
void send_at_response(AT_Response *AT_Response);
AT_Response create_at_response(const char *message);
const char *pattern = "^AT\\+([a-zA-Z0-9]+)(\\?|=([^\\n]*))?\r\n$";

command_entry *commands = NULL; // Global variable to store the commands

void add_command(command_entry **commands, const char *name, void (*func)(char *params))
{
    command_entry *entry = (command_entry *)malloc(sizeof(command_entry)); // Allocate memory for the new entry
    strcpy(entry->command_name, name);                                     // Copy the command name to the new entry
    entry->func = func;                                                    // Assign the function pointer to the new entry
    HASH_ADD_STR(*commands, command_name, entry);                          // Add the new entry to the hash table
}

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

void AT_command_reg()
{ // Register the AT commands
    add_command(&commands, "type1=", handle_type_1_command);
    add_command(&commands, "device=", handle_device_command);
    add_command(&commands, "wifi=", handle_wifi_set);
    add_command(&commands, "wifi?", handle_wifi_query);
    add_command(&commands, "eui=", handle_eui_command);
    add_command(&commands, "token=", handle_token);
    add_command(&commands, "wifitable?", handle_wifi_table);
}

void handle_type_1_command(char *params)
{
    printf("Handling type 1 command\n");
    printf("Params: %s\n", params);
}

void handle_device_command(char *params)
{
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

    int code = 0;
    wifi_config *config = (wifi_config *)heap_caps_malloc(sizeof(wifi_config), MALLOC_CAP_SPIRAM);
    strncpy(config->ssid, ssid, sizeof(config->ssid) - 1);
    config->ssid[sizeof(config->ssid) - 1] = '\0'; // Ensure null-termination

    strncpy(config->password, password, sizeof(config->password) - 1);
    config->password[sizeof(config->password) - 1] = '\0'; // Ensure null-termination
    config->caller = AT_CMD_CALLER;
    code = set_wifi_config(config);
    cJSON_AddStringToObject(root, "name", "Wifi_Cfg");
    cJSON_AddNumberToObject(root, "code", code);
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
    cJSON *root = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    // 填充 JSON 对象
    cJSON_AddStringToObject(root, "name", "Wifi_Cfg");
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data);
    cJSON_AddStringToObject(data, "Ssid", "SEEED-2.4G");
    cJSON_AddStringToObject(data, "Rssi", "2");
    cJSON_AddStringToObject(data, "Encryption", "WPA");
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
    xTaskNotifyGive(xTask_wifi_config_layer);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    // pushWiFiStack(&wifiStack_connected, (WiFiEntry) { "Network0_connected", "-60", "WPA" });
    // pushWiFiStack(&wifiStack_connected, (WiFiEntry) { "Network1_connected", "-70", "WPA2" });
    // pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { "Network1", "-70", "WPA2" });
    // pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { "Network2", "-80", "WEP" });
    // pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { "Network3", "-90", "WPA" });
    // pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { "Network4", "-100", "WPA2" });
    // pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { "Network5", "-110", "WPA" });
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
        esp_log_buffer_hex("HEX TAG3", msg_at->msg, msg_at->size);
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

        char params[100] = "";
        if (matches[3].rm_so != -1)
        {
            snprintf(params, sizeof(params), "%.*s", (int)(matches[3].rm_eo - matches[3].rm_so), test_strings + matches[3].rm_so);
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
    vTaskDelay(5000 / portTICK_PERIOD_MS); // delay 5s
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
    // command data struct initialization
    // initWiFiStack(&wifiStack, 10);
}
void AT_command_free()
{
    command_entry *current_command, *tmp;
    HASH_ITER(hh, commands, current_command, tmp)
    {
        HASH_DEL(commands, current_command); // Delete the entry from the hash table
        free(current_command);               // Free the memory allocated for the entry
    }

    // data struct free
    // free(json_str);
    // freeWiFiStack(&wifiStack);
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
            pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { "Network6", "-120", "WPA2" });
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
            pushWiFiStack(&wifiStack_scanned, (WiFiEntry) { p_cfg->ssid, p_cfg->rssi, authmode_s });
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