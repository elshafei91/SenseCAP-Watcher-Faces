#include "esp_check.h"
#include "sscma_client_io.h"
#include "sscma_client_io_interface.h"

static const char *TAG = "sscma_client.io";

esp_err_t sscma_client_io_del(sscma_client_io_t *io)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return io->del(io);
}

esp_err_t sscma_client_io_write(sscma_client_io_t *io, const void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    assert(len > 0);
    assert(data);
    return io->write(io, data, len);
}

esp_err_t sscma_client_io_read(sscma_client_io_t *io, void *data, size_t len)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    assert(len > 0);
    assert(data);
    return io->read(io, data, len);
}

esp_err_t sscma_client_io_available(sscma_client_io_t *io, size_t *ret_avail)
{
    ESP_RETURN_ON_FALSE(io, ESP_ERR_INVALID_ARG, TAG, "invalid argument");
    return io->available(io, ret_avail);
}
