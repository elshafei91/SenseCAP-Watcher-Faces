#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "mqtt_client.h"


#include "app_mqtt_client.h"
#include "event_loops.h"
#include "data_defs.h"
#include "deviceinfo.h"
#include "util.h"
#include "uuid.h"


static const char *TAG = "mqtt-client";
const int MQTT_PUB_QOS = 0;

static struct view_data_deviceinfo g_deviceinfo;
static SemaphoreHandle_t g_sem_mqttinfo;
static SemaphoreHandle_t g_sem_taskpub_ack;
static struct view_data_mqtt_connect_info *g_mqttinfo;
static esp_mqtt_client_handle_t g_mqtt_client;
static esp_mqtt_client_config_t g_mqtt_cfg;

static char g_mqtt_broker_uri[144];
static char g_mqtt_client_id[32];
static char g_mqtt_password[171];

static char g_topic_down_task_publish[64];
static char g_topic_up_task_publish_ack[64];
static char g_topic_up_task_status_change[70];
static char g_topic_up_warn_event_report[64];

static struct ctrl_data_mqtt_tasklist_cjson g_ctrl_data_mqtt_tasklist_cjson;


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/**
 * This func is called by __mqtt_event_handler, i.e. under the context of MQTT task 
 * (the MQTT task is within the esp-mqtt component)
 * It's OK to process a MQTT msg with max length = 2048 (2048 is inbox buffer size of
 * MQTT compoent, configured with menuconfig)
*/
static void __parse_mqtt_tasklist(char *mqtt_msg_buff, int msg_buff_len)
{
    static struct ctrl_data_mqtt_tasklist_cjson *p_tasklist_cjson = &g_ctrl_data_mqtt_tasklist_cjson;

    ESP_LOGI(TAG, "start to parse tasklist from MQTT msg ...");
    ESP_LOGD(TAG, "MQTT msg: \r\n %.*s\r\n", msg_buff_len, mqtt_msg_buff);
    
    cJSON *tmp_cjson = cJSON_Parse(mqtt_msg_buff);

    if (tmp_cjson == NULL) {
        ESP_LOGE(TAG, "failed to parse cJSON object for MQTT msg:");
        ESP_LOGE(TAG, "%.*s\r\n", msg_buff_len, mqtt_msg_buff);
        return;
    }

    xSemaphoreTake(g_ctrl_data_mqtt_tasklist_cjson.mutex, portMAX_DELAY);
    if (g_ctrl_data_mqtt_tasklist_cjson.tasklist_cjson != NULL) {
        cJSON_Delete(g_ctrl_data_mqtt_tasklist_cjson.tasklist_cjson);
    }
    g_ctrl_data_mqtt_tasklist_cjson.tasklist_cjson = tmp_cjson;
    xSemaphoreGive(g_ctrl_data_mqtt_tasklist_cjson.mutex);

    ESP_LOGD(TAG, "PTR: 0x%" PRIx32, (uint32_t)p_tasklist_cjson);
    
    esp_event_post_to(ctrl_event_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_TASKLIST_JSON, 
                                    &p_tasklist_cjson,
                                    sizeof(void *), /* ptr size */
                                    portMAX_DELAY);
    xSemaphoreGive(g_sem_taskpub_ack);
}

