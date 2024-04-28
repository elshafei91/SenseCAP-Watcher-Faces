#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_spiffs.h"

#include "sensecap-watcher.h"

static const char *TAG = "main";

sscma_client_handle_t client = NULL;
sscma_client_flasher_handle_t flasher = NULL;
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
    client = bsp_sscma_client_init();
    assert(client != NULL);
    flasher = bsp_sscma_flasher_init();
    assert(flasher != NULL);

    bsp_spiffs_init_default();

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
    int64_t start = esp_timer_get_time();
    if (sscma_client_ota_start(client, flasher, 0x000000) != ESP_OK)
    {
        ESP_LOGI(TAG, "sscma_client_ota_start failed\n");
    }
    else
    {
        ESP_LOGI(TAG, "sscma_client_ota_start success\n");
        FILE *f = fopen("/spiffs/output.img", "r");
        if (f == NULL)
        {
            ESP_LOGE(TAG, "open output.img failed\n");
        }
        else
        {
            ESP_LOGI(TAG, "open output.img success\n");
            size_t len = 0;
            char buf[128] = { 0 };
            do
            {
                memset(buf, 0, sizeof(buf));
                if (fread(buf, 1, sizeof(buf), f) <= 0)
                {
                    printf("\n");
                    break;
                }
                else
                {
                    len += sizeof(buf);
                    if (sscma_client_ota_write(client, buf, sizeof(buf)) != ESP_OK)
                    {
                        ESP_LOGI(TAG, "sscma_client_ota_write failed\n");
                        break;
                    }
                }
            }
            while (true);
            fclose(f);
        }
        sscma_client_ota_finish(client);
        ESP_LOGI(TAG, "sscma_client_ota_finish success, take %lld ms\n", esp_timer_get_time() - start);
    }

    if (sscma_client_get_info(client, &info, false) == ESP_OK)
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

    sscma_client_sample(client, -1);

    while (1)
    {
        ESP_LOGI(TAG, "free_heap_size = %ld\n", esp_get_free_heap_size());
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
