/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "iot_button.h"
#include "iot_knob.h"
#include "led_strip.h"
#include "led_strip_interface.h"

/* Light */
#define BSP_RGB_CTRL       (GPIO_NUM_4)

/* Buttons */
#define BSP_KNOB_A         (GPIO_NUM_1)
#define BSP_KNOB_B         (GPIO_NUM_2)
#define BSP_BTN_IO         (GPIO_NUM_3)

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
 *
 * WS2812
 *
 **************************************************************************************************/

/**
 * @brief Initialize WS2812
 *
 * @return
 *     - ESP_OK Success
 *     - ESP_ERR_INVALID_ARG Parameter error
 */
esp_err_t bsp_led_init();

/**
 * @brief Set RGB for a specific pixel
 *
 * @param r: red part of color
 * @param g: green part of color
 * @param b: blue part of color
 *
 * @return
 *      - ESP_OK: Set RGB for a specific pixel successfully
 *      - ESP_ERR_INVALID_ARG: Set RGB for a specific pixel failed because of invalid parameters
 *      - ESP_FAIL: Set RGB for a specific pixel failed because other error occurred
 */
esp_err_t bsp_led_rgb_set(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif