#pragma once

#include "esp_assert.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct sscma_client_io_t *sscma_client_io_handle_t; /*!< Type of SSCMA client IO handle */

    typedef struct sscma_client_t
    {
        sscma_client_io_handle_t io; /* !< IO handle */
        int reset_gpio_num;          /* !< GPIO number of reset pin */
        bool reset_level;            /* !< Level of reset pin */
        bool inited;                 /* !< Whether inited */
        struct
        {
            uint8_t *data;
            size_t len;
            size_t pos;
        } rx_buffer, tx_buffer; /* !< RX and TX buffer */
    } *sscma_client_handle_t;

#ifdef __cplusplus
}
#endif
