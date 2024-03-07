#pragma once

#include "esp_assert.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct sscma_client_io_t *sscma_client_io_handle_t; /*!< Type of SSCMA client IO handle */
    typedef struct sscma_client_t *sscma_client_handle_t;       /*!< Type of SCCMA client handle */

    /**
     * @brief Reply message
     */
    typedef struct
    {
        cJSON *payload;
        char *data;
        size_t len;
    } sscma_client_reply_t;

    /**
     * @brief Request message
     */
    typedef struct
    {
        char cmd[32];
        QueueHandle_t reply;
        ListItem_t item;
    } sscma_client_request_t;

    /**
     * @brief Callback function of SCCMA client
     * @param[in] client SCCMA client handle
     * @param[in] reply Reply message
     * @param[in] user_ctx User context
     * @return None
     */
    typedef void (*sscma_client_event_cb_t)(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx);

    /**
     * @brief Type of SCCMA client callback
     */
    typedef struct
    {
        sscma_client_event_cb_t on_event;
        sscma_client_event_cb_t on_log;
    } sscma_client_callback_t;

    struct sscma_client_t
    {
        sscma_client_io_handle_t io;      /* !< IO handle */
        int reset_gpio_num;               /* !< GPIO number of reset pin */
        bool reset_level;                 /* !< Level of reset pin */
        bool inited;                      /* !< Whether inited */
        sscma_client_event_cb_t on_event; /* !< Callback function */
        sscma_client_event_cb_t on_log;   /* !< Callback function */
        void *user_ctx;                   /* !< User context */
        TaskHandle_t monitor_task;        /* !< Monitor task handle */
        TaskHandle_t process_task;        /* !< Process task handle */
        struct
        {
            char *data;            /* !< Data buffer */
            size_t len;            /* !< Data length */
            size_t pos;            /* !< Data position */
        } rx_buffer, tx_buffer;    /* !< RX and TX buffer */
        QueueHandle_t reply_queue; /* !< Queue for reply message */
        List_t *request_list;      /* !< Request list */
    };

#ifdef __cplusplus
}
#endif
