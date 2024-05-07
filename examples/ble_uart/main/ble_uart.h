/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/** Major version number (X.x.x) */
#define BLE_UART_VERSION_MAJOR 0
/** Minor version number (x.X.x) */
#define BLE_UART_VERSION_MINOR 0
/** Patch version number (x.x.X) */
#define BLE_UART_VERSION_PATCH 1

/**
 * Macro to convert version number into an integer
 *
 * To be used in comparisons, such as BLE_UART_VERSION >= BLE_UART_VERSION_VAL(4, 0, 0)
 */
#define BLE_UART_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))

/**
 * Current version, as an integer
 *
 * To be used in comparisons, such as BLE_UART_VERSION >= BLE_UART_VERSION_VAL(4, 0, 0)
 */
#define BLE_UART_VERSION BLE_UART_VERSION_VAL(BLE_UART_VERSION_MAJOR, BLE_UART_VERSION_MINOR, BLE_UART_VERSION_PATCH)
