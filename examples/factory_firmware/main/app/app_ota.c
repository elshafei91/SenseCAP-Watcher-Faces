
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"

#include "app_ota.h"
#include "data_defs.h"
#include "event_loops.h"
#include "util.h"

#define HTTPS_TIMEOUT_MS                30000
#define HTTPS_DOWNLOAD_RETRY_TIMES      5

enum {
    OTA_TYPE_ESP32 = 1,
    OTA_TYPE_HIMAX,
    OTA_TYPE_AI_MODEL
};


static const char *TAG = "ota";

static TaskHandle_t g_task;
static StaticTask_t g_task_tcb;
static bool g_ota_running = false;
static uint8_t network_connect_flag = 0;

static SemaphoreHandle_t g_sem_network;
static char *g_url;




static int cmp_versions ( const char * version1, const char * version2 ) {
	unsigned major1 = 0, minor1 = 0, bugfix1 = 0;
	unsigned major2 = 0, minor2 = 0, bugfix2 = 0;
	sscanf(version1, "%u.%u.%u", &major1, &minor1, &bugfix1);
	sscanf(version2, "%u.%u.%u", &major2, &minor2, &bugfix2);
	if (major1 < major2) return -1;
	if (major1 > major2) return 1;
	if (minor1 < minor2) return -1;
	if (minor1 > minor2) return 1;
	if (bugfix1 < bugfix2) return -1;
	if (bugfix1 > bugfix2) return 1;
	return 0;
}

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "New firmware version: %s", new_app_info->version);

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    } else {
        ESP_LOGW(TAG, "Failed to get running_app_info! Always do OTA.");
    }

    int res = cmp_versions(new_app_info->version, running_app_info.version);

    if (res <= 0) return ESP_ERR_OTA_VERSION_TOO_OLD;

    return ESP_OK;
}

static esp_err_t __http_client_init_cb(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_OK;
    /* Uncomment to add custom headers to HTTP request */
    // err = esp_http_client_set_header(http_client, "Custom-Header", "Value");
    return err;
}

static void __do_esp32_ota()
{
    ESP_LOGI(TAG, "starting esp32 ota process ...");

    esp_err_t ota_finish_err = ESP_OK;

    esp_http_client_config_t config = {
        .url = g_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTPS_TIMEOUT_MS,
        .keep_alive_enable = true,
    };
#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
#endif

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
        .http_client_init_cb = __http_client_init_cb, // Register a callback to be invoked after esp_http_client is initialized
        .partial_http_download = true,
        .max_http_request_size = MBEDTLS_SSL_IN_CONTENT_LEN,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err;
    int i;
    struct view_data_ota_status ota_status;

    for (i = 0; i < HTTPS_DOWNLOAD_RETRY_TIMES; i++)
    {
        err = esp_https_ota_begin(&ota_config, &https_ota_handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp32 ota begin failed [%d]", i);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else break;
    }

    if (i >= HTTPS_DOWNLOAD_RETRY_TIMES) {
        ESP_LOGE(TAG, "esp32 ota begin failed eventually");
        ota_status.status = OTA_STATUS_FAIL;
        ota_status.err_code = ESP_ERR_OTA_CONNECTION_FAIL;
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_OTA_ESP32_FW, 
                                    &ota_status, sizeof(struct view_data_ota_status),
                                    portMAX_DELAY);
        return;
    }

    ESP_LOGI(TAG, "esp32 ota connection established, start downloading ...");
    ota_status.status = OTA_STATUS_DOWNLOADING;
    ota_status.percentage = 0;
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_OTA_ESP32_FW, 
                        &ota_status, sizeof(struct view_data_ota_status),
                        portMAX_DELAY);

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp32 ota, esp_https_ota_get_img_desc failed");
        ota_status.status = OTA_STATUS_FAIL;
        ota_status.err_code = ESP_ERR_OTA_GET_IMG_HEADER_FAIL;
        goto ota_end;
    }
    err = validate_image_header(&app_desc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp32 ota, validate new firmware failed");
        ota_status.status = OTA_STATUS_FAIL;
        ota_status.err_code = err;
        goto ota_end;
    }

    int total_bytes, read_bytes = 0, last_report_bytes = 0;
    int step_bytes;

    total_bytes = esp_https_ota_get_image_size(https_ota_handle);
    step_bytes = (int)(total_bytes / 10);
    last_report_bytes = step_bytes;

    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        // esp_https_ota_perform returns after every read operation which gives user the ability to
        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
        // data read so far.
        read_bytes = esp_https_ota_get_image_len_read(https_ota_handle);
        if (read_bytes >= last_report_bytes) {
            ota_status.status = OTA_STATUS_DOWNLOADING;
            ota_status.percentage = (int)(100 * read_bytes / total_bytes);
            ota_status.err_code = ESP_OK;
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_OTA_ESP32_FW, 
                                &ota_status, sizeof(struct view_data_ota_status),
                                portMAX_DELAY);
            last_report_bytes += step_bytes;
            ESP_LOGD(TAG, "esp32 ota, image bytes read: %d, %d%%", read_bytes, ota_status.percentage);
        }
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG, "esp32 ota, complete data was not received.");
        ota_status.status = OTA_STATUS_FAIL;
        ota_status.err_code = ESP_ERR_OTA_DOWNLOAD_FAIL;
    } else {
        ota_finish_err = esp_https_ota_finish(https_ota_handle);
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            ESP_LOGI(TAG, "esp32 ota, upgrade successful. Rebooting ...");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            // TODO: call mqtt client to report ota status, better do blocking call
            esp_restart();
            //return;
        } else {
            ota_status.status = OTA_STATUS_FAIL;
            ota_status.err_code = ESP_ERR_OTA_DOWNLOAD_FAIL;
            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
                ESP_LOGE(TAG, "esp32 ota, image validation failed, image is corrupted");
            }
            ESP_LOGE(TAG, "esp32 ota, upgrade failed when trying to finish: 0x%x", ota_finish_err);
        }
    }

