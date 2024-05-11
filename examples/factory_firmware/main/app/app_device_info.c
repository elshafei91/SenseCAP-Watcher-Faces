
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_event.h"
#include "event_loops.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "data_defs.h"
#include "app_device_info.h"
#include "system_layer.h"
#define SN_TAG                    "SN_TAG"
#define APP_DEVICE_INFO_MAX_STACK 4096

uint8_t SN[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
uint8_t EUI[] = { 0x2C, 0xF7, 0xF1, 0xC2, 0x44, 0x81, 0x00, 0x47, 0xB0, 0x47, 0xD1, 0xD5, 0x8B, 0xC7, 0xF8, 0xFB };
int server_code = 1;
int create_batch = 1000205;
SemaphoreHandle_t MUTEX_SN = NULL;

void byteArrayToHexString(const uint8_t *byteArray, size_t byteArraySize, char *hexString)
{
    for (size_t i = 0; i < byteArraySize; ++i)
    {
        sprintf(&hexString[2 * i], "%02X", byteArray[i]);
    }
}

uint8_t *get_sn(int caller)
{
    uint8_t *result = NULL;
    if (xSemaphoreTake(MUTEX_SN, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("SN_TAG", "get_sn: MUTEX_SN take failed");
        return NULL;
    }
    else
    {
        switch (caller)
        {
            case BLE_CALLER:
                ESP_LOGI(SN_TAG, "BLE get sn");
                result = SN;
                xSemaphoreGive(MUTEX_SN);
                break;
            case UI_CALLER:
                ESP_LOGI(SN_TAG, "UI get sn");
                char storage_space_2[10];
                char storage_space_3[20];
                char storage_space_4[19]; // 18 chars + null terminator
                snprintf(storage_space_2, sizeof(storage_space_2), "%d", server_code);
                snprintf(storage_space_3, sizeof(storage_space_3), "%d", create_batch);
                char hexString1[33] = { 0 }; // 32 chars + null terminator for data1
                char hexString4[19] = { 0 }; // 18 chars + null terminator for data4
                byteArrayToHexString(EUI, sizeof(EUI), hexString1);
                byteArrayToHexString(SN, sizeof(SN), hexString4);
                hexString1[32] = '\0';
                hexString4[18] = '\0';
                char final_string[150];
                snprintf(final_string, sizeof(final_string), "w1:%s:%s:%s:%s", hexString1, storage_space_2, storage_space_3, hexString4);
                printf("SN: %s\n", final_string);
                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, &final_string, sizeof(final_string), portMAX_DELAY);
                xSemaphoreGive(MUTEX_SN);
                break;
        }
        return result;
    }
}

void app_device_info_task(void *pvParameter)
{
    MUTEX_SN = xSemaphoreCreateBinary();
    xSemaphoreGive(MUTEX_SN);
    while (1)
    {
        // efuse read function
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_device_info_init()
{
    // StaticTask_t app_device_info_layer_task_buffer;
    // StackType_t *app_device_info_layer_stack_buffer = heap_caps_malloc(APP_DEVICE_INFO_MAX_STACK * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    // xTaskCreateStatic(&app_device_info_task,
    //     "app_device_info_task",             // wifi_config_layer
    //     APP_DEVICE_INFO_MAX_STACK,    // 1024*5
    //     NULL,                            // NULL
    //     4,                               // 10
    //     app_device_info_layer_stack_buffer,  // wifi_config_layer_stack_buffer
    //     &app_device_info_layer_task_buffer); // wifi_config_layer_task_buffer
    xTaskCreate(&app_device_info_task, "app_device_info_task", 4096, NULL, 5, NULL);
}