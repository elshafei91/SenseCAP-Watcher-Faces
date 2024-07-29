/**
 * SCD4x driver
 * Author: WayenWeng <jinyuan.weng@seeed.cc>
*/

#ifndef SENSOR_SCD4x_H
#define SENSOR_SCD4x_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_SCD4x_I2C_ADDR 0x62

/**
 * @brief Initializes the sensor.
 *
 * @return 0 on success, an error code otherwise
 */
int16_t sensor_scd4x_init(void);

/**
 * @brief Uninitializes the sensor.
 *
 * @return 0 on success, an error code otherwise
 */
int16_t sensor_scd4x_uninit(void);

/**
 * @brief Check whether new measurement data is available for read-out.
 *
 * @param data_ready_flag True if data available, otherwise false.
 *
 * @return 0 on success, an error code otherwise
 */
int16_t sensor_scd4x_get_data_ready_flag(bool* data_ready_flag);

/**
 * @brief Read sensor output and convert.
 *
 * @param co2 CO₂ concentration in milli ppm
 *
 * @return 0 on success, an error code otherwise
 */
int16_t sensor_scd4x_read_measurement(uint32_t *co2);

#ifdef __cplusplus
}
#endif

#endif
