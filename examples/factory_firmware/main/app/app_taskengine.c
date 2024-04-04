#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_check.h"
#include "cJSON.h"

#include "app_taskengine.h"
#include "data_defs.h"
#include "event_loops.h"
#include "app_mqtt_client.h"
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
// static SemaphoreHandle_t g_sem_tasklist_cjson;
static SemaphoreHandle_t g_mtx_tasklist_cjson;
static cJSON *g_tasklist_cjson;
static struct ctrl_data_taskinfo7 g_ctrl_data_taskinfo_7;  // this is garbage
static struct ctrl_data_mqtt_tasklist_cjson *g_ctrl_data_mqtt_tasklist_cjson;
static char g_tasklist_reqid[40];
static intmax_t g_current_running_tlid;

static esp_err_t __validate_incoming_tasklist_cjson(cJSON *tl_cjson)
{
    esp_err_t ret = ESP_OK;

    cJSON *json_root = tl_cjson;
    cJSON *json_requestId = cJSON_GetObjectItem(json_root, "requestId");
    ESP_GOTO_ON_FALSE(json_requestId != NULL, ESP_FAIL, err, TAG, 
                      "incoming tasklist cjson invalid: no requestId field!");
    cJSON *json_order = cJSON_GetObjectItem(json_root, "order");
    ESP_GOTO_ON_FALSE(json_order != NULL, ESP_FAIL, err, TAG, 
                      "incoming tasklist cjson invalid: no order field!");

    cJSON *json_order0 = cJSON_GetArrayItem(json_order, 0);
    ESP_GOTO_ON_FALSE(json_order0 != NULL, ESP_FAIL, err, TAG, 
                      "incoming tasklist cjson invalid: order field is empty array!");

    cJSON *json_order0_name = cJSON_GetObjectItem(json_order0, "name");
    ESP_GOTO_ON_FALSE(json_order0_name != NULL, ESP_FAIL, err, TAG, 
                      "incoming tasklist cjson invalid: order has no name field!");
    char *order_name = json_order0_name->valuestring;
    ESP_GOTO_ON_FALSE(strcmp(order_name, "task-publish") == 0, ESP_FAIL, err, TAG, 
                      "incoming tasklist cjson invalid: order name is not task-publish!");


    //TODO
err:    
    return ret;
}

