

#pragma once

#include "cJSON.h"

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_mqtt_client_init(void);

esp_err_t app_mqtt_client_report_tasklist_ack(char *request_id, cJSON *task_settings_node);

esp_err_t app_mqtt_client_report_tasklist_status(int tasklist_id, int tasklist_status_num);

esp_err_t app_mqtt_client_report_warn_event(int tasklist_id, char *tasklist_name, int warn_type);


#ifdef __cplusplus
}
#endif
