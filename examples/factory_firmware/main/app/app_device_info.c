
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

uint8_t SN[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x10};

SemaphoreHandle_t MUTEX_SN = NULL;

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
                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, &SN, sizeof(SN), portMAX_DELAY);
                xSemaphoreGive(MUTEX_SN);
                break;
            default:
                ESP_LOGI(SN_TAG, "Unknown caller");
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