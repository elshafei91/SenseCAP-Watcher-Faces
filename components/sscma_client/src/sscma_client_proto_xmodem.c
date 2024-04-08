#include <stdlib.h>
#include <string.h>
#include <sys/cdefs.h>

#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"

#include "sscma_client_proto.h"

static const char *TAG = "sscma_client.proto.xmodem";

#define XSOH 0x01 // Start Of Header
#define XSTX 0x02
#define XETX 0x03
#define XEOT 0x04 // End Of Transfer
#define XENQ 0x05
#define XACK 0x06  // ACK
#define XNACK 0x15 // NACK
#define XETB 0x17  //
#define XCAN 0x18  // CANCEL
#define XC 0x43
#define XEOF 0x1A

#define XMODEM_BLOCK_SIZE 128

#define WRITE_BLOCK_MAX_RETRIES 15
#define TRANSFER_ACK_TIMEOUT 30000         // 30 seconds
#define TRANSFER_EOT_TIMEOUT 30000         // 30 seconds
#define TRANSFER_ETB_TIMEOUT 30000         // 30 seconds
#define TRANSFER_WRITE_BLOCK_TIMEOUT 30000 // 30 seconds

esp_err_t sscma_client_proto_xmodem_start(sscma_client_proto_handle_t proto);
esp_err_t sscma_client_proto_xmodem_write(sscma_client_proto_handle_t proto, const void *data, size_t len);
esp_err_t sscma_client_proto_xmodem_finish(sscma_client_proto_handle_t proto);
esp_err_t sscma_client_proto_xmodem_abort(sscma_client_proto_handle_t proto);
esp_err_t sscma_client_proto_xmodem_del(sscma_client_proto_handle_t proto);

typedef enum
{
    INITIAL,
    WAIT_FOR_C,
    WAIT_FOR_C_TIMEOUT,
    WAIT_FOR_C_ACK,
    WRITE_BLOCK_FAILED,
    ABORT_TRANSFER,
    WRITE_BLOCK,
    C_ACK_RECEIVED,
    COMPLETE,
    WRITE_EOT,
    WAIT_FOR_EOT_ACK,
    TIMEOUT_EOT,
    WRITE_BLOCK_TIMEOUT,
    WRITE_ETB,
    WAIT_FOR_ETB_ACK,
    TIMEOUT_ETB,
    WAIT_WRITE_BLOCK,
    FAILED,
    FINAL,
} xmodem_state_t;

typedef struct xmodem_packet_t
{
    uint8_t preamble;
    uint8_t id;
    uint8_t id_complement;
    uint8_t data[XMODEM_BLOCK_SIZE];
    union
    {
        uint8_t data[2];
        uint16_t value;
    } crc;
} __attribute__((packed, aligned(1))) xmodem_packet_t;

typedef struct
{
    sscma_client_proto_t base;  /*!< The base class. */
    sscma_client_io_t *io;      /*!< The IO interface. */
    SemaphoreHandle_t lock;     /*!< The lock. */
    xmodem_state_t state;       /*!< The state of the protocol. */
    xmodem_packet_t cur_packet; /*!< The current packet being transmitted. */
    uint8_t cur_packet_id;      /*!< The ID of the current packet. */
    int64_t cur_time;           /*!< The current time. */
    uint8_t *buf;
    uint32_t total_len;
    uint32_t xfer_len;
    uint32_t xfer_size;
    uint8_t write_block_retries;
} sscma_client_proto_xmodem_t;

static inline bool xmodem_calculate_crc(const uint8_t *data, const uint32_t size,
                                        uint16_t *result)
{
    uint16_t crc = 0x0;
    uint32_t count = size;
    bool status = false;
    uint8_t i = 0;

    if (0 != data && 0 != result)
    {
        status = true;

        while (0 < count--)
        {
            crc = crc ^ (uint16_t)*data << 8;
            data++;
            i = 8;

            do
            {
                if (0x8000 & crc)
                {
                    crc = crc << 1 ^ 0x1021;
                }
                else
                {
                    crc = crc << 1;
                }

            } while (0 < --i);
        }
        *result = ((crc & 0xFF) << 8) + ((crc >> 8) & 0xFF);
    }

    return status;
}

static inline bool xmodem_verify_packet(const xmodem_packet_t packet,
                                        uint8_t expected_packet_id)
{
    bool status = false;
    uint8_t crc_status = false;
    uint16_t calculated_crc = 0;

    crc_status = xmodem_calculate_crc(packet.data, XMODEM_BLOCK_SIZE, &calculated_crc);

    if (packet.preamble == XSOH &&
        packet.id == expected_packet_id &&
        packet.id_complement == 0xFF - packet.id &&
        crc_status && calculated_crc == packet.crc.value)
    {
        status = true;
    }

    return status;
}

