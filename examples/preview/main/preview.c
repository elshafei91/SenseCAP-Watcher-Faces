#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sscma_client_io.h"
#include "sscma_client_ops.h"
#include "indoor_ai_camera.h"
static const char *TAG = "main";

/* SPI settings */
#define EXAMPLE_SSCMA_SPI_NUM (SPI2_HOST)
#define EXAMPLE_SSCMA_SPI_CLK_HZ (12 * 1000 * 1000)

/* SPI pins */
#define EXAMPLE_SSCMA_SPI_SCLK (4)
#define EXAMPLE_SSCMA_SPI_MOSI (5)
#define EXAMPLE_SSCMA_SPI_MISO (6)
#define EXAMPLE_SSCMA_SPI_CS (21)
#define EXAMPLE_SSCMA_SPI_SYNC (-1)

#define EXAMPLE_SSCMA_RESET (-1)

sscma_client_io_handle_t io = NULL;
sscma_client_handle_t client = NULL;

lv_disp_t *lvgl_disp;
lv_obj_t *image;

#define IMG_WIDTH 240
#define IMG_HEIGHT 240

static lv_img_dsc_t img_dsc = {
    .header.always_zero = 0,
    .header.w = IMG_WIDTH,
    .header.h = IMG_HEIGHT,
    .data_size = 0,
    .header.cf = LV_IMG_CF_RAW_ALPHA,
    .data = NULL,
};
#define DECODED_STR_MAX_SIZE (48 * 1024)
static unsigned char decoded_str[DECODED_STR_MAX_SIZE]; // 静态分配

void display_one_image(lv_obj_t *image, const unsigned char *p_data)
{
    if (!p_data)
        return;

    size_t str_len = strlen((const char *)p_data);
    size_t output_len = 0;
    // 首先获取解码后的长度
    int decode_ret = mbedtls_base64_decode(NULL, 0, &output_len, p_data, str_len);

    if (decode_ret == MBEDTLS_ERR_BASE64_INVALID_CHARACTER)
    {
        ESP_LOGE(TAG, "Invalid character in Base64 string");
        return;
    }

    // 检查解码后的长度是否超出预期
    if (output_len == 0 || output_len > DECODED_STR_MAX_SIZE)
    {
        ESP_LOGE(TAG, "Base64 decode output size is too large or zero.");
        return;
    }

    // 执行实际的解码操作
    decode_ret = mbedtls_base64_decode(decoded_str, DECODED_STR_MAX_SIZE, &output_len, p_data, str_len);
    if (decode_ret == 0)
    {
        img_dsc.data = decoded_str;
        img_dsc.data_size = output_len; // 确保设置了正确的data_size
        lv_img_set_src(image, &img_dsc);
    }
    else if (decode_ret == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
    {
        ESP_LOGE(TAG, "Buffer too small for decoding %d bytes %d output", str_len, output_len);
        return;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to decode Base64 string, error: %d", decode_ret);
    }
}

void on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    // Note: reply is automatically recycled after exiting the function.

    char *img = NULL;
    int img_size = 0;
    if (sscma_utils_fetch_image_from_reply(reply, &img, &img_size) == ESP_OK)
    {
        if (img_size < 8 * 1024)
        {
            // lv_obj_clean(image);
            //  printf("image: %s\n", img);
            lvgl_port_lock(0);
            display_one_image(image, (const unsigned char *)img);
            lvgl_port_unlock();
        }
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
    lvgl_disp = bsp_lvgl_init();
    assert(lvgl_disp != NULL);

    // lvgl image
    image = lv_img_create(lv_scr_act());
    lv_obj_set_align(image, LV_ALIGN_CENTER);

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

    sscma_client_config_t sscma_client_config = SSCMA_CLIENT_CONFIG_DEFAULT();
    sscma_client_config.reset_gpio_num = EXAMPLE_SSCMA_RESET;

    ESP_ERROR_CHECK(sscma_client_new(io, &sscma_client_config, &client));
    const sscma_client_callback_t callback = {
        .on_event = on_event,
        .on_log = on_log,
    };

    if (sscma_client_register_callback(client, &callback, NULL) != ESP_OK)
    {
        printf("set callback failed\n");
        abort();
    }

    sscma_client_init(client);
    sscma_client_set_model(client, 1);
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
        printf("ID: %d\n", model->id ? model->id : -1);
        printf("UUID: %s\n", model->uuid ? model->uuid : "N/A");
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
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    sscma_client_set_model(client, 2);
    if (sscma_client_get_model(client, &model, true) == ESP_OK)
    {
        printf("ID: %d\n", model->id ? model->id : -1);
        printf("UUID: %s\n", model->uuid ? model->uuid : "N/A");
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

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    sscma_client_set_model(client, 3);
    if (sscma_client_get_model(client, &model, true) == ESP_OK)
    {
        printf("ID: %d\n", model->id ? model->id : -1);
        printf("UUID: %s\n", model->uuid ? model->uuid : "N/A");
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

    sscma_client_set_sensor(client, 1, 0, true);
    // vTaskDelay(50 / portTICK_PERIOD_MS);

    if (sscma_client_invoke(client, -1, false, true) != ESP_OK)
    {
        printf("sample failed\n");
    }

    while (1)
    {
        // if (sscma_client_invoke(client, 1, false, true) != ESP_OK)
        // {
        //     printf("sample failed\n");
        // }

        printf("free_heap_size = %ld\n", esp_get_free_heap_size());
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
