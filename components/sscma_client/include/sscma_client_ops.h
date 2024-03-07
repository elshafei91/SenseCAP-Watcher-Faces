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
        int reset_gpio_num;        /*!< GPIO number of reset pin */
        int tx_buffer_size;        /*!< Size of TX buffer */
        int rx_buffer_size;        /*!< Size of RX buffer */
        int process_task_priority; /* SSCMA process task priority */
        int process_task_stack;    /* SSCMA process task stack size */
        int process_task_affinity; /* SSCMA process task pinned to core (-1 is no affinity) */
        int monitor_task_priority; /* SSCMA monitor task priority */
        int monitor_task_stack;    /* SSCMA monitor task stack size */
        int monitor_task_affinity; /* SSCMA monitor task pinned to core (-1 is no affinity) */
        void *user_ctx;            /* User context */
        struct
        {
            unsigned int reset_active_high : 1; /*!< Setting this if the panel reset is high level active */
        } flags;                                /*!< SSCMA client config flags */
    } sscma_client_config_t;

#define SSCMA_CLIENT_CONFIG_DEFAULT()   \
    {                                   \
        .reset_gpio_num = -1,           \
        .tx_buffer_size = 4096,         \
        .rx_buffer_size = 65536,        \
        .process_task_priority = 5,     \
        .process_task_stack = 4096,     \
        .process_task_affinity = -1,    \
        .monitor_task_priority = 4,     \
        .monitor_task_stack = 10240,    \
        .monitor_task_affinity = -1,    \
        .user_ctx = NULL,               \
        .flags = {                      \
            .reset_active_high = false, \
        }                               \
    }

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

    /**
     * @brief Register callback
     *
     * @param[in] client SCCMA client handle
     * @param[in] callback SCCMA client callback
     * @param[in] user_ctx User context
     * @return
     *          - ESP_OK on success
     */
    esp_err_t sscma_client_register_callback(sscma_client_handle_t client, const sscma_client_callback_t *callback, void *user_ctx);

    /**
     * @brief Clear reply
     *
     * @param[in] reply Reply
     * @return void
     */
    void sscma_client_reply_clear(sscma_client_reply_t *reply);

    /**
     * @brief Send request to SCCMA client
     *
     * @param[in] client SCCMA client handle
     *
     * @return
     *          - ESP_OK on success
     */
    esp_err_t sscma_client_request(sscma_client_handle_t client, const char *cmd, sscma_client_reply_t *reply, bool wait, TickType_t timeout);

    /**
     * @brief Get SCCMA client info
     *
     * @param[in] client SCCMA client handle
     * @param[in] info pointer to sscma_client_info_t
     * @param[in] cached true if info is cached
     * @return
     *          - ESP_OK on success
     */
    esp_err_t sscma_client_get_info(sscma_client_handle_t client, sscma_client_info_t **info, bool cached);

    /**
     * @brief Send request to SCCMA clien
     *
     * @param[in] client SCCMA client handle
     * @param[in] model pointer to sscma_client_model_t
     * @param[in] cached true if model is cached
     * @return
     *          - ESP_OK on success
     */
    esp_err_t sscma_client_get_model(sscma_client_handle_t client, sscma_client_model_t **model, bool cached);

#ifdef __cplusplus
}
#endif