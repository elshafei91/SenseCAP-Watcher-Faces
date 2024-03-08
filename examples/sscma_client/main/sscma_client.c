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
#include <driver/usb_serial_jtag.h>
static const char *TAG = "main";

/* SPI settings */
#define EXAMPLE_SSCMA_SPI_NUM (SPI2_HOST)
#define EXAMPLE_SSCMA_SPI_CLK_HZ (12 * 1000 * 1000)

/* SPI pins */
#define EXAMPLE_SSCMA_SPI_SCLK (7)
#define EXAMPLE_SSCMA_SPI_MOSI (8)
#define EXAMPLE_SSCMA_SPI_MISO (9)
#define EXAMPLE_SSCMA_SPI_CS (2)
#define EXAMPLE_SSCMA_SPI_SYNC (1)

#define EXAMPLE_SSCMA_RESET (4)

/* I2C settings */
#define EXAMPLE_SSCMA_I2C_NUM (0)
#define EXAMPLE_SSCMA_I2C_CLK_HZ (400000)

/* I2C pins */
#define EXAMPLE_SSCMA_I2C_SCL (6)
#define EXAMPLE_SSCMA_I2C_SDA (5)

char usb_buf[2 * 1024];
char sscma_buf[64 * 1024];
sscma_client_io_handle_t io = NULL;
sscma_client_handle_t client = NULL;

void on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    if (reply->len >= 100)
    {
        strcpy(&reply->data[100 - 4], "...");
    }
    // Note: reply is automatically recycled after exiting the function.

    char *img = NULL;
    int img_size = 0;
    if (sscma_utils_fetch_image_from_reply(reply, &img, &img_size) == ESP_OK)
    {
        printf("image_size: %d\n", img_size);
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
                printf("[box %d]: x=%d, y=%d, w=%d, h=%d, score=%d, target=%d\n", i, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, boxes[i].score, boxes[i].target);
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
                printf("[class %d]: target=%d, score=%d\n", i, classes[i].target, classes[i].score);
            }
        }
        free(classes);
    }
}

char t_img[32 * 1024] = {0};
sscma_client_box_t t_boxed[10];
sscma_client_class_t t_classes[10];
void on_event_debug(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    if (reply->len >= 100)
    {
        strcpy(&reply->data[100 - 4], "...");
    }
    // Note: reply is automatically recycled after exiting the function.

    int img_size = 0;
    if (sscma_utils_prase_image_from_reply(reply, t_img, sizeof(t_img), &img_size) == ESP_OK)
    {
        printf("image_size: %d\n", img_size);
    }
    int box_count = 0;
    if (sscma_utils_prase_boxes_from_reply(reply, &t_boxed, 10, &box_count) == ESP_OK)
    {
        if (box_count > 0)
        {
            for (int i = 0; i < box_count; i++)
            {
                printf("[box %d]: x=%d, y=%d, w=%d, h=%d, score=%d, target=%d\n", i, t_boxed[i].x, t_boxed[i].y, t_boxed[i].w, t_boxed[i].h, t_boxed[i].score, t_boxed[i].target);
            }
        }
    }

    int class_count = 0;
    if (sscma_utils_prase_classes_from_reply(reply, &t_classes, 10, &class_count) == ESP_OK)
    {
        if (class_count > 0)
        {
            for (int i = 0; i < class_count; i++)
            {
                printf("[class %d]: target=%d, score=%d\n", i, t_classes[i].target, t_classes[i].score);
            }
        }
    }
}

