#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sscma_client_io.h"
#include "sscma_client_ops.h"
#include "indoor_ai_camera.h"
#include <driver/usb_serial_jtag.h>
static const char *TAG = "main";

/* SPI settings */
#define EXAMPLE_SSCMA_SPI_NUM (SPI2_HOST)
#define EXAMPLE_SSCMA_SPI_CLK_HZ (12 * 1000 * 1000)

/* SPI pins */
#define EXAMPLE_SSCMA_SPI_SCLK (4)
#define EXAMPLE_SSCMA_SPI_MOSI (5)
#define EXAMPLE_SSCMA_SPI_MISO (6)
#define EXAMPLE_SSCMA_SPI_CS (21)
#define EXAMPLE_SSCMA_SPI_SYNC (IO_EXPANDER_PIN_NUM_6)

/* UART pins */
#define EXAMPLE_SSCMA_UART_NUM (1)
#define EXAMPLE_SSCMA_UART_TX (17)
#define EXAMPLE_SSCMA_UART_RX (18)
#define EXAMPLE_SSCMA_UART_BAUD_RATE (921600)

#define EXAMPLE_SSCMA_RESET (BSP_PWR_AI_CHIP)

char usb_buf[2 * 1024];
char sscma_buf[64 * 1024];
sscma_client_io_handle_t io = NULL;
sscma_client_io_handle_t io_ota = NULL;
sscma_client_handle_t client = NULL;
esp_io_expander_handle_t io_expander = NULL;

void on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    // Note: reply is automatically recycled after exiting the function.

    char *img = NULL;
    int img_size = 0;
    if (sscma_utils_fetch_image_from_reply(reply, &img, &img_size) == ESP_OK)
    {
        ESP_LOGI(TAG, "image_size: %d\n", img_size);
        free(img);
    }
    sscma_client_box_t *boxes = NULL;
    int box_count = 0;
    if (sscma_utils_fetch_boxes_from_reply(reply, &boxes, &box_count) == ESP_OK)
    {
        if (box_count > 0)
        {
            for (int i = 0; i < box_count; i++)
            {
                ESP_LOGI(TAG, "[box %d]: x=%d, y=%d, w=%d, h=%d, score=%d, target=%d\n", i, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, boxes[i].score, boxes[i].target);
            }
        }
        free(boxes);
    }

    sscma_client_class_t *classes = NULL;
    int class_count = 0;
    if (sscma_utils_fetch_classes_from_reply(reply, &classes, &class_count) == ESP_OK)
    {
        if (class_count > 0)
        {
            for (int i = 0; i < class_count; i++)
            {
                ESP_LOGI(TAG, "[class %d]: target=%d, score=%d\n", i, classes[i].target, classes[i].score);
            }
        }
        free(classes);
    }
}

void on_log(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    if (reply->len >= 100)
    {
        strcpy(&reply->data[100 - 4], "...");
    }

    ESP_LOGI(TAG, "log: %s\n", reply->data);
}

void app_main(void)
{
    io_expander = bsp_io_expander_init();
    assert(io_expander != NULL);

    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = EXAMPLE_SSCMA_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT};
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(EXAMPLE_SSCMA_UART_NUM, 64 * 1024, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(EXAMPLE_SSCMA_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(EXAMPLE_SSCMA_UART_NUM, EXAMPLE_SSCMA_UART_TX, EXAMPLE_SSCMA_UART_RX, -1, -1));

    sscma_client_io_uart_config_t io_uart_config = {
        .user_ctx = NULL,
    };

    sscma_client_new_io_uart_bus((sscma_client_uart_bus_handle_t)EXAMPLE_SSCMA_UART_NUM, &io_uart_config, &io_ota);

    const spi_bus_config_t buscfg = {
        .sclk_io_num = EXAMPLE_SSCMA_SPI_SCLK,
        .mosi_io_num = EXAMPLE_SSCMA_SPI_MOSI,
        .miso_io_num = EXAMPLE_SSCMA_SPI_MISO,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 4095,
    };

    ESP_ERROR_CHECK(spi_bus_initialize(EXAMPLE_SSCMA_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO));

    const sscma_client_io_spi_config_t spi_io_config = {
        .sync_gpio_num = EXAMPLE_SSCMA_SPI_SYNC,
        .cs_gpio_num = EXAMPLE_SSCMA_SPI_CS,
        .pclk_hz = EXAMPLE_SSCMA_SPI_CLK_HZ,
        .spi_mode = 0,
        .wait_delay = 2,
        .user_ctx = NULL,
        .io_expander = io_expander,
        .flags.sync_use_expander = true,
    };

    sscma_client_new_io_spi_bus((sscma_client_spi_bus_handle_t)EXAMPLE_SSCMA_SPI_NUM, &spi_io_config, &io);

    sscma_client_config_t sscma_client_config = SSCMA_CLIENT_CONFIG_DEFAULT();
    sscma_client_config.reset_gpio_num = EXAMPLE_SSCMA_RESET;
    sscma_client_config.io_expander = io_expander;
    sscma_client_config.flags.reset_use_expander = true;

    ESP_ERROR_CHECK(sscma_client_new(io, &sscma_client_config, &client));
    const sscma_client_callback_t callback = {
        .on_event = on_event,
        .on_log = on_log,
    };

    if (sscma_client_register_callback(client, &callback, NULL) != ESP_OK)
    {
        ESP_LOGI(TAG, "set callback failed\n");
        abort();
    }
    sscma_client_init(client);

    sscma_client_invoke(client, -1, false, true);

    if(sscma_client_ota_start(client, io_ota, 0) != ESP_OK)
    {
        ESP_LOGI(TAG, "sscma_client_ota_start failed\n");
        abort();
    }else{
        ESP_LOGI(TAG, "sscma_client_ota_start success\n");
    }

    while (1)
    {
        ESP_LOGI(TAG, "free_heap_size = %ld\n", esp_get_free_heap_size());
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
