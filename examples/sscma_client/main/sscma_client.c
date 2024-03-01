#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "driver/i2c.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sscma_client_io.h"
#include <driver/usb_serial_jtag.h>
static const char *TAG = "main";

/* Touch settings */
#define EXAMPLE_SSCMA_I2C_NUM (0)
#define EXAMPLE_SSCMA_I2C_CLK_HZ (400000)

/* LCD touch pins */
#define EXAMPLE_SSCMA_I2C_SCL (6)
#define EXAMPLE_SSCMA_I2C_SDA (5)

char usb_buf[2 * 1024];
char sscma_buf[64 * 1024];
sscma_client_io_handle_t io = NULL;

void app_main(void)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT};
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(0, 32768, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(0, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(0, 43, 44, -1, -1));

    sscma_client_io_uart_config_t io_uart_config = {
        .user_ctx = NULL,
    };

    // sscma_client_new_io_uart_bus((sscma_client_uart_bus_handle_t)0, &io_config, &io);

    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_SSCMA_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = EXAMPLE_SSCMA_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = EXAMPLE_SSCMA_I2C_CLK_HZ};
    ESP_ERROR_CHECK(i2c_param_config(EXAMPLE_SSCMA_I2C_NUM, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(EXAMPLE_SSCMA_I2C_NUM, i2c_conf.mode, 0, 0, 0));

    sscma_client_io_i2c_config_t io_i2c_config = {
        .dev_addr = 0x62,
    };

    sscma_client_new_io_i2c_bus((sscma_client_i2c_bus_handle_t)0, &io_i2c_config, &io);

    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 32768,
        .rx_buffer_size = 4096,
    };

    usb_serial_jtag_driver_install(&config);
    size_t rlen = 0;
    char rbuf[32] = {0};

    printf("proxy start...\n");

    while (1)
    {
        do
        {
            rlen = usb_serial_jtag_read_bytes(rbuf, sizeof(rbuf), 1 / portTICK_PERIOD_MS);
            if (rlen)
            {
                usb_serial_jtag_write_bytes(rbuf, rlen, portMAX_DELAY);
                sscma_client_io_write(io, rbuf, rlen);
            }
        } while (rlen > 0);

        if (sscma_client_io_available(io, &rlen) == ESP_OK && rlen)
        {
            if (rlen > sizeof(sscma_buf))
            {
                rlen = sizeof(sscma_buf);
            }
            //printf("len:%d\n", rlen);
            sscma_client_io_read(io, sscma_buf, rlen);
            usb_serial_jtag_write_bytes(sscma_buf, rlen, portMAX_DELAY);
        }
        //vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}