static inline bool xmodem_timeout(sscma_client_proto_xmodem_t *xmodem, int64_t timeout)
{
    if (esp_timer_get_time() - xmodem->cur_time >= (timeout * 1000))
    {
        return true;
    }

    vTaskDelay(1 / portTICK_PERIOD_MS);

    return false;
}

xmodem_state_t xmodem_process(sscma_client_proto_xmodem_t *xmodem)
{
    uint8_t response = 0;
    uint8_t ctrl = 0;
    size_t rlen = 0;

    switch (xmodem->state)
    {
    case INITIAL:
    {
        ESP_LOGD(TAG, "INITIAL");
        xmodem->state = WAIT_FOR_C;
        xmodem->cur_time = esp_timer_get_time();
        break;
    }
    case WAIT_FOR_C:
    {
        ESP_LOGD(TAG, "WAIT_FOR_C");
        if (sscma_client_io_available(xmodem->io, &rlen) == ESP_OK && rlen)
        {
            sscma_client_io_read(xmodem->io, &response, 1);
            if (response == XC)
            {
                xmodem->state = WRITE_BLOCK;
                xmodem->cur_time = esp_timer_get_time();
            }
        }
        else if (xmodem_timeout(xmodem, TRANSFER_ACK_TIMEOUT))
        {
            xmodem->state = WAIT_FOR_C_TIMEOUT;
        }
        break;
    }
    case WRITE_BLOCK:
    {
        ESP_LOGD(TAG, "WRITE_BLOCK");
        if (xmodem->buf == NULL || xmodem->total_len == 0)
        {
            break;
        }
        /* setup current packet */
        if (xmodem->cur_packet.id != xmodem->cur_packet_id || xmodem->cur_packet_id == 1)
        {
            xmodem->cur_packet.preamble = XSOH;
            xmodem->cur_packet.id = xmodem->cur_packet_id;
            xmodem->cur_packet.id_complement = 0xFF - xmodem->cur_packet_id;
            memset(xmodem->cur_packet.data, 0xFF, XMODEM_BLOCK_SIZE);
            xmodem->xfer_size = xmodem->total_len - xmodem->xfer_len > XMODEM_BLOCK_SIZE ? XMODEM_BLOCK_SIZE : xmodem->total_len - xmodem->xfer_len;
            memcpy(xmodem->cur_packet.data, xmodem->buf + xmodem->xfer_len, xmodem->xfer_size);
            xmodem_calculate_crc(xmodem->cur_packet.data, XMODEM_BLOCK_SIZE, &xmodem->cur_packet.crc.value);
        }
        sscma_client_io_write(xmodem->io, (uint8_t *)&xmodem->cur_packet, sizeof(xmodem_packet_t));
        xmodem->cur_packet_id++;
        xmodem->xfer_len += xmodem->xfer_size;
        xmodem->state = WAIT_FOR_C_ACK;
        xmodem->cur_time = esp_timer_get_time();

        break;
    }
    case WAIT_WRITE_BLOCK:
    {
        if (xmodem->buf != NULL && xmodem->total_len != 0)
        {
            xmodem->state = WRITE_BLOCK;
        }
        break;
    }
    case WAIT_FOR_C_ACK:
    {
        ESP_LOGD(TAG, "WAIT_FOR_C_ACK");
        if (sscma_client_io_available(xmodem->io, &rlen) == ESP_OK && rlen)
        {
            sscma_client_io_read(xmodem->io, &response, 1);
            xmodem->cur_time = esp_timer_get_time();
            switch (response)
            {
            case XACK:
            {
                ESP_LOGD(TAG, "ACK");
                xmodem->state = C_ACK_RECEIVED;
                break;
            }
            case XNACK:
            {
                ESP_LOGD(TAG, "NACK");
                xmodem->state = WRITE_BLOCK_FAILED;
                break;
            }
            case XEOF:
            {
                ESP_LOGD(TAG, "EOF");
                xmodem->state = COMPLETE;
                break;
            }
            default:
                break;
            }
        }
        else if (xmodem_timeout(xmodem, TRANSFER_ACK_TIMEOUT))
        {
            xmodem->state = WRITE_BLOCK_TIMEOUT;
        }
        break;
    }
    case WRITE_BLOCK_FAILED:
    {
        ESP_LOGD(TAG, "WRITE_BLOCK_FAILED");
        if (xmodem->write_block_retries > WRITE_BLOCK_MAX_RETRIES)
        {
            xmodem->state = ABORT_TRANSFER;
        }
        else
        {
            xmodem->state = WRITE_BLOCK;
            xmodem->cur_packet_id--;
            xmodem->xfer_len -= xmodem->xfer_size;
            xmodem->write_block_retries++;
        }
        break;
    }
    case C_ACK_RECEIVED:
    {
        ESP_LOGD(TAG, "C_ACK_RECEIVED");
        if (xmodem->xfer_len >= xmodem->total_len)
        {
            xmodem->total_len = 0;
            xmodem->buf = NULL;
            xmodem->state = WAIT_WRITE_BLOCK;
        }
        else
        {
            xmodem->write_block_retries = 0;
            xmodem->state = WRITE_BLOCK;
        }

        break;
    }
    case WRITE_EOT:
    {
        ESP_LOGD(TAG, "WRITE_EOT");
        ctrl = XEOT;
        sscma_client_io_write(xmodem->io, (uint8_t *)&ctrl, sizeof(char));
        xmodem->state = WAIT_FOR_EOT_ACK;
        break;
    }
    case WAIT_FOR_EOT_ACK:
    {
        ESP_LOGD(TAG, "WAIT_FOR_EOT_ACK");
        if (sscma_client_io_available(xmodem->io, &rlen) == ESP_OK && rlen)
        {
            sscma_client_io_read(xmodem->io, &response, 1);
            switch (response)
            {
            case XACK:
                xmodem->state = COMPLETE;
                break;
            case XNACK:
                xmodem->state = ABORT_TRANSFER;
                break;
            default:
                break;
            }
        }
        else if (xmodem_timeout(xmodem, TRANSFER_EOT_TIMEOUT))
        {
            xmodem->state = TIMEOUT_EOT;
        }
        break;
    }
    case COMPLETE:
    {
        ESP_LOGD(TAG, "COMPLETE");
        ctrl = XEOT;
        sscma_client_io_write(xmodem->io, (uint8_t *)&ctrl, sizeof(char));
        xmodem->state = FINAL;
        break;
    }
    case ABORT_TRANSFER:
    {
        ESP_LOGD(TAG, "ABORT_TRANSFER");
        ctrl = XCAN;
        sscma_client_io_write(xmodem->io, (uint8_t *)&ctrl, sizeof(char));
        xmodem->state = FAILED;
        break;
    }
    case TIMEOUT_EOT:
    {
        ESP_LOGD(TAG, "TIMEOUT_EOT");
        xmodem->state = ABORT_TRANSFER;
        break;
    }
    case WRITE_BLOCK_TIMEOUT:
    {
        ESP_LOGD(TAG, "TIMEOUT_EOT");
        xmodem->state = WRITE_BLOCK_FAILED;
        break;
    }
    default:
    {
        xmodem->state = ABORT_TRANSFER;
        break;
    }
    }

    return xmodem->state;
}

