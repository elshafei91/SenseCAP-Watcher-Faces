/**
 * OTA service
 * Author: Jack <jack.shao@seeed.cc>
*/

#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "data_defs.h"

#ifdef __cplusplus
extern "C" {
#endif


enum {
    OTA_STATUS_SUCCEED = 0,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_FAIL,
};

#define ESP_ERR_OTA_ALREADY_RUNNING         0x200
#define ESP_ERR_OTA_VERSION_TOO_OLD         0x201
#define ESP_ERR_OTA_CONNECTION_FAIL         0x202
#define ESP_ERR_OTA_GET_IMG_HEADER_FAIL     0x203
#define ESP_ERR_OTA_DOWNLOAD_FAIL           0x204


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


#ifdef __cplusplus
}
#endif