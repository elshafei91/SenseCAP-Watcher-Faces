#pragma once

#include <stdbool.h>
#include "esp_err.h"

#include "sscma_client_commands.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct sscma_client_t sscma_client_t; /*!< Type of SCCMA client */

    /**
     * @brief SCCMA client interface
     */
    struct sscma_client_t
    {
        /**
         * @brief Reset SCCMA client
         *
         * @param[in] client SCCMA client handle
         * @return
         *          - ESP_OK on success
         */
        esp_err_t (*reset)(sscma_client_t *client);

        /**
         * @brief Initialize SCCMA client
         *
         * @param[in] client SCCMA client handle
         * @return
         *          - ESP_OK on success
         */
        esp_err_t (*init)(sscma_client_t *client);

        /**
         * @brief Destory SCCMA client
         *
         * @param[in] client SCCMA client handle
         * @return
         *          - ESP_OK on success
         */
        esp_err_t (*del)(sscma_client_t *client);

        /**
         * @brief Write data to SCCMA client
         *
         * @param[in] client SCCMA client handle
         * @param[in] data Data to be written
         * @param[in] size Size of data
         * @return
         *          - ESP_ERR_INVALID_ARG   if parameter is invalid
         *          - ESP_ERR_NOT_SUPPORTED if read is not supported by transport
         *          - ESP_OK                on success
         */
        esp_err_t (*write)(sscma_client_t *client, const void *data, size_t size);

        /**
         * @brief Read data from SCCMA client
         *
         * @param[in] client SCCMA client handle
         * @param[in] data Data to be read
         * @param[in] size Size of data
         * @return
         *          - ESP_ERR_INVALID_ARG   if parameter is invalid
         *          - ESP_ERR_NOT_SUPPORTED if read is not supported by transport
         *          - ESP_OK                on success
         *
         */
        esp_err_t (*read)(sscma_client_t *client, void *data, size_t size);

        /**
         * @brief Get available size of data
         *
         * @param[in] client SCCMA client handle
         * @param[out] len Available size
         * @return
         *          - ESP_ERR_INVALID_ARG   if parameter is invalid
         *          - ESP_ERR_NOT_SUPPORTED if read is not supported by transport
         *          - ESP_OK
         */
        esp_err_t (*available)(sscma_client_t *client, size_t *len);
    };

#ifdef __cplusplus
}
#endif
