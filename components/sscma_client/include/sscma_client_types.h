#pragma once

#include "esp_assert.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_io_expander.h"

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
     *
     */
    typedef struct
    {
        char *id;     /* !< ID */
        char *name;   /* !< Name */
        char *hw_ver; /* !< Hardware version */
        char *sw_ver; /* !< Software version */
        char *fw_ver; /* !< Firmware version */
    } sscma_client_info_t;

    typedef struct
    {
        int id;             /* !< ID */
        char *uuid;         /* !< UUID */
        char *name;         /*!< Name */
        char *ver;          /*!< Version */
        char *category;     /*!< Category */
        char *algorithm;    /*!< Algorithm */
        char *description;  /*!< Description */
        char *classes[80];  /*!< Classes */
        char *token;        /*!< Token */
        char *url;          /*!< URL */
        char *manufacturer; /*!< Manufacturer */
    } sscma_client_model_t;

    typedef struct
    {
        int id;
        int type;
        int state;
        int opt_id;
        char *opt_detail;
    } sscma_client_sensor_t;

    typedef struct
    {
        uint16_t x;
        uint16_t y;
        uint16_t w;
        uint16_t h;
        uint8_t score;
        uint8_t target;
    } sscma_client_box_t;

    typedef struct
    {
        uint8_t target;
        uint8_t score;
    } sscma_client_class_t;

    typedef struct
    {
        uint16_t x;
        uint16_t y;
        uint16_t z;
        uint8_t score;
        uint8_t target;
    } sscma_client_point_t;

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
        sscma_client_io_handle_t io;          /* !< IO handle */
        int reset_gpio_num;                   /* !< GPIO number of reset pin */
        bool reset_level;                     /* !< Level of reset pin */
        bool inited;                          /* !< Whether inited */
        sscma_client_info_t info;             /* !< Info */
        sscma_client_model_t model;           /* !< Model */
        sscma_client_event_cb_t on_event;     /* !< Callback function */
        sscma_client_event_cb_t on_log;       /* !< Callback function */
        void *user_ctx;                       /* !< User context */
        esp_io_expander_handle_t io_expander; /* !< IO expander handle */
        TaskHandle_t monitor_task;            /* !< Monitor task handle */
        TaskHandle_t process_task;            /* !< Process task handle */
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
