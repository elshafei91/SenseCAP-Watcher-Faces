
/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "iot_button.h"
#include "iot_knob.h"
#include "led_strip.h"
#include "led_strip_interface.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lcd_touch_chsc6x.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_io_expander.h"
#include "esp_io_expander_pca95xx_16bit.h"

/* RGB LED */
#define BSP_RGB_CTRL       (GPIO_NUM_40)

/* Knob */
#define BSP_KNOB_A         (GPIO_NUM_41)
#define BSP_KNOB_B         (GPIO_NUM_42)
#define BSP_KNOB_BTN       (IO_EXPANDER_PIN_NUM_3)

/* LCD */
#define BSP_LCD_SPI_SCLK   (GPIO_NUM_7)
#define BSP_LCD_SPI_MOSI   (GPIO_NUM_9)
#define BSP_LCD_SPI_CS     (GPIO_NUM_45)
#define BSP_LCD_GPIO_RST   (GPIO_NUM_NC)
#define BSP_LCD_GPIO_DC    (GPIO_NUM_1)
#define BSP_LCD_GPIO_BL    (IO_EXPANDER_PIN_NUM_7)

/* Touch */
#define BSP_TOUCH_I2C_NUM  (1)
#define BSP_TOUCH_GPIO_INT (GPIO_NUM_46)
#define BSP_TOUCH_I2C_SDA  (GPIO_NUM_39)
#define BSP_TOUCH_I2C_SCL  (GPIO_NUM_38)
#define BSP_TOUCH_I2C_CLK  (400000)

/* General */
#define BSP_GENERAL_I2C_NUM (0)
#define BSP_GENERAL_I2C_SDA (GPIO_NUM_47)
#define BSP_GENERAL_I2C_SCL (GPIO_NUM_48)
#define BSP_GENERAL_I2C_CLK  (400000)

/* Audio */
#define BSP_AUDIO_I2S_NUM   (0)
#define BSP_AUDIO_I2S_BCK   (GPIO_NUM_18)
#define BSP_AUDIO_I2S_WS    (GPIO_NUM_19)
#define BSP_AUDIO_I2S_DATA  (GPIO_NUM_23)

/* Camera */

/* Settings */
#define DRV_LCD_SPI_NUM        (SPI2_HOST)

#define DRV_LCD_H_RES          (240)
#define DRV_LCD_V_RES          (240)
#define DRV_LCD_PIXEL_CLK_HZ   (40 * 1000 * 1000)
#define DRV_LCD_CMD_BITS       (8)
#define DRV_LCD_PARAM_BITS     (8)
#define DRV_LCD_COLOR_SPACE    (ESP_LCD_COLOR_SPACE_BGR)
#define DRV_LCD_BITS_PER_PIXEL (16)
#define DRV_LCD_SWAP_XY        (1)
#define DRV_LCD_MIRROR_X       (0)
#define DRV_LCD_MIRROR_Y       (0)

#define DRV_LCD_BL_ON_LEVEL    (1)
#define DRV_LCD_LEDC_DUTY_RES  (LEDC_TIMER_10_BIT)
#define DRV_LCD_LEDC_CH        (1)

#define DRV_IO_EXP_INPUT_MASK  (0x007f) // P0.0 ~ P0.6
#define DRV_IO_EXP_OUTPUT_MASK (0xff80) // P0.7, P1.0 ~ P1.7

#define LVGL_DRAW_BUFF_DOUBLE  (1)
#define LVGL_DRAW_BUFF_HEIGHT  (CONFIG_LVGL_DRAW_BUFF_HEIGHT)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  /*!< LVGL port configuration */
    uint32_t        buffer_size;    /*!< Size of the buffer for the screen in pixels */
    bool            double_buffer;  /*!< True, if should be allocated two buffers */
    struct {
        unsigned int buff_dma: 1;    /*!< Allocated LVGL buffer will be DMA capable */
        unsigned int buff_spiram: 1; /*!< Allocated LVGL buffer will be in PSRAM */
    } flags;
} bsp_display_cfg_t;

esp_err_t bsp_rgb_init(void);
esp_err_t bsp_rgb_set(uint8_t r, uint8_t g, uint8_t b);

esp_err_t bsp_lcd_brightness_set(int brightness_percent);

lv_disp_t *bsp_lvgl_init(void);
lv_disp_t *bsp_lvgl_init_with_cfg(const bsp_display_cfg_t *cfg);

esp_io_expander_handle_t bsp_io_expander_init();


#ifdef __cplusplus
}
#endif