static void __app_taskengine_task(void *p_arg)
{
    cJSON *tasklist_cjson_from_flash = NULL;
    uint32_t tasklist_exist = 0;
    static struct ctrl_data_taskinfo7 *p_ctrl_data_taskinfo_7 = &g_ctrl_data_taskinfo_7;

    vTaskDelay(pdMS_TO_TICKS(2000));

    while (1)
    {
        switch (g_taskengine_sm)
        {
        case TE_SM_LOAD_STORAGE_TL:
            char *json_buff = malloc(2048);
            size_t json_str_len = 2048;
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
                xSemaphoreTake(g_mtx_tasklist_cjson, portMAX_DELAY);
                g_tasklist_cjson = tasklist_cjson_from_flash;
                xSemaphoreGive(g_mtx_tasklist_cjson);
                g_taskengine_sm = TE_SM_TRANSLATE_TL;
            } else {
                ESP_LOGW(TAG, "loaded tasklist is not valid JSON, ignore ...");
                g_taskengine_sm = TE_SM_WAIT_TL;
            }
            break;

        case TE_SM_WAIT_TL:
            ESP_LOGI(TAG, "state: TE_SM_WAIT_TL");
            // tell UI no tasklist running
            tasklist_exist = 0;
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TASKLIST_EXIST, 
                                &tasklist_exist,
                                4, /* uint32_t size */
                                portMAX_DELAY);
            g_current_running_tlid = 0;
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  //task sleep
            break;

        case TE_SM_TRANSLATE_TL:
            ESP_LOGI(TAG, "state: TE_SM_TRANSLATE_TL");

            bool using_flash_tasklist = g_tasklist_cjson == tasklist_cjson_from_flash;

            if (g_tasklist_cjson != tasklist_cjson_from_flash && tasklist_cjson_from_flash != NULL) {
                cJSON_Delete(tasklist_cjson_from_flash);
                tasklist_cjson_from_flash = NULL;
            }
            // TODO: this is temp solution for EW
            xSemaphoreTake(g_mtx_tasklist_cjson, portMAX_DELAY);
            cJSON *node = g_tasklist_cjson;
            cJSON *item, *item2;
            cJSON *found = NULL;
            int array_len, i;
            if ((node = cJSON_GetObjectItem(node, "order"))) {
                if ((node = cJSON_GetArrayItem(node, 0))) {
                    if ((node = cJSON_GetObjectItem(node, "value"))) {
                        if ((node = cJSON_GetObjectItem(node, "taskSettings"))) {
                            if ((node = cJSON_GetArrayItem(node, 0))) {
                                if (cJSON_GetObjectItem(node, "tlid")) {
                                    g_current_running_tlid = (intmax_t)(cJSON_GetObjectItem(node, "tlid")->valuedouble);
                                }
                                if ((node = cJSON_GetObjectItem(node, "tl"))) {
                                    array_len = cJSON_GetArraySize(node);
                                    ESP_LOGD(TAG, "array_len=%d", array_len);
                                    for (i = 0; i < array_len; i++) {
                                        if ((item = cJSON_GetArrayItem(node, i))) {
                                            if ((item = cJSON_GetObjectItem(item, "tl"))) {
                                                array_len = cJSON_GetArraySize(item);
                                                ESP_LOGD(TAG, "array_len=%d", array_len);
                                                for (i = 0; i < array_len; i++)
                                                {
                                                    item2 = cJSON_GetArrayItem(item, i);
                                                    if (cJSON_GetObjectItem(item2, "tid")->valueint == 7) {
                                                        ESP_LOGI(TAG, "found taskid 7");
                                                        found = cJSON_Duplicate(item2, 1);
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            xSemaphoreGive(g_mtx_tasklist_cjson);

            if (found != NULL) {
                xSemaphoreTake(g_ctrl_data_taskinfo_7.mutex, portMAX_DELAY);
                if (g_ctrl_data_taskinfo_7.task7 != NULL) {
                    cJSON_Delete(g_ctrl_data_taskinfo_7.task7);
                }
                g_ctrl_data_taskinfo_7.task7 = found;
                g_ctrl_data_taskinfo_7.no_task7 = false;
                xSemaphoreGive(g_ctrl_data_taskinfo_7.mutex);

                esp_event_post_to(ctrl_event_handle, CTRL_EVENT_BASE, CTRL_EVENT_BROADCAST_TASK7, 
                                    &p_ctrl_data_taskinfo_7,
                                    sizeof(void *), /* ptr size */
                                    portMAX_DELAY);
            } else {
                xSemaphoreTake(g_ctrl_data_taskinfo_7.mutex, portMAX_DELAY);
                g_ctrl_data_taskinfo_7.no_task7 = true;
                xSemaphoreGive(g_ctrl_data_taskinfo_7.mutex);
                ESP_LOGI(TAG, "no task7, do local warn");
                esp_event_post_to(ctrl_event_handle, CTRL_EVENT_BASE, CTRL_EVENT_BROADCAST_TASK7, 
                                    &p_ctrl_data_taskinfo_7,
                                    sizeof(void *), /* ptr size */
                                    portMAX_DELAY);
            }

            if (!using_flash_tasklist) {
                // sidejob: store tasklist into flash
                xSemaphoreTake(g_mtx_tasklist_cjson, portMAX_DELAY);
                char *json_buff1 = cJSON_Print(g_tasklist_cjson);
                xSemaphoreGive(g_mtx_tasklist_cjson);

                esp_err_t ret = storage_write("tasklist_json", json_buff1, strlen(json_buff1));
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "tasklist json is saved into flash.");
                } else {
                    ESP_LOGI(TAG, "tasklist json failed to be saved into flash.");
                }
                free(json_buff1);
                

                // valid tasklist, task ack to MQTT
                xSemaphoreTake(g_mtx_tasklist_cjson, portMAX_DELAY);
                cJSON *json_root = g_tasklist_cjson;
                do {
                    cJSON *json_requestId = cJSON_GetObjectItem(json_root, "requestId");
                    if (json_requestId == NULL) break;
                    char *request_id = json_requestId->valuestring;

                    cJSON *json_order = cJSON_GetObjectItem(json_root, "order");
                    if (json_order == NULL) break;

                    cJSON *json_order0 = cJSON_GetArrayItem(json_order, 0);
                    if (json_order0 == NULL) break;

                    cJSON *json_order_value = cJSON_GetObjectItem(json_order0, "value");
                    if (json_order_value == NULL) break;

                    cJSON *json_order_value_taskSettings = cJSON_GetObjectItem(json_order_value, "taskSettings");
                    if (json_order_value_taskSettings == NULL) break;

                    app_mqtt_client_report_tasklist_ack(request_id, json_order_value_taskSettings);
                } while (0);
                xSemaphoreGive(g_mtx_tasklist_cjson);
            }

            // translate done, jump state
            g_taskengine_sm = TE_SM_TL_RUN;

            break;

        case TE_SM_TL_RUN:
            ESP_LOGI(TAG, "state: TE_SM_TL_RUN");

            // compare the requestId
            xSemaphoreTake(g_mtx_tasklist_cjson, portMAX_DELAY);
            cJSON *json_root1 = g_tasklist_cjson;
            cJSON *json_requestId1 = cJSON_GetObjectItem(json_root1, "requestId");
            memcpy(g_tasklist_reqid, json_requestId1->valuestring, strlen(json_requestId1->valuestring));
            xSemaphoreGive(g_mtx_tasklist_cjson);

            // TODO: make the tasklist run

            // tell UI tasklist running
            tasklist_exist = 1;
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_TASKLIST_EXIST, 
                                &tasklist_exist,
                                4, /* uint32_t size */
                                portMAX_DELAY);

            ESP_LOGI(TAG, "#### Now the tasklist is running ... ####");
            ESP_LOGI(TAG, "tasklist requestId=%s", g_tasklist_reqid);
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

        g_ctrl_data_mqtt_tasklist_cjson = *(struct ctrl_data_mqtt_tasklist_cjson **)event_data;
        g_mtx_tasklist_cjson = g_ctrl_data_mqtt_tasklist_cjson->mutex;

        xSemaphoreTake(g_mtx_tasklist_cjson, portMAX_DELAY);
        cJSON *tmp_cjson = g_ctrl_data_mqtt_tasklist_cjson->tasklist_cjson;
        if (__validate_incoming_tasklist_cjson(tmp_cjson) != ESP_OK) {
            ESP_LOGW(TAG, "incoming tasklist cjson is not valid, ignore ...");
            xSemaphoreGive(g_mtx_tasklist_cjson);
            break;
        }
        cJSON *json_requestId = cJSON_GetObjectItem(tmp_cjson, "requestId");
        if (strcmp(json_requestId->valuestring, g_tasklist_reqid) == 0) {
            ESP_LOGW(TAG, "incoming tasklist is the same as running, ignore ...");
            xSemaphoreGive(g_mtx_tasklist_cjson);
            break;
        }
        g_tasklist_cjson = tmp_cjson;
        xSemaphoreGive(g_mtx_tasklist_cjson);

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
    g_mtx_tasklist_cjson = xSemaphoreCreateMutex();
    g_taskengine_sm = TE_SM_LOAD_STORAGE_TL;
    memset(g_tasklist_reqid, 0, sizeof(g_tasklist_reqid));
    g_ctrl_data_taskinfo_7.mutex = xSemaphoreCreateMutex();  // this is garbage
    g_ctrl_data_taskinfo_7.task7 = NULL;
    g_ctrl_data_taskinfo_7.no_task7 = true;

    g_current_running_tlid = 0;

    xTaskCreate(__app_taskengine_task, "app_taskengine_task", 1024 * 4, NULL, 4, &g_task);

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(ctrl_event_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_TASKLIST_JSON,
                                                            __ctrl_event_handler, NULL, NULL));

    return ESP_OK;
}

esp_err_t app_taskengine_register_task_executor(void *something)
{
    return ESP_OK;
}

intmax_t app_taskengine_get_current_tlid(void)
{
    return g_current_running_tlid;
}