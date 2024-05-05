
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
#include "portmacro.h"
#include "uhash.h"
#include "at_cmd.h"
#include "cJSON.h"

#ifdef DEBUG_AT_CMD
char *test_strings[] = {
    "\rAT+type1?\n",
    "\rAT+wifi={\"Ssid\":\"Watcher_Wifi\",\"Password\":\"12345678\"}\n",
    "\rAT+type3\n", // Added a test string without parameters
    NULL};
// array to hold task status
TaskStatus_t pxTaskStatusArray[TASK_STATS_BUFFER_SIZE];
// task number
UBaseType_t uxArraySize, x;
// total run time
uint32_t ulTotalRunTime;
#endif

SemaphoreHandle_t AT_response_semaphore;
QueueHandle_t AT_response_queue;
void create_AT_response_queue();
void init_AT_response_semaphore();
void send_at_response(AT_Response *AT_Response);
AT_Response create_at_response(const char *message);
const char *pattern = "^\rAT\\+([a-zA-Z0-9]+)(\\?|=([^\\n]*))?\n$";

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
    snprintf(full_command, sizeof(full_command), "%s%c", name, query); // 区分命令名和类型
    HASH_FIND_STR(*commands, full_command, entry);
    if (entry)
    {
        if (query == '?')
        {
            entry->func(NULL); // 对于查询型命令，不传递任何参数
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

void AT_command_reg(){  // Register the AT commands
    add_command(&commands, "type1=", handle_type_1_command);
    add_command(&commands, "device=", handle_device_command);
    add_command(&commands, "wifi=", handle_wifi_set);   // 处理WiFi设置
    add_command(&commands, "wifi?", handle_wifi_query); // 处理WiFi查询
    add_command(&commands, "eui=", handle_eui_command);
    add_command(&commands, "token=", handle_token);
}


void AT_command_free()
{
    command_entry *current_command, *tmp;
    HASH_ITER(hh, commands, current_command, tmp)
    {
        HASH_DEL(commands, current_command); // Delete the entry from the hash table
        free(current_command);               // Free the memory allocated for the entry
    }
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
    AT_Response response = create_at_response("AT_OK");
    send_at_response(&response);
}

void handle_wifi_query(char *params)
{
    printf("Handling token command\n");
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
    // 事件数据传递
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
    // 事件数据传递完成

    regex_t regex;
    int ret;
    ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret)
    {
        printf("Could not compile regex\n");
    }

    // char *test_strings[] = {
    //         "\rAT+type1?\n",
    //         "\rAT+wifi={\"Ssid\":\"Watcher_Wifi\",\"Password\":\"12345678\"}\n",
    //         "\rAT+type3\n", // Added a test string without parameters
    //         NULL
    // };
    regmatch_t matches[4];
    ret = regexec(&regex, test_strings, 4, matches, 0);
    if (!ret)
    {
        char command_type[20];
        snprintf(command_type, sizeof(command_type), "%.*s",
                 (int)(matches[1].rm_eo - matches[1].rm_so),
                 test_strings + matches[1].rm_so);

        char params[100] = "";
        if (matches[3].rm_so != -1)
        {
            snprintf(params, sizeof(params), "%.*s",
                     (int)(matches[3].rm_eo - matches[3].rm_so),
                     test_strings + matches[3].rm_so);
        }
        char query_type = test_strings[matches[1].rm_eo] == '?' ? '?' : '=';     
        exec_command(&commands, command_type, params,query_type);
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
    vTaskDelay(5000 / portTICK_PERIOD_MS); // delay 1s
}

// 注册事件处理器和创建发送任务的函数
void init_event_loop_and_task(void)
{
    esp_event_loop_args_t loop_args = {
        .queue_size = 20,
        .task_name = "task_AT_command",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 2048 * 2,
        .task_core_id = tskNO_AFFINITY};

    // 创建事件循环
    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &at_event_loop_handle));

    // 注册事件处理器
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
        // 分配足够的内存来存储响应字符串
        response.response = heap_caps_malloc(strlen(message) + 1, MALLOC_CAP_SPIRAM); // +1 for null terminator
        if (response.response)
        {
            strcpy(response.response, message);
            response.length = strlen(message);
        }
        else
        {
            printf("Failed to allocate memory for AT response\n");
            // 处理内存分配失败的情况
            response.response = NULL;
            response.length = 0;
        }
    }
    else
    {
        // 处理空消息输入的情况
        response.response = NULL;
        response.length = 0;
    }
    return response;
}

void AT_cmd_init()
{
    create_AT_response_queue();
    init_AT_response_semaphore();
    init_event_loop_and_task();
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
            printf("Task %s:\n\tState: %u\n\tPriority: %u\n\tStack High Water Mark: %lu\n",
                   pxTaskStatusArray[x].pcTaskName,
                   pxTaskStatusArray[x].eCurrentState,
                   pxTaskStatusArray[x].uxCurrentPriority,
                   pxTaskStatusArray[x].usStackHighWaterMark);
        }

        // wait for 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
#endif