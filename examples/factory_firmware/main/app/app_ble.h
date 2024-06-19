/**
 * BLE service
 * Author: Jack <jack.shao@seeed.cc>
*/


#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

//forward declaration
struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

/** GATT server. */
#define GATT_SVR_SVC_ALERT_UUID               0x1811
#define GATT_SVR_CHR_SUP_NEW_ALERT_CAT_UUID   0x2A47
#define GATT_SVR_CHR_NEW_ALERT                0x2A46
#define GATT_SVR_CHR_SUP_UNR_ALERT_CAT_UUID   0x2A48
#define GATT_SVR_CHR_UNR_ALERT_STAT_UUID      0x2A45
#define GATT_SVR_CHR_ALERT_NOT_CTRL_PT        0x2A44

/* implemented in app_ble_gatt_svr.c */
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
int gatt_svr_init(void);

esp_err_t app_ble_init(void);
void set_ble_status(int caller, int status);
uint8_t *app_ble_get_mac_address(void);

#ifdef __cplusplus
}
#endif