void on_log(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    if (reply->len >= 100)
    {
        strcpy(&reply->data[100 - 4], "...");
    }
    // Note: reply is automatically recycled after exiting the function.
    printf("log: %s\n", reply->data);
}

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

    // sscma_client_new_io_uart_bus((sscma_client_uart_bus_handle_t)0, &io_uart_config, &io);

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
        .wait_delay = 2,
    };

    // sscma_client_new_io_i2c_bus((sscma_client_i2c_bus_handle_t)0, &io_i2c_config, &io);

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
    };

    sscma_client_new_io_spi_bus((sscma_client_spi_bus_handle_t)EXAMPLE_SSCMA_SPI_NUM, &spi_io_config, &io);

    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 32768,
        .rx_buffer_size = 4096,
    };

    usb_serial_jtag_driver_install(&config);
    size_t rlen = 0;
    char rbuf[32] = {0};

    sscma_client_config_t sscma_client_config = SSCMA_CLIENT_CONFIG_DEFAULT();
    sscma_client_config.reset_gpio_num = EXAMPLE_SSCMA_RESET;

    ESP_ERROR_CHECK(sscma_client_new(io, &sscma_client_config, &client));
    const sscma_client_callback_t callback = {
        .on_event = on_event_debug,
        .on_log = on_log,
    };

    if (sscma_client_register_callback(client, &callback, NULL) != ESP_OK)
    {
        printf("set callback failed\n");
        abort();
    }
    sscma_client_init(client);

    while (1)
    {
        do
        {
            rlen = usb_serial_jtag_read_bytes(rbuf, sizeof(rbuf), 1 / portTICK_PERIOD_MS);
            if (rlen)
            {
                // sscma_client_write(client, rbuf, rlen);
                // sscma_client_reply_t reply;
                // // Notice: Requested reply need to be manually recycled
                // sscma_client_request(client, "AT+ID?\r\n", &reply, true, portMAX_DELAY);
                // sscma_client_reply_clear(&reply);
                // sscma_client_request(client, "AT+INFO=\"asdf\"\r\n", &reply, true, portMAX_DELAY);
                // sscma_client_reply_clear(&reply);
                // sscma_client_request(client, "AT+ID1?\r\n", &reply, true, portMAX_DELAY);
                // sscma_client_reply_clear(&reply);
                // sscma_client_request(client, "AT+INVOKE=-1,0,0\r\n", &reply, true, portMAX_DELAY);
                // // sscma_client_request(client, "AT+SAMPLE=1\r\n", &reply, true, portMAX_DELAY);
                // sscma_client_reply_clear(&reply);
                sscma_client_info_t *info;
                if (sscma_client_get_info(client, &info, true) == ESP_OK)
                {
                    printf("ID: %s\n", (info->id != NULL) ? info->id : "NULL");
                    printf("Name: %s\n", (info->name != NULL) ? info->name : "NULL");
                    printf("Hardware Version: %s\n", (info->hw_ver != NULL) ? info->hw_ver : "NULL");
                    printf("Software Version: %s\n", (info->sw_ver != NULL) ? info->sw_ver : "NULL");
                    printf("Firmware Version: %s\n", (info->fw_ver != NULL) ? info->fw_ver : "NULL");
                }
                else
                {
                    printf("get info failed\n");
                }
                sscma_client_model_t *model;
                if (sscma_client_get_model(client, &model, true) == ESP_OK)
                {
                    printf("ID: %s\n", model->id ? model->id : "N/A");
                    printf("Name: %s\n", model->name ? model->name : "N/A");
                    printf("Version: %s\n", model->ver ? model->ver : "N/A");
                    printf("Category: %s\n", model->category ? model->category : "N/A");
                    printf("Algorithm: %s\n", model->algorithm ? model->algorithm : "N/A");
                    printf("Description: %s\n", model->description ? model->description : "N/A");

                    printf("Classes:\n");
                    if (model->classes[0] != NULL)
                    {
                        for (int i = 0; model->classes[i] != NULL; i++)
                        {
                            printf("  - %s\n", model->classes[i]);
                        }
                    }
                    else
                    {
                        printf("  N/A\n");
                    }

                    printf("Token: %s\n", model->token ? model->token : "N/A");
                    printf("URL: %s\n", model->url ? model->url : "N/A");
                    printf("Manufacturer: %s\n", model->manufacturer ? model->manufacturer : "N/A");
                }
                else
                {
                    printf("get model failed\n");
                }
                sscma_client_break(client);
                sscma_client_set_sensor(client, 1, 2, true);
                // vTaskDelay(50 / portTICK_PERIOD_MS);
                if (sscma_client_sample(client, 5) != ESP_OK)
                {
                    printf("sample failed\n");
                }
                // vTaskDelay(1000 / portTICK_PERIOD_MS);
                sscma_client_set_sensor(client, 1, 0, true);
                // vTaskDelay(50 / portTICK_PERIOD_MS);
                if (sscma_client_invoke(client, -1, false, true) != ESP_OK)
                {
                    printf("sample failed\n");
                }
            }
        } while (rlen > 0);

        // if (sscma_client_available(client, &rlen) == ESP_OK && rlen)
        // {
        //     if (rlen > sizeof(sscma_buf))
        //     {
        //         rlen = sizeof(sscma_buf);
        //     }
        //     // printf("len:%d\n", rlen);
        //     sscma_client_read(client, sscma_buf, rlen);
        //     usb_serial_jtag_write_bytes(sscma_buf, rlen, portMAX_DELAY);
        // }
        printf("free_heap_size = %ld\n", esp_get_free_heap_size());
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