static void __mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    static char tmp_topic_holder[80];

    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");

        msg_id = esp_mqtt_client_subscribe(client, g_topic_down_task_publish, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d, topic=%s", msg_id, g_topic_down_task_publish);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        // ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        if (event->current_data_offset != 0) {
            ESP_LOGW(TAG, "incoming msg too big, total_data_len=%d", event->total_data_len);
            break;
        }
        //printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        //printf("DATA=%.*s\r\n", event->data_len, event->data);
        ESP_LOGI(TAG, "topic=%.*s", event->topic_len, event->topic);

        memcpy(tmp_topic_holder, event->topic, event->topic_len);
        tmp_topic_holder[event->topic_len] = '\0';
        if (strstr(tmp_topic_holder, "task-publish")) {
            __parse_mqtt_tasklist(event->data, event->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void __app_mqtt_client_task(void *p_arg)
{
    ESP_LOGI(TAG, "starting app_mqtt_client task ...");

    bool mqtt_client_inited = false;
    

    if (deviceinfo_get(&g_deviceinfo) != ESP_OK) {
        ESP_LOGE(TAG, "deviceinfo_get failed!");
        vTaskDelay(portMAX_DELAY);
    }

    sniprintf(g_topic_down_task_publish, sizeof(g_topic_down_task_publish), 
                "iot/ipnode/%s/get/order/task-publish", g_deviceinfo.eui);
    sniprintf(g_topic_up_task_publish_ack, sizeof(g_topic_up_task_publish_ack), 
                "iot/ipnode/%s/update/order/task-publish-ack", g_deviceinfo.eui);
    sniprintf(g_topic_up_task_status_change, sizeof(g_topic_up_task_status_change), 
                "iot/ipnode/%s/update/event/change-device-status", g_deviceinfo.eui);
    sniprintf(g_topic_up_warn_event_report, sizeof(g_topic_up_warn_event_report), 
                "iot/ipnode/%s/update/event/measure-sensor", g_deviceinfo.eui);

    
    while (1)
    {
        if (xSemaphoreTake(g_sem_mqttinfo, pdMS_TO_TICKS(1000)) == pdPASS) {
            //mqtt connect info changed, copy into here
            xSemaphoreTake(g_mqttinfo->mutex, portMAX_DELAY);
            snprintf(g_mqtt_broker_uri, sizeof(g_mqtt_broker_uri), "mqtt://%s:%d", g_mqttinfo->serverUrl, g_mqttinfo->mqttPort);
            snprintf(g_mqtt_client_id, sizeof(g_mqtt_client_id), "device-6p-%s", g_deviceinfo.eui);
            if (!mqtt_client_inited) {
            }
            memcpy(g_mqtt_password, g_mqttinfo->token, sizeof(g_mqttinfo->token));
            xSemaphoreGive(g_mqttinfo->mutex);

            ESP_LOGI(TAG, "mqtt connect info changed, uri: %s", g_mqtt_broker_uri);

            g_mqtt_cfg.broker.address.uri = g_mqtt_broker_uri;
            g_mqtt_cfg.credentials.username = g_mqtt_client_id;
            g_mqtt_cfg.credentials.client_id = g_mqtt_client_id;
            g_mqtt_cfg.credentials.authentication.password = g_mqtt_password;
            g_mqtt_cfg.session.disable_clean_session = true;

            //first time?
            if (!mqtt_client_inited) {
                g_mqtt_client = esp_mqtt_client_init(&g_mqtt_cfg);
                esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, __mqtt_event_handler, NULL);
                esp_mqtt_client_start(g_mqtt_client);
                mqtt_client_inited = true;
                ESP_LOGI(TAG, "mqtt client started!");
            } else {
                esp_mqtt_set_config(g_mqtt_client, &g_mqtt_cfg);
                esp_mqtt_client_reconnect(g_mqtt_client);
                ESP_LOGI(TAG, "mqtt client start reconnecting ...");
            }
        }
        if (xSemaphoreTake(g_sem_taskpub_ack, pdMS_TO_TICKS(1)) == pdPASS) {
            
        }
    } 
}

static void __view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    //MQTT connection info changed
    case VIEW_EVENT_MQTT_CONNECT_INFO:
    {
        ESP_LOGI(TAG, "received event: VIEW_EVENT_MQTT_CONNECT_INFO");

        g_mqttinfo = *(struct view_data_mqtt_connect_info **)event_data;

        xSemaphoreGive(g_sem_mqttinfo);  //just forward the change, don't care the life cycle of the data

        break;
    }
    default:
        break;
    }
}

esp_err_t app_mqtt_client_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    g_sem_mqttinfo = xSemaphoreCreateBinary();
    g_sem_taskpub_ack = xSemaphoreCreateBinary();
    g_ctrl_data_mqtt_tasklist_cjson.mutex = xSemaphoreCreateMutex();
    g_ctrl_data_mqtt_tasklist_cjson.tasklist_cjson = NULL;

    //ESP_ERROR_CHECK(esp_event_loop_create_default());  //already done in app_wifi.c


    xTaskCreate(__app_mqtt_client_task, "app_mqtt_client_task", 1024 * 4, NULL, 4, NULL);

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_MQTT_CONNECT_INFO,
                                                            __view_event_handler, NULL, NULL));

    return ESP_OK;
}