ota_end:
    esp_https_ota_abort(https_ota_handle);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_OTA_ESP32_FW, 
                        &ota_status, sizeof(struct view_data_ota_status),
                        portMAX_DELAY);
}

static void __app_ota_task(void *p_arg)
{
    uint32_t ota_type;

    ESP_LOGI(TAG, "starting ota task ...");

    while (1) {
        // wait for network connection
        xSemaphoreTake(g_sem_network, pdMS_TO_TICKS(10000));
        if (!network_connect_flag)
        {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(30000));  //give the time to more important tasks right after the network is established

        do {
            ota_type = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            g_ota_running = true;
            if (ota_type == OTA_TYPE_ESP32) {
                __do_esp32_ota();
            } else if (ota_type == OTA_TYPE_HIMAX) {

            } else if (ota_type == OTA_TYPE_AI_MODEL) {

            } else {
                ESP_LOGW(TAG, "unknown ota type: %" PRIu32, ota_type);
            }
            g_ota_running = false;

        } while (network_connect_flag);
    }
}

/* Event handler for catching system events */
static void __sys_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT) {
        switch (event_id) {
            case ESP_HTTPS_OTA_START:
                ESP_LOGI(TAG, "OTA started");
                break;
            case ESP_HTTPS_OTA_CONNECTED:
                ESP_LOGI(TAG, "Connected to server");
                break;
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                ESP_LOGI(TAG, "Reading Image Description");
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                ESP_LOGI(TAG, "Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_DECRYPT_CB:
                ESP_LOGI(TAG, "Callback to decrypt function");
                break;
            case ESP_HTTPS_OTA_WRITE_FLASH:
                ESP_LOGD(TAG, "Writing to flash: %d written", *(int *)event_data);
                break;
            case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
                ESP_LOGI(TAG, "Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
                break;
            case ESP_HTTPS_OTA_FINISH:
                ESP_LOGI(TAG, "OTA finish");
                break;
            case ESP_HTTPS_OTA_ABORT:
                ESP_LOGI(TAG, "OTA abort");
                break;
        }
    }
}

static void __app_event_handler(void *handler_args, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    //wifi connection state changed
    case VIEW_EVENT_WIFI_ST:
    {
        ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
        struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
        network_connect_flag = p_st->is_network ? 1 : 0;
        xSemaphoreGive(g_sem_network);
        break;
    }
    default:
        break;
    }
}


esp_err_t app_ota_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    g_sem_network = xSemaphoreCreateBinary();

    const uint32_t stack_size = 4 * 1024;
    StackType_t *task_stack = (StackType_t *)psram_alloc(stack_size);
    g_task = xTaskCreateStatic(__app_ota_task, "app_ota_task", stack_size, NULL, 1, task_stack, &g_task_tcb);

    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, __sys_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                                             __app_event_handler, NULL, NULL));

    return ESP_OK;
}

esp_err_t app_ota_ai_model_download(char *url, int size_bytes)
{
    if (g_ota_running) return ESP_ERR_OTA_ALREADY_RUNNING;



    return ESP_OK;
}

esp_err_t app_ota_esp32_fw_download(char *url)
{
    if (g_ota_running) return ESP_ERR_OTA_ALREADY_RUNNING;

    g_url = url;

    if (xTaskNotify(g_task, OTA_TYPE_ESP32, eSetValueWithoutOverwrite) == pdFAIL) {
        return ESP_ERR_OTA_ALREADY_RUNNING;
    }

    return ESP_OK;
}

esp_err_t app_ota_himax_fw_download(char *url)
{
    if (g_ota_running) return ESP_ERR_OTA_ALREADY_RUNNING;

    return ESP_OK;
}

