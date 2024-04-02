#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_check.h"
#include "cJSON.h"

#include "app_taskengine.h"
#include "data_defs.h"
#include "event_loops.h"
#include "storage.h"


enum taskengine_state_machine {
    TE_SM_LOAD_STORAGE_TL = 0,
    TE_SM_WAIT_TL,              //no tasklist is stored in flash
    TE_SM_TRANSLATE_TL,         //translate tasklist cjson to internal tasklist structure
    TE_SM_TL_RUN,
    TE_SM_TL_STOP,
    TE_SM_NUM
};


static const char *TAG = "taskengine";

static TaskHandle_t g_task = NULL;
static uint8_t g_taskengine_sm;  //state machine for taskengine
static SemaphoreHandle_t g_sem_tasklist_cjson;
static SemaphoreHandle_t g_mutex_tasklist_cjson;
static cJSON *g_tasklist_cjson;

static esp_err_t __validate_incoming_tasklist_cjson(cJSON *tl_cjson)
{
    return ESP_OK;
}

static void __app_taskengine_task(void *p_arg)
{
    cJSON *tasklist_cjson_from_flash = NULL;

    while (1)
    {
        switch (g_taskengine_sm)
        {
        case TE_SM_LOAD_STORAGE_TL:
            char *json_buff = malloc(2048);
            size_t json_str_len;
            if (storage_read("tasklist_json", json_buff, &json_str_len) != ESP_OK) {
                ESP_LOGE(TAG, "failed to read tasklist from flash!");
                free(json_buff);
                //jump state
                g_taskengine_sm = TE_SM_WAIT_TL;
                break;
            }
            ESP_LOGI(TAG, "loaded tasklist from flash, str_len=%d", json_str_len);
            tasklist_cjson_from_flash = cJSON_Parse(json_buff);
            free(json_buff);
            if (tasklist_cjson_from_flash != NULL) {
                if (__validate_incoming_tasklist_cjson(tasklist_cjson_from_flash) != ESP_OK) {
                    ESP_LOGW(TAG, "restored tasklist cjson is not valid, maybe outdated, ignore ...");
                    g_taskengine_sm = TE_SM_WAIT_TL;
                    break;
                }
                xSemaphoreTake(g_mutex_tasklist_cjson, portMAX_DELAY);
                g_tasklist_cjson = tasklist_cjson_from_flash;
                xSemaphoreGive(g_mutex_tasklist_cjson);
                g_taskengine_sm = TE_SM_TRANSLATE_TL;
            } else {
                ESP_LOGW(TAG, "loaded tasklist is not valid JSON, ignore ...");
                g_taskengine_sm = TE_SM_WAIT_TL;
            }
            break;

        case TE_SM_WAIT_TL:
            ESP_LOGI(TAG, "state: TE_SM_WAIT_TL");
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  //task sleep
            break;

        case TE_SM_TRANSLATE_TL:
            ESP_LOGI(TAG, "state: TE_SM_TRANSLATE_TL");

            if (g_tasklist_cjson != tasklist_cjson_from_flash && tasklist_cjson_from_flash != NULL) {
                cJSON_Delete(tasklist_cjson_from_flash);
                tasklist_cjson_from_flash = NULL;
            }
            // TODO: this is temp solution for EW


            // sidejob: store tasklist into flash
            char *json_buff1 = malloc(2048);
            xSemaphoreTake(g_mutex_tasklist_cjson, portMAX_DELAY);
            json_buff1 = cJSON_Print(g_tasklist_cjson);
            xSemaphoreGive(g_mutex_tasklist_cjson);

            storage_write("tasklist_json", json_buff1, strlen(json_buff1));
            free(json_buff1);
            ESP_LOGI(TAG, "tasklist json is saved into flash.");

            // translate done, jump state
            g_taskengine_sm = TE_SM_TL_RUN;

            break;

        case TE_SM_TL_RUN:
            ESP_LOGI(TAG, "state: TE_SM_TL_RUN");
            // TODO
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  //task sleep

            //cleanup?
            if (g_taskengine_sm != TE_SM_TL_RUN) {
                //TODO: cleanup job here
            }
            break;

        case TE_SM_TL_STOP:
            // TODO: 
            g_taskengine_sm = TE_SM_WAIT_TL;
            break;
        
        default:
            break;
        }
    }
}

static void __ctrl_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    case CTRL_EVENT_MQTT_TASKLIST_JSON:
    {
        ESP_LOGI(TAG, "received event: CTRL_EVENT_MQTT_TASKLIST_JSON");

        cJSON *tmp_cjson = *(cJSON **)event_data;
        if (__validate_incoming_tasklist_cjson(tmp_cjson) != ESP_OK) {
            ESP_LOGW(TAG, "incoming tasklist cjson is not valid, ignore ...");
            break;
        }

        xSemaphoreTake(g_mutex_tasklist_cjson, portMAX_DELAY);
        g_tasklist_cjson = tmp_cjson;
        xSemaphoreGive(g_mutex_tasklist_cjson);

        g_taskengine_sm = TE_SM_TRANSLATE_TL;
        xTaskNotifyGive(g_task);  // wakeup the task

        break;
    }
    default:
        break;
    }
}

esp_err_t app_taskengine_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    // g_sem_tasklist_cjson = xSemaphoreCreateBinary();
    g_mutex_tasklist_cjson = xSemaphoreCreateMutex();
    g_taskengine_sm = TE_SM_LOAD_STORAGE_TL;

    xTaskCreate(__app_taskengine_task, "app_taskengine_task", 1024 * 4, NULL, 4, &g_task);

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(ctrl_event_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_TASKLIST_JSON,
                                                            __ctrl_event_handler, NULL, NULL));

    return ESP_OK;
}

esp_err_t app_taskengine_register_task_executor(void *something)
{
    return ESP_OK;
}