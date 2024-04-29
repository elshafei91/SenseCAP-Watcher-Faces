/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "sensecap-watcher.h"

extern void lv_demo_keypad_encoder(void);

static const char *TAG = "Main";

lv_disp_t *lvgl_disp;

void i2c_detect(void)
{
    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (address << 1) | I2C_MASTER_WRITE, 0x1);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(BSP_GENERAL_I2C_NUM, cmd, 50 / portTICK_PERIOD_MS);
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                printf("%02x ", address);
            } else if (ret == ESP_ERR_TIMEOUT) {
                printf("UU ");
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
}

void app_main(void)
{
    lvgl_disp = bsp_lvgl_init();
    assert(lvgl_disp != NULL);

    bsp_rgb_init();
    bsp_rgb_set(0, 0, 2);
    bsp_sdcard_init_default();
    lv_demo_keypad_encoder();
}
