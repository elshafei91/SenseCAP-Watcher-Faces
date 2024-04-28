#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_err.h"
#include "cJSON.h"


#include "app_cmd.h" //AT cmd driver

#define ble_at_sprintf(s, ...) sprintf((char *)(s), ##__VA_ARGS__)
#define WATCHER_BLE_DATA_MAX_LEN (512)
#define WATCHER_BLE_CMD_MAX_LEN (20)
#define WATCHER_BLE_STATUS_MAX_LEN (20)
#define WATCHER_BLE_DATA_BUFF_MAX_LEN (2 * 1024)

#define CHAR_DECLARATION_SIZE (sizeof(uint8_t))

#define PROFILE_APP_IDX 0
#define PROFILE_NUM 1

#define CONFIG_watcher_BLE_SERVICE_UUID_TEST 0x00FF
#define CONFIG_watcher_BLE_CHAR_UUID_TEST_A 0xFF01
#define CONFIG_watcher_BLE_CHAR_UUID_TEST_B 0xFF02
#define CONFIG_watcher_BLE_CHAR_UUID_TEST_C 0xFF03
#ifdef __cplusplus
extern "C"
{
#endif

    // Attributes Table element
    enum
    {
    WATCHER_BLE_IDX_SVC,
    watcher_ble_IDX_CHAR_A,
    watcher_ble_IDX_CHAR_VAL_A,
    watcher_ble_IDX_CHAR_CFG_A,
    watcher_ble_IDX_CHAR_B,
    watcher_ble_IDX_CHAR_VAL_B,
    watcher_ble_IDX_CHAR_C,
    watcher_ble_IDX_CHAR_VAL_C,
    WATCHER_BLE_IDX_NB
    };

    esp_err_t app_ble_init(void);

    esp_err_t app_ble_recv_event(void);

    esp_err_t app_ble_send_event(void);

// if used
#ifdef BLE_DEBUG

    esp_err_t app_Ble_Debug(void);

#endif

#ifdef __cplusplus
}
#endif