esp_err_t sscma_client_proto_xmodem_create(sscma_client_io_t *io, sscma_client_proto_handle_t *ret_proto)
{
    sscma_client_proto_xmodem_t *xmodem = NULL;

    ESP_RETURN_ON_FALSE(io && ret_proto, ESP_ERR_INVALID_ARG, TAG, "invalid argument");

    xmodem = (sscma_client_proto_xmodem_t *)calloc(1, sizeof(sscma_client_proto_xmodem_t));

    ESP_RETURN_ON_FALSE(xmodem, ESP_ERR_NO_MEM, TAG, "no mem for xmodem");

    xmodem->io = io;
    xmodem->state = INITIAL;
    xmodem->cur_packet_id = 0;
    xmodem->cur_time = esp_timer_get_time();

    xmodem->base.start = sscma_client_proto_xmodem_start;
    xmodem->base.write = sscma_client_proto_xmodem_write;
    xmodem->base.finish = sscma_client_proto_xmodem_finish;
    xmodem->base.abort = sscma_client_proto_xmodem_abort;
    xmodem->base.del = sscma_client_proto_xmodem_del;

    xmodem->lock = xSemaphoreCreateMutex();
    if (xmodem->lock == NULL)
    {
        free(xmodem);
        return ESP_ERR_NO_MEM;
    }

    *ret_proto = &xmodem->base;

    return ESP_OK;
}

esp_err_t sscma_client_proto_xmodem_del(sscma_client_proto_handle_t proto)
{
    sscma_client_proto_xmodem_t *xmodem = __containerof(proto, sscma_client_proto_xmodem_t, base);
    if (xmodem->lock)
    {
        vSemaphoreDelete(xmodem->lock);
    }
    free(xmodem);
    return ESP_OK;
}

esp_err_t sscma_client_proto_xmodem_start(sscma_client_proto_handle_t proto)
{
    esp_err_t ret = ESP_OK;
    sscma_client_proto_xmodem_t *xmodem = __containerof(proto, sscma_client_proto_xmodem_t, base);

    xSemaphoreTake(xmodem->lock, portMAX_DELAY);

    xmodem->state = INITIAL;
    xmodem->cur_packet_id = 1;
    xmodem->buf = NULL;
    xmodem->total_len = 0;
    xmodem->xfer_len = 0;
    xmodem->xfer_size = 0;
    xmodem->cur_time = esp_timer_get_time();
    do
    {
        xmodem_process(xmodem);
        if (xmodem->state == WRITE_BLOCK)
        {
            ret = ESP_OK;
            break;
        }
        if (xmodem->state == WAIT_FOR_C_TIMEOUT)
        {
            ret = ESP_ERR_TIMEOUT;
            break;
        }
    } while ((esp_timer_get_time() - xmodem->cur_time) / 1000 < TRANSFER_ACK_TIMEOUT);

    xSemaphoreGive(xmodem->lock);

    return ret;
}

esp_err_t sscma_client_proto_xmodem_write(sscma_client_proto_handle_t proto, const void *data, size_t len)
{
    esp_err_t ret = ESP_OK;
    sscma_client_proto_xmodem_t *xmodem = __containerof(proto, sscma_client_proto_xmodem_t, base);

    xSemaphoreTake(xmodem->lock, portMAX_DELAY);

    xmodem->buf = data;
    xmodem->xfer_len = 0;
    xmodem->xfer_size = 0;
    xmodem->total_len = len;
    do
    {
        xmodem_process(xmodem);
        if (xmodem->state == WAIT_WRITE_BLOCK)
        {
            ret = ESP_OK;
            break;
        }
        if (xmodem->state == WRITE_BLOCK_TIMEOUT)
        {
            ret = ESP_ERR_TIMEOUT;
            break;
        }
        if (xmodem->state == FAILED)
        {
            ret = ESP_FAIL;
            break;
        }
    } while (1);

    xSemaphoreGive(xmodem->lock);

    return ret;
}

esp_err_t sscma_client_proto_xmodem_finish(sscma_client_proto_handle_t proto)
{
    esp_err_t ret = ESP_OK;
    sscma_client_proto_xmodem_t *xmodem = __containerof(proto, sscma_client_proto_xmodem_t, base);

    xSemaphoreTake(xmodem->lock, portMAX_DELAY);

    xmodem->state = WRITE_EOT;
    xmodem->cur_packet_id = -1;
    xmodem->buf = NULL;
    xmodem->total_len = 0;
    xmodem->xfer_len = 0;
    xmodem->xfer_size = 0;
    do
    {
        xmodem_process(xmodem);
        if (xmodem->state == COMPLETE)
        {
            ret = ESP_OK;
            break;
        }
        if (xmodem->state == WRITE_BLOCK_TIMEOUT)
        {
            ret = ESP_ERR_TIMEOUT;
            break;
        }
        if (xmodem->state == FAILED)
        {
            ret = ESP_FAIL;
            break;
        }
    } while (1);

    xSemaphoreGive(xmodem->lock);

    return ret;
}

esp_err_t sscma_client_proto_xmodem_abort(sscma_client_proto_handle_t proto)
{
    esp_err_t ret = ESP_OK;
    sscma_client_proto_xmodem_t *xmodem = __containerof(proto, sscma_client_proto_xmodem_t, base);

    xSemaphoreTake(xmodem->lock, portMAX_DELAY);

    xmodem->state = ABORT_TRANSFER;
    xmodem->buf = NULL;
    xmodem->total_len = 0;
    xmodem->xfer_len = 0;
    xmodem->xfer_size = 0;

    do
    {
        xmodem_process(xmodem);
        if (xmodem->state == FINAL)
        {
            ret = ESP_OK;
            break;
        }
        if (xmodem->state == TIMEOUT_EOT)
        {
            ret = ESP_ERR_TIMEOUT;
            break;
        }
        if (xmodem->state == FAILED)
        {
            ret = ESP_FAIL;
            break;
        }
    } while (1);

    xSemaphoreGive(xmodem->lock);
    return ret;
}
