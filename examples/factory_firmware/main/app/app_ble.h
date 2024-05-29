#ifndef APP_BLE_H
#define APP_BLE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>

#include "esp_err.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "esp_gatts_api.h"
#include "sdkconfig.h"
#include "app_cmd.h" 

/*----------------------------------------------------*/
// DEBUG MODE definition
//#define BLE_DEBUG
#define GATTS_TAG "Watcher_BLE_Server"

/*----------------------------------------------------*/
// Data definitions

#define GATTS_NUM_HANDLE_WATCHER 7      //critical warning - 7

#define TEST_DEVICE_NAME "010203040506070809-WACH"      

#define GATTS_DEMO_CHAR_VAL_LEN_MAX 0x40

#define PREPARE_BUF_MAX_SIZE 512000
#define TINY_BUF_MAX_SIZE 1024
#define PROFILE_NUM 2
#define PROFILE_WATCHER_APP_ID 0

/*----------------------------------------------------*/
// Configuration definitions

#define adv_config_flag (1 << 0)
#define scan_rsp_config_flag (1 << 1)
#define CONFIG_SET_RAW_ADV_DATA

/*----------------------------------------------------*/
// Function declarations

esp_err_t app_ble_init(void);
void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
void set_ble_status(int caller, int status);

#endif
