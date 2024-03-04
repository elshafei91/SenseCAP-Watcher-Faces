#include <stdlib.h>
#include <sys/cdefs.h>
#include "sdkconfig.h"
#if CONFIG_SSCMA_ENABLE_DEBUG_LOG
// The local log level must be defined before including esp_log.h
// Set the maximum log level for this source file
#define LOG_SSCMA_LEVEL ESP_LOG_DEBUG
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sscma_client_types.h"
#include "sscma_client_io.h"
#include "sscma_client_commands.h"
#include "sscma_client_ops.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

static const char *TAG = "sscma_client";

esp_err_t sscma_client_new(const sscma_client_io_handle_t io, const sscma_client_config_t *config, sscma_client_handle_t *ret_client)
{
#if CONFIG_SSCMA_ENABLE_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    esp_err_t ret = ESP_OK;
    sscma_client_handle_t client = NULL;
    ESP_GOTO_ON_FALSE(io && config && ret_client, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    client = (sscma_client_handle_t)malloc(sizeof(struct sscma_client_t));
    ESP_GOTO_ON_FALSE(client, ESP_ERR_NO_MEM, err, TAG, "no mem for sscma client");
    client->io = io;
    client->inited = false;

    if (config->reset_gpio_num >= 0)
    {
        gpio_config_t io_conf = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << config->reset_gpio_num,
        };
        ESP_GOTO_ON_ERROR(gpio_config(&io_conf), err, TAG, "configure GPIO for RST line failed");
    }

    client->rx_buffer.data = (uint8_t *)malloc(config->rx_buffer_size);
    ESP_GOTO_ON_FALSE(client->rx_buffer.data, ESP_ERR_NO_MEM, err, TAG, "no mem for rx buffer");
    client->rx_buffer.pos = 0;
    client->rx_buffer.len = config->rx_buffer_size;

    client->tx_buffer.data = (uint8_t *)malloc(config->tx_buffer_size);
    ESP_GOTO_ON_FALSE(client->tx_buffer.data, ESP_ERR_NO_MEM, err, TAG, "no mem for tx buffer");
    client->tx_buffer.pos = 0;
    client->tx_buffer.len = config->tx_buffer_size;

    client->reset_gpio_num = config->reset_gpio_num;
    client->reset_level = config->flags.reset_active_high;

    *ret_client = client;

    ESP_LOGD(TAG, "new sscma client @%p", client);

    return ESP_OK;

err:
    if (client)
    {
        if (client->rx_buffer.data)
        {
            free(client->rx_buffer.data);
        }
        if (client->tx_buffer.data)
        {
            free(client->tx_buffer.data);
        }
        if (config->reset_gpio_num >= 0)
        {
            gpio_reset_pin(client->reset_gpio_num);
        }
        free(client);
    }
    return ret;
}

esp_err_t sscma_client_del(sscma_client_handle_t client)
{
    if (client)
    {
        if (client->reset_gpio_num >= 0)
        {
            gpio_reset_pin(client->reset_gpio_num);
        }
        free(client->rx_buffer.data);
        free(client->tx_buffer.data);
        free(client);
    }
    return ESP_OK;
}

esp_err_t sscma_client_init(sscma_client_handle_t client)
{
    if (!client->inited)
    {
        sscma_client_reset(client);
        client->inited = true;
    }
    return ESP_OK;
}

esp_err_t sscma_client_reset(sscma_client_handle_t client)
{
    // perform hardware reset
    if (client->reset_gpio_num >= 0)
    {
        gpio_set_level(client->reset_gpio_num, client->reset_level);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(client->reset_gpio_num, !client->reset_level);
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    else
    {
        vTaskDelay(500 / portTICK_PERIOD_MS); // wait for sscma to be ready
    }
    return ESP_OK;
}

esp_err_t sscma_client_read(sscma_client_handle_t client, void *data, size_t size)
{
    return sscma_client_io_read(client->io, data, size);
}

esp_err_t sscma_client_write(sscma_client_handle_t client, const void *data, size_t size)
{
    return sscma_client_io_write(client->io, data, size);
}

esp_err_t sscma_client_available(sscma_client_handle_t client, size_t *ret_avail)
{
    return sscma_client_io_available(client->io, ret_avail);
}
