/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_spiffs.h"
#include "jpeg_decoder.h"
#include "indoor_ai_camera.h"

extern const lv_img_dsc_t image;
// extern void lv_demo_keypad_encoder(void);

static const char *TAG = "Main";

lv_disp_t *lvgl_disp;

static uint16_t pixels[DRV_LCD_H_RES * DRV_LCD_V_RES] = {0};
static char jpg_buf[32 * 1024] = {0};

void app_main(void)
{
    lvgl_disp = bsp_lvgl_init();
    assert(lvgl_disp != NULL);

    // ESP_LOGI(TAG, "Initializing SPIFFS");
    // esp_vfs_spiffs_conf_t conf = {
    //   .base_path = "/spiffs",
    //   .partition_label = NULL,
    //   .max_files = 5,
    //   .format_if_mount_failed = false
    // };
    // esp_err_t ret = esp_vfs_spiffs_register(&conf);
    // if (ret != ESP_OK) {
    //     if (ret == ESP_FAIL) {
    //         ESP_LOGE(TAG, "Failed to mount or format filesystem");
    //     } else if (ret == ESP_ERR_NOT_FOUND) {
    //         ESP_LOGE(TAG, "Failed to find SPIFFS partition");
    //     } else {
    //         ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
    //     }
    //     return;
    // }
    // FILE* f = fopen("/spiffs/image.jpg", "r");
    // if (f == NULL) {
    //     ESP_LOGE(TAG, "Failed to open image.jpg");
    //     return;
    // }
    // memset(jpg_buf, 0, sizeof(jpg_buf));
    // size_t jpg_size = fread(jpg_buf, 1, sizeof(jpg_buf), f);
    // if (jpg_size == 0) {
    //     ESP_LOGE(TAG, "Failed to read image.jpg");
    //     return;
    // }
    // fclose(f);
    // esp_jpeg_image_cfg_t jpeg_cfg = {
    //     .indata = (uint8_t *)jpg_buf,
    //     .indata_size = jpg_size,
    //     .outbuf = (uint8_t*)pixels,
    //     .outbuf_size = DRV_LCD_H_RES * DRV_LCD_V_RES * sizeof(uint16_t),
    //     .out_format = JPEG_IMAGE_FORMAT_RGB565,
    //     .out_scale = JPEG_IMAGE_SCALE_0,
    //     .flags = {
    //         .swap_color_bytes = 0,
    //     }
    // };
    // esp_jpeg_image_output_t outimg;
    // esp_jpeg_decode(&jpeg_cfg, &outimg);
    // ESP_LOGI(TAG, "Size of image is: %dpx x %dpx", outimg.width, outimg.height);
    // lv_img_dsc_t img_dsc = {
    //    .header = {
    //        .cf = LV_IMG_CF_TRUE_COLOR,
    //        .always_zero = 0,
    //        .w = DRV_LCD_H_RES,
    //        .h = DRV_LCD_V_RES,
    //     },
    //    .data_size = DRV_LCD_H_RES * DRV_LCD_V_RES * 2,
    //    .data = (const uint8_t *)pixels,
    // };
    // esp_vfs_spiffs_unregister(NULL);
    // ESP_LOGI(TAG, "SPIFFS unmounted");

    lv_obj_t * wp;
    wp = lv_img_create(lv_scr_act());
    // lv_img_set_src(wp, &img_dsc);
    // lv_img_set_src(wp, "A:/spiffs/image.jpg");
    lv_img_set_src(wp, &image);

    // lv_demo_keypad_encoder();
}
