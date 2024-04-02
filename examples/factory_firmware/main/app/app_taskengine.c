// #include "cJSON.h"

// #include "app_taskengine.h"


// static const char *TAG = "taskengine";

// static SemaphoreHandle_t g_sem_tasklist_cjson;

// static void __app_taskengine_task(void *p_arg)
// {

// }

// static void __ctrl_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
// {
//     switch (id)
//     {
//     case CTRL_EVENT_MQTT_TASKLIST_JSON:
//     {
//         ESP_LOGI(TAG, "received event: CTRL_EVENT_MQTT_TASKLIST_JSON");

//         g_mqttinfo = *(struct view_data_mqtt_connect_info **)event_data;

//         xSemaphoreGive(g_sem_mqttinfo);  //just forward the change, don't care the life cycle of the data

//         break;
//     }
//     default:
//         break;
//     }
// }

// esp_err_t app_taskengine_init(void)
// {
// #if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
//     esp_log_level_set(TAG, ESP_LOG_DEBUG);
// #endif

//     g_sem_tasklist_cjson = xSemaphoreCreateBinary();

//     xTaskCreate(__app_taskengine_task, "app_taskengine_task", 1024 * 4, NULL, 4, NULL);

//     ESP_ERROR_CHECK(esp_event_handler_instance_register_with(ctrl_event_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_TASKLIST_JSON,
//                                                             __ctrl_event_handler, NULL, NULL));

//     return ESP_OK;
// }

// esp_err_t app_taskengine_register_task_executor(void *something)
// {
//     return ESP_OK;
// }