esp_err_t app_mqtt_client_report_tasklist_ack(char *request_id, cJSON *task_settings_node)
{
    int ret = ESP_OK;

    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %d,"
        "\"intent\": \"order\","
        "\"type\": \"response\","
        "\"deviceEui\": \"%s\","
        "\"order\":  ["
            "{"
                "\"name\": \"task-publish-ack\","
                "\"value\": {"
                    "\"code\":0,"
                    "\"data\": {"
                        "\"taskSettings\": %s"
                    "}"
                "}"
            "}"
        "]"
    "}";

    ESP_RETURN_ON_FALSE(g_mqtt_client, ESP_FAIL, TAG, "g_mqtt_client is not inited yet");
    
    char *json_buff = malloc(2048);
    char *task_settings_str = cJSON_Print(task_settings_node);
    int timestamp_ms = util_get_timestamp_ms();

    sniprintf(json_buff, sizeof(json_buff), json_fmt, request_id, timestamp_ms, g_deviceinfo.eui, task_settings_str);

    ESP_LOGD(TAG, "app_mqtt_client_report_tasklist_ack: \r\n%s\r\n", json_buff);

    int msg_id = esp_mqtt_client_enqueue(g_mqtt_client, g_topic_up_task_publish_ack, json_buff, strlen(json_buff),
                                        MQTT_PUB_QOS, false/*retain*/, true/*store*/);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_mqtt_client_report_tasklist_ack enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }

    free(task_settings_str);
    free(json_buff);

    return ret;
}

esp_err_t app_mqtt_client_report_tasklist_status(int tasklist_id, int tasklist_status_num)
{
    int ret = ESP_OK;

    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %d,"
        "\"intent\": \"event\","
        "\"deviceEui\": \"%s\","
        "\"events\":  [{"
            "\"name\": \"change-device-status\","
            "\"value\": {"
                "\"3968\": ["
                    "{"
                        "\"tlid\": %d,"
                        "\"status\": %d"
                    "}"
                "]"
            "},"
            "\"timestamp\": %d,"
        "}]"
    "}";

    ESP_RETURN_ON_FALSE(g_mqtt_client, ESP_FAIL, TAG, "g_mqtt_client is not inited yet [2]");
    
    char *json_buff = malloc(2048);
    char uuid[37];
    int timestamp_ms = util_get_timestamp_ms();

    UUIDGen(uuid);
    sniprintf(json_buff, sizeof(json_buff), json_fmt, uuid, timestamp_ms, g_deviceinfo.eui, tasklist_id, tasklist_status_num,
              timestamp_ms);

    ESP_LOGD(TAG, "app_mqtt_client_report_tasklist_ack: \r\n%s\r\n", json_buff);

    int msg_id = esp_mqtt_client_enqueue(g_mqtt_client, g_topic_up_task_status_change, json_buff, strlen(json_buff),
                                        MQTT_PUB_QOS, false/*retain*/, true/*store*/);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_mqtt_client_report_tasklist_status enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }

    free(json_buff);

    return ret;
}

esp_err_t app_mqtt_client_report_warn_event(int tasklist_id, char *tasklist_name, int warn_type)
{
    int ret = ESP_OK;

    const char *json_fmt =  \
    "{"
        "\"requestId\": \"%s\","
        "\"timestamp\": %d,"
        "\"intent\": \"event\","
        "\"deviceEui\": \"%s\","
        "\"deviceKey\": \"%s\","
        "\"events\":  [{"
            "\"name\": \"measure-sensor\","
            "\"value\": [{"
                "\"channel\": 1,"
                "\"measurements\": {"
                    "\"5004\": ["
                        "{"
                            "\"tlid\": %d,"
                            "\"tn\": \"%s\","
                            "\"warnType\": %d"
                        "}"
                    "]"
                "},"
                "\"measureTime\": %d"
            "}]"
        "}]"
    "}";

    ESP_RETURN_ON_FALSE(g_mqtt_client, ESP_FAIL, TAG, "g_mqtt_client is not inited yet [3]");
    
    char *json_buff = malloc(2048);
    char uuid[37];
    int timestamp_ms = util_get_timestamp_ms();

    UUIDGen(uuid);
    sniprintf(json_buff, sizeof(json_buff), json_fmt, uuid, timestamp_ms, g_deviceinfo.eui, g_deviceinfo.key,
              tasklist_id, tasklist_name, warn_type, timestamp_ms);

    ESP_LOGD(TAG, "app_mqtt_client_report_tasklist_ack: \r\n%s\r\n", json_buff);

    int msg_id = esp_mqtt_client_enqueue(g_mqtt_client, g_topic_up_task_status_change, json_buff, strlen(json_buff),
                                        MQTT_PUB_QOS, false/*retain*/, true/*store*/);

    if (msg_id < 0) {
        ESP_LOGW(TAG, "app_mqtt_client_report_warn_event enqueue failed, err=%d", msg_id);
        ret = ESP_FAIL;
    }

    free(json_buff);

    return ret;
}
