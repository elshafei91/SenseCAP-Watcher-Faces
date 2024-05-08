
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "data_defs.h"
#include "app_device_info.h"
#include "system_layer.h"

const char *data = "010203040506070809";
SemaphoreHandle_t MUTEX_SN = NULL;
static char* caller int get_sn(int caller)
{
    if (xSemaphoreTake(MUTEX_SN, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "get_sn: MUTEX_SN take failed");
        return -1;
    }
    else
    {
        switch (caller)
        {
            case AT_CMD_CALLER:
                ESP_LOGI(TAG, "AT_CMD get sn");
                return sn;
                break;
            case UI_CALLER:
                ESP_LOGI(TAG, "UI get sn");
                const char *data = "010203040506070809";
                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, &data, sizeof(data), portMAX_DELAY);
                break;
            default:
                ESP_LOGI(TAG, "Unknown caller");
                break;
        }
    }
}
void app_device_info_task(void *pvParameter)
{
    MUTEX_SN = xSemaphoreCreateBinary();
    xSemaphoreGive(MUTEX_SN);
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}