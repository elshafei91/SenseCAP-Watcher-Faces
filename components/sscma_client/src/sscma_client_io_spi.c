// #include <stdlib.h>
// #include <string.h>
// #include <sys/cdefs.h>
// #include "sdkconfig.h"

// #include "sscma_client_io_interface.h"
// #include "sscma_client_io.h"
// #include "driver/spi.h"
// #include "driver/gpio.h"
// #include "esp_log.h"
// #include "esp_check.h"

// static const char *TAG = "sscma_client.io.spi";

// #define HEADER_LEN (uint8_t)4
// #define MAX_PL_LEN (uint8_t)4095
// #define CHECKSUM_LEN (uint8_t)2

// #define PACKET_SIZE (uint16_t)(256)

// #define FEATURE_TRANSPORT 0x10
// #define FEATURE_TRANSPORT_CMD_READ 0x01
// #define FEATURE_TRANSPORT_CMD_WRITE 0x02
// #define FEATURE_TRANSPORT_CMD_AVAILABLE 0x03
// #define FEATURE_TRANSPORT_CMD_START 0x04
// #define FEATURE_TRANSPORT_CMD_STOP 0x05
// #define FEATURE_TRANSPORT_CMD_RESET 0x06

// static esp_err_t client_io_spi_del(sscma_client_io_t *io);
// static esp_err_t client_io_spi_write(sscma_client_io_t *io, const void *data, size_t len);
// static esp_err_t client_io_spi_read(sscma_client_io_t *io, void *data, size_t len);
// static esp_err_t client_io_spi_available(sscma_client_io_t *io, size_t *len);

// typedef struct
// {
//     sscma_client_io_t base;
//     uint32_t spi_bus_id;         // SPI bus id, indicating which SPI port
//     uint32_t dev_addr;           // Device address
//     void *user_ctx;              // User context
//     uint8_t buffer[PACKET_SIZE]; // SPI packet buffer
// } sscma_client_io_spi_t;

// esp_err_t sscma_client_new_io_spi_bus(sscma_client_spi_bus_handle_t bus, sscma_client_io_spi_config_t *io_config, sscma_client_io_handle_t *ret_io)
// {
//     esp_err_t ret = ESP_OK;
//     sscma_client_io_spi_t *spi_client_io = NULL;
//     ESP_GOTO_ON_FALSE(io_config && ret_io, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
//     spi_client_io = (sscma_client_io_spi_t *)calloc(1, sizeof(sscma_client_io_spi_t));
//     ESP_GOTO_ON_FALSE(spi_client_io, ESP_ERR_NO_MEM, err, TAG, "no mem for spi client io");

//     spi_client_io->spi_bus_id = (uint32_t)bus;
//     spi_client_io->dev_addr = io_config->dev_addr;
//     spi_client_io->user_ctx = io_config->user_ctx;
//     spi_client_io->base.del = client_io_spi_del;
//     spi_client_io->base.write = client_io_spi_write;
//     spi_client_io->base.read = client_io_spi_read;
//     spi_client_io->base.available = client_io_spi_available;

//     *ret_io = &spi_client_io->base;
//     ESP_LOGD(TAG, "new spi sscma client io @%p", spi_client_io);

//     return ESP_OK;

// err:
//     if (spi_client_io)
//     {
//         free(spi_client_io);
//     }
//     return ret;
// }

// static esp_err_t client_io_spi_del(sscma_client_io_t *io)
// {
//     esp_err_t ret = ESP_OK;
//     sscma_client_io_spi_t *spi_client_io = __containerof(io, sscma_client_io_spi_t, base);

//     ESP_LOGD(TAG, "del spi sscma client io @%p", spi_client_io);
//     free(spi_client_io);
//     return ret;
// }

// static esp_err_t client_io_spi_write(sscma_client_io_t *io, const void *data, size_t len)
// {
//     esp_err_t ret = ESP_OK;

//     sscma_client_io_spi_t *spi_client_io = __containerof(io, sscma_client_io_spi_t, base);
//     uint16_t packets = len / MAX_PL_LEN;
//     uint16_t remain = len % MAX_PL_LEN;
//     if (data)
//     {
//         for (uint16_t i = 0; i < packets; i++)
//         {
//         }

//         if (remain)
//         {
//         }
//     }

//     return ret;
// }

// static esp_err_t client_io_spi_read(sscma_client_io_t *io, void *data, size_t len)
// {
//     esp_err_t ret = ESP_OK;

//     sscma_client_io_spi_t *spi_client_io = __containerof(io, sscma_client_io_spi_t, base);
//     uint16_t packets = len / MAX_PL_LEN;
//     uint16_t remain = len % MAX_PL_LEN;

//     if (data)
//     {

//         for (uint16_t i = 0; i < packets; i++)
//         {
//         }
//     }

//     return ret;
// }

// static esp_err_t client_io_spi_available(sscma_client_io_t *io, size_t *len)
// {

//     esp_err_t ret = ESP_OK;
//     sscma_client_io_spi_t *spi_client_io = __containerof(io, sscma_client_io_spi_t, base);

//     spi_client_io->buffer[0] = FEATURE_TRANSPORT;
//     spi_client_io->buffer[1] = FEATURE_TRANSPORT_CMD_AVAILABLE;
//     spi_client_io->buffer[2] = 0x00;
//     spi_client_io->buffer[3] = 0x00;
//     spi_client_io->buffer[4] = 0xFF;
//     spi_client_io->buffer[5] = 0xFF;

//     *len = 0; //(spi_client_io->buffer[0] << 8) | spi_client_io->buffer[1];

//     return ret;
// }
