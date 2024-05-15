#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_app_desc.h"

#include "sensecap-watcher.h"

#include "deviceinfo.h"
#include "storage.h"
#include "data_defs.h"
#include "event_loops.h"
#include "app_mqtt_client.h"
#include "util.h"


static const char *TAG = "deviceinfo";

static TaskHandle_t g_task;
static StaticTask_t g_task_tcb;

#define DEVICEINFO_STORAGE  "deviceinfo"

static struct view_data_device_status g_device_status;
static SemaphoreHandle_t g_sem_mqttconn;

int deviceinfo_get(struct view_data_deviceinfo *p_info)
{
    size_t len=sizeof(struct view_data_deviceinfo);
    memset(p_info, 0, len);
    esp_err_t ret = storage_read(DEVICEINFO_STORAGE, (void *)p_info, &len);
    if (ret != ESP_OK) {
        return ret;
	}
    return ESP_OK;
}

int deviceinfo_set(struct view_data_deviceinfo *p_info)
{
    esp_err_t ret = 0;
    ret = storage_write(DEVICEINFO_STORAGE, (void *)p_info, sizeof(struct view_data_deviceinfo));
    if( ret != ESP_OK ) {
        ESP_LOGE("", "cfg write err:%d", ret);
        return ret;
    }
    return ESP_OK;
}

static void __deviceinfo_task(void *p_arg)
{
    uint8_t batnow;

    xSemaphoreTake(g_sem_mqttconn, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(10000));  //postpone a bit for other more important routines

    //mqtt connected implies time has been synced
    //send once after boot
    g_device_status.battery_per = bsp_battery_get_percent();

    //mqtt pub
    app_mqtt_client_report_device_status(&g_device_status);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        batnow = bsp_battery_get_percent();
        if (g_device_status.battery_per - batnow > 10 || batnow == 0) {
            g_device_status.battery_per = batnow;
            //mqtt pub
            app_mqtt_client_report_device_status(&g_device_status);
        }
    }
}

static void __event_loop_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    case CTRL_EVENT_MQTT_CONNECTED:
    {
        ESP_LOGI(TAG, "received event: CTRL_EVENT_MQTT_CONNECTED");

        xSemaphoreGive(g_sem_mqttconn);

        break;
    }
    default:
        break;
    }
}

esp_err_t app_device_status_monitor_init(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();

    g_device_status.hw_version = "1.0";
    g_device_status.fw_version = app_desc->version;
    g_device_status.battery_per = 100;

    g_sem_mqttconn = xSemaphoreCreateBinary();

    // xTaskCreate(__deviceinfo_task, "deviceinfo_task", 1024 * 3, NULL, 1, NULL);

    const uint32_t stack_size = 2 * 1024 + 256;
    StackType_t *task_stack = (StackType_t *)psram_alloc(stack_size);
    g_task = xTaskCreateStatic(__deviceinfo_task, "deviceinfo", stack_size, NULL, 1, task_stack, &g_task_tcb);

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(ctrl_event_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_CONNECTED,
                                                            __event_loop_handler, NULL, NULL));

    return ESP_OK;
}
