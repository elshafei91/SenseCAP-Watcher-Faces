#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"
#include "data_defs.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ENABLE_TEST_ENV
#define SENSECAP_URL "http://intranet-sensecap-env-expose-publicdns.seeed.cc"
#else
#define SENSECAP_URL "https://sensecap.seeed.cc"
#endif

#define SENSECAP_PATH_TOKEN_GET "/deviceapi/hardware/iotjoin/requestMqttToken"

#define HTTPS_TOKEN_LEN             71

#define MQTT_TOPIC_STR_LEN          256
#define MQTT_TOPIC_STR_LEN_LONG     512
#define MQTT_TOKEN_LEN              171

#define SENSECRAFT_TASK_STACK_SIZE  8*1024
#define SENSECRAFT_TASK_PRIO        4
struct sensecraft_mqtt_connect_info
{
    char serverUrl[128];
    char token[MQTT_TOKEN_LEN];
    int mqttPort;
    int mqttsPort;
    int expiresIn;
};

struct app_sensecraft
{
    struct view_data_deviceinfo deviceinfo;
    struct sensecraft_mqtt_connect_info mqtt_info;
    char https_token[HTTPS_TOKEN_LEN];
    SemaphoreHandle_t sem_handle;
    SemaphoreHandle_t net_sem_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    TaskHandle_t task_handle;
    esp_mqtt_client_handle_t mqtt_handle;
    esp_mqtt_client_config_t mqtt_cfg;
    char mqtt_broker_uri[256];
    char mqtt_client_id[256];
    char mqtt_password[MQTT_TOKEN_LEN];
    char topic_down_task_publish[MQTT_TOPIC_STR_LEN];
    char topic_down_version_notify[MQTT_TOPIC_STR_LEN];
    char topic_up_change_device_status[MQTT_TOPIC_STR_LEN];
    char topic_up_task_publish_ack[MQTT_TOPIC_STR_LEN];
    char topic_up_warn_event_report[MQTT_TOPIC_STR_LEN];
    char topic_cache[MQTT_TOPIC_STR_LEN];
    char *p_mqtt_recv_buf;
    bool net_flag;
    bool timesync_flag;
    bool token_flag;
    bool mqtt_connected_flag;
    time_t last_http_time;
};

esp_err_t app_sensecraft_init(void);

esp_err_t app_sensecraft_https_token_get(char *p_token, size_t len);

esp_err_t app_sensecraft_https_token_gen(struct view_data_deviceinfo *p_deviceinfo, char *p_token, size_t len);

esp_err_t app_sensecraft_mqtt_report_taskflow(char *p_str, size_t len);

esp_err_t app_sensecraft_mqtt_report_taskflow_ack(char *request_id, char *p_str, size_t len);

esp_err_t app_sensecraft_mqtt_report_taskflow_status(intmax_t tasklist_id, int tf_status);

esp_err_t app_sensecraft_mqtt_report_taskflow_module_status(intmax_t tasklist_id, 
                                                            int tf_status,  
                                                            char *p_module_name, int module_status);
esp_err_t app_sensecraft_mqtt_report_warn_event(intmax_t taskflow_id, 
                                                char *taskflow_name, 
                                                char *p_img, size_t img_len, 
                                                char *p_msg, size_t msg_len);

esp_err_t app_sensecraft_mqtt_report_device_status_generic(char *event_value_fields);
esp_err_t app_sensecraft_mqtt_report_device_status(struct view_data_device_status *dev_status);


#ifdef __cplusplus
}
#endif
