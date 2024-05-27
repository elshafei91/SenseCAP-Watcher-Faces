/**
 * OTA service
 * Author: Jack <jack.shao@seeed.cc>
*/

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_https_ota.h"

#include "sensecap-watcher.h"

#include "data_defs.h"

#ifdef __cplusplus
extern "C" {
#endif


enum {
    OTA_STATUS_SUCCEED = 0,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_FAIL,
};

// ota status for SenseCraft platform
// !!! UI should use this status value as well
// this status value is merged for multiple OTA tasks, e.g. both himax and esp32
// are going to be upgraded.
enum {
    SENSECRAFT_OTA_STATUS_IDLE = 0,
    SENSECRAFT_OTA_STATUS_UPGRADING,
    SENSECRAFT_OTA_STATUS_SUCCEED,
    SENSECRAFT_OTA_STATUS_FAIL,
};

#define ESP_ERR_OTA_ALREADY_RUNNING         0x200
#define ESP_ERR_OTA_VERSION_TOO_OLD         0x201
#define ESP_ERR_OTA_CONNECTION_FAIL         0x202
#define ESP_ERR_OTA_GET_IMG_HEADER_FAIL     0x203
#define ESP_ERR_OTA_DOWNLOAD_FAIL           0x204
#define ESP_ERR_OTA_SSCMA_START_FAIL        0x205
#define ESP_ERR_OTA_SSCMA_WRITE_FAIL        0x206

#define ESP_ERR_OTA_JSON_INVALID            0x300
#define ESP_ERR_OTA_NO_HIMAX_VERSION        0x301
#define ESP_ERR_OTA_TIMEOUT                 0x302


// used to pass userdata to http client event handler
typedef struct {
    sscma_client_handle_t client;
    sscma_client_flasher_handle_t flasher;
    int ota_type;
    esp_err_t *err;
} ota_http_userdata_t;

//worker cmd
enum {
    CMD_esp_https_ota_begin = 0,
    CMD_esp_https_ota_get_img_desc,
    CMD_esp_ota_get_running_partition,
    CMD_esp_ota_get_partition_description,
    CMD_esp_https_ota_perform,
    CMD_esp_https_ota_finish,
};
typedef struct {
    esp_https_ota_config_t *ota_config;
    esp_https_ota_handle_t *ota_handle;
    esp_app_desc_t *app_desc;
    esp_partition_t *partition;
    esp_err_t err;
} ota_worker_task_data_t;

// ota status queue item
typedef struct {
    int ota_src;  //source, himax? esp32?
    struct view_data_ota_status ota_status;
} ota_status_q_item_t;

// ota cmd queue item
typedef struct {
    int ota_type;
    char url[1000];  //we need copy url here, allocate large enough memory here
    size_t  file_size;
} ota_cmd_q_item_t;

esp_err_t app_ota_init(void);

/**
 * caller should not care about the progress,
 * percentage progress will send as event to event loop, consumers like UI can then use them
 * to render progress UI element.
 * event:
 * - VIEW_EVENT_OTA_AI_MODEL
 * This function will block until AI model download done or failed, error code will be returned
 * if failed.
*/
esp_err_t app_ota_ai_model_download(char *url, int size_bytes);

/**
 * caller should listen to event loop to get percentage progess, the progress event will be
 * sent on flash chunk written rhythm, consumers can then down speed to slower rhythm, e.g. 10%,
 * report to BLE on that slower rhythm.
 * caller should also listen to event loop to get failure state and failure reason.
 * events:
 * - VIEW_EVENT_OTA_ESP32_FW
 * - VIEW_EVENT_OTA_HIMAX_FW
 * This function will not block, caller should asynchronously get result and progress via events above.
*/
esp_err_t app_ota_esp32_fw_download(char *url);
esp_err_t app_ota_himax_fw_download(char *url);

/**
 * used by console cmd `ota` to force ota
*/
void  app_ota_any_ignore_version_check(bool ignore);


#ifdef __cplusplus
}
#endif