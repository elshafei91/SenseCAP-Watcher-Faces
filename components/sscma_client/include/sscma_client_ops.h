#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "sscma_client_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Configuration of SCCMA client
     */
    typedef struct
    {
        int reset_gpio_num; /*!< GPIO number of reset pin */
        int tx_buffer_size; /*!< Size of TX buffer */
        int rx_buffer_size; /*!< Size of RX buffer */
        int task_priority;  /* SSCMA task priority */
        int task_stack;     /* SSCMA task stack size */
        int task_affinity;  /* SSCMA task pinned to core (-1 is no affinity) */
        void *user_ctx;     /* User context */
        struct
        {
            unsigned int reset_active_high : 1; /*!< Setting this if the panel reset is high level active */
        } flags;                                /*!< SSCMA client config flags */
    } sscma_client_config_t;

    /**
     * @brief Create new SCCMA client
     *
     * @param[in] io IO handle
     * @param[in] config SCCMA client config
     * @param[out] ret_client SCCMA client handle
     * @return
     *          - ESP_OK on success
     *          - ESP_ERR_INVALID_ARG if parameter is invalid
     *          - ESP_ERR_NO_MEM if out of memory
     */
    esp_err_t sscma_client_new(const sscma_client_io_handle_t io, const sscma_client_config_t *config, sscma_client_handle_t *ret_client);

    /**
     * @brief Destroy SCCMA client
     *
     * @param[in] client SCCMA client handle
     * @return
     *          - ESP_OK on success
     */
    esp_err_t sscma_client_del(sscma_client_handle_t client);

    /**
     * @brief Initialize SCCMA client
     *
     * @param[in] client SCCMA client handle
     * @return
     *          - ESP_OK on success
     */
    esp_err_t sscma_client_init(sscma_client_handle_t client);

    /**
     * @brief Reset SCCMA client
     *
     * @param[in] client SCCMA client handle
     * @return
     *          - ESP_OK on success
     */
    esp_err_t sscma_client_reset(sscma_client_handle_t client);
    /**
     * @brief Read data from SCCMA client
     *
     * @param[in] client SCCMA client handle
     * @param[out] data Data to be read
     * @param[in] size Size of data
     * @return
     *          - ESP_ERR_INVALID_ARG   if parameter is invalid
     *          - ESP_ERR_NOT_SUPPORTED if read is not supported by transport
     *          - ESP_OK                on success
     */
    esp_err_t sscma_client_read(sscma_client_handle_t client, void *data, size_t size);

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
    esp_err_t sscma_client_write(sscma_client_handle_t client, const void *data, size_t size);

    /**
     * @brief Get available data
     *
     * @param[in] client SCCMA client handle
     * @param[out] ret_avail Available data
     *
     * @return
     *          - ESP_OK on success
     */
    esp_err_t sscma_client_available(sscma_client_handle_t client, size_t *ret_avail);

#ifdef __cplusplus
}
#endif