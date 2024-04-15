#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "sscma_client_io_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sscma_client_proto_t sscma_client_proto_t; /*!< Type of SSCMA protocol handle */

struct sscma_client_proto_t
{
    /**
     * @brief Start protocal transmitter
     * @param[in] handle transmitter handle
     * @param[in] offset offset
     * @return
     * - ESP_OK
     */
    esp_err_t (*start)(sscma_client_proto_t *handle);

    /**
     * @brief Write data to protocal transmitter
     * @param[in] handle transmitter handle
     * @param[in] data data
     * @param[in] len length
     * @return
     * - ESP_OK
     */
    esp_err_t (*write)(sscma_client_proto_t *handle, const void *data, size_t len);

    /**
     * @brief Start protocal transmitter
     * @param[in] handle transmitter handle
     * @return
     * - ESP_OK
     */
    esp_err_t (*finish)(sscma_client_proto_t *handle);

    /**
     * @brief Abort protocal transmitter
     * @param[in] handle transmitter handle
     * @return
     * - ESP_OK
     */
    esp_err_t (*abort)(sscma_client_proto_t *handle);

    /**
     * @brief Delete protocal transmitter
     * @param[in] handle transmitter handle
     * @return
     * - ESP_OK
     */
    esp_err_t (*del)(sscma_client_proto_t *handle);
};

/**
 * Start protocal transmitter
 * @param[in] handle transmitter handle
 * @return
 * - ESP_OK
 */
esp_err_t sscma_client_proto_start(sscma_client_proto_t *handle);

/**
 * Write data to protocal transmitter
 * @param[in] handle transmitter handle
 * @param[in] data data
 * @param[in] len length
 * @return
 * - ESP_OK
 */
esp_err_t sscma_client_proto_write(sscma_client_proto_t *handle, const void *data, size_t len);

/**
 * Finish protocal transmitter
 * @param[in] handle transmitter handle
 * @return
 * - ESP_OK
 */
esp_err_t sscma_client_proto_finish(sscma_client_proto_t *handle);

/**
 * Abort protocal transmitter
 * @param[in] handle transmitter handle
 * @return
 * - ESP_OK
 */
esp_err_t sscma_client_proto_abort(sscma_client_proto_t *handle);

/**
 * Delete protocal transmitter
 * @param[in] handle transmitter handle
 * @return
 * - ESP_OK
 */
esp_err_t sscma_client_proto_delete(sscma_client_proto_t *handle);

/**
 * Create xmodem protocal
 * @param[in] io io
 * @param[out] ret_handle handle
 * @return
 * - ESP_OK
 */
esp_err_t sscma_client_proto_xmodem_create(sscma_client_io_t *io, sscma_client_proto_t **ret_handle);

#ifdef __cplusplus
}
#endif