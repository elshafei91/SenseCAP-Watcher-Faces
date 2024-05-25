
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "cJSON.h"
#include "cJSON_Utils.h"

#include "sensecap-watcher.h"

#include "app_ota.h"
#include "data_defs.h"
#include "event_loops.h"
#include "util.h"
#include "tf_module_ai_camera.h"
#include "app_sensecraft.h"


ESP_EVENT_DEFINE_BASE(OTA_EVENT_BASE);


#define HTTPS_TIMEOUT_MS                30000
#define HTTPS_DOWNLOAD_RETRY_TIMES      5
#define SSCMA_FLASH_CHUNK_SIZE          128   //this value is copied from the `sscma_client_ota` example
#define AI_MODEL_RINGBUFF_SIZE          256

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
static SemaphoreHandle_t g_sem_ai_model_downloaded;
static SemaphoreHandle_t g_sem_worker_done;
static esp_err_t g_result_err;
static char *g_url_himax;
static char *g_url_esp32;
static char *g_cur_ota_version_esp32;
static char *g_cur_ota_version_himax;

static QueueHandle_t g_Q_ota_msg;
static QueueHandle_t g_Q_ota_status;
static RingbufHandle_t g_rb_ai_model;

static bool g_ignore_version_check = false;


static void __ota_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    ota_worker_task_data_t *worker_data = *(ota_worker_task_data_t **)event_data;

    switch(id) {
        case CMD_esp_https_ota_begin:
            worker_data->err = esp_https_ota_begin(worker_data->ota_config, worker_data->ota_handle);
            break;
        case CMD_esp_https_ota_get_img_desc:
            worker_data->err = esp_https_ota_get_img_desc(*(worker_data->ota_handle), worker_data->app_desc);
            break;
        case CMD_esp_ota_get_running_partition:
            worker_data->partition = esp_ota_get_running_partition();
            break;
        case CMD_esp_ota_get_partition_description:
            worker_data->err = esp_ota_get_partition_description(worker_data->partition, worker_data->app_desc);
            break;
        case CMD_esp_https_ota_perform:
            worker_data->err = esp_https_ota_perform(*(worker_data->ota_handle));
            break;
        case CMD_esp_https_ota_finish:
            worker_data->err = esp_https_ota_finish(*(worker_data->ota_handle));
            break;
        default:
            break;
    }

    xSemaphoreGive(g_sem_worker_done);
}

static void worker_call(ota_worker_task_data_t *worker_data, int cmd)
{
    esp_event_post_to(app_event_loop_handle, OTA_EVENT_BASE, cmd,
                      &worker_data, sizeof(ota_worker_task_data_t *),  portMAX_DELAY);
    xSemaphoreTake(g_sem_worker_done, portMAX_DELAY);
}

static int cmp_versions ( const char * version1, const char * version2 ) {
	unsigned major1 = 0, minor1 = 0, bugfix1 = 0;
	unsigned major2 = 0, minor2 = 0, bugfix2 = 0;
    if (g_ignore_version_check) return 1;
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

    ota_worker_task_data_t worker_data;

    worker_call(&worker_data, CMD_esp_ota_get_running_partition);

    //esp_partition_t *running = worker_data.partition;
    esp_app_desc_t *running_app_info = psram_calloc(1, sizeof(esp_app_desc_t));

    worker_data.app_desc = running_app_info;
    worker_call(&worker_data, CMD_esp_ota_get_partition_description);

    if (worker_data.err == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info->version);
    } else {
        ESP_LOGW(TAG, "Failed to get running_app_info! Always do OTA.");
    }

    int res = cmp_versions(new_app_info->version, running_app_info->version);
    free(running_app_info);

    if (res <= 0) return ESP_ERR_OTA_VERSION_TOO_OLD;

    return ESP_OK;
}

static void esp32_ota_process()
{
    ESP_LOGI(TAG, "starting esp32 ota process, downloading %s", g_url_esp32);

    esp_err_t ota_finish_err = ESP_OK;
    ota_worker_task_data_t worker_data;

    esp_http_client_config_t *config = psram_calloc(1, sizeof(esp_http_client_config_t));
    config->url = g_url_esp32;
    config->crt_bundle_attach = esp_crt_bundle_attach;
    config->timeout_ms = HTTPS_TIMEOUT_MS;

    // don't enable this if the cert of your OTA server needs SNI support.
    // e.g. Your cert is generated with Let's Encrypt and the CommonName is *.yourdomain.com,
    // in this case, please don't enable this, SNI needs common name check.
#ifdef CONFIG_SKIP_COMMON_NAME_CHECK
    config->skip_cert_common_name_check = true;
#endif

    esp_https_ota_config_t *ota_config = psram_calloc(1, sizeof(esp_https_ota_config_t));
    ota_config->http_config = config;

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err;
    int i;
    struct view_data_ota_status ota_status;

    for (i = 0; i < HTTPS_DOWNLOAD_RETRY_TIMES; i++)
    {
        worker_data.ota_config = ota_config;
        worker_data.ota_handle = &https_ota_handle;
        worker_call(&worker_data, CMD_esp_https_ota_begin);

        err = worker_data.err;
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp32 ota begin failed [%d]", i);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else break;
    }

    if (i >= HTTPS_DOWNLOAD_RETRY_TIMES) {
        ESP_LOGE(TAG, "esp32 ota begin failed eventually");
        ota_status.status = OTA_STATUS_FAIL;
        ota_status.err_code = ESP_ERR_OTA_CONNECTION_FAIL;
        esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_OTA_ESP32_FW,
                                    &ota_status, sizeof(struct view_data_ota_status),
                                    portMAX_DELAY);
        free(config);
        free(ota_config);
        return;
    }

    ESP_LOGI(TAG, "esp32 ota connection established, start downloading ...");
    ota_status.status = OTA_STATUS_DOWNLOADING;
    ota_status.percentage = 0;
    esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_OTA_ESP32_FW,
                        &ota_status, sizeof(struct view_data_ota_status),
                        portMAX_DELAY);

    esp_app_desc_t app_desc;
    worker_data.ota_handle = &https_ota_handle;
    worker_data.app_desc = &app_desc;
    worker_call(&worker_data, CMD_esp_https_ota_get_img_desc);

    err = worker_data.err;
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
    ESP_LOGI(TAG, "New firmware binary length: %d", total_bytes);
    step_bytes = (int)(total_bytes / 10);
    last_report_bytes = step_bytes;

    while (1) {
        worker_data.ota_handle = &https_ota_handle;
        worker_call(&worker_data, CMD_esp_https_ota_perform);
        err = worker_data.err;
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
            esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_OTA_ESP32_FW,
                                &ota_status, sizeof(struct view_data_ota_status),
                                portMAX_DELAY);
            last_report_bytes += step_bytes;
            ESP_LOGI(TAG, "esp32 ota, image bytes read: %d, %d%%", read_bytes, ota_status.percentage);
        }
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG, "esp32 ota, complete data was not received.");
        ota_status.status = OTA_STATUS_FAIL;
        ota_status.err_code = ESP_ERR_OTA_DOWNLOAD_FAIL;
    } else {
        // goto ota_end;

        worker_data.ota_handle = &https_ota_handle;
        worker_call(&worker_data, CMD_esp_https_ota_finish);
        ota_finish_err = worker_data.err;
        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
            ESP_LOGI(TAG, "esp32 ota, upgrade successful. Rebooting ...");
            //vTaskDelay(1000 / portTICK_PERIOD_MS);
            // TODO: call mqtt client to report ota status, better do blocking call
            //esp_restart();
            //return;
            ota_status.status = OTA_STATUS_SUCCEED;
            ota_status.err_code = ESP_OK;
            ota_status.percentage = 100;
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
    if (ota_status.status != OTA_STATUS_SUCCEED) {
        esp_https_ota_abort(https_ota_handle);
    }
    esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_OTA_ESP32_FW,
                        &ota_status, sizeof(struct view_data_ota_status),
                        portMAX_DELAY);
    free(config);
    free(ota_config);
}

static const char *ota_type_str(int ota_type)
{
    if (ota_type == OTA_TYPE_ESP32) return "esp32 firmware";
    else if (ota_type == OTA_TYPE_HIMAX) return "himax firmware";
    else if (ota_type == OTA_TYPE_AI_MODEL) return "ai model";
    else return "unknown ota";
}

static esp_err_t __http_event_handler(esp_http_client_event_t *evt)
{
    static int content_len, written_len, last_report_bytes, step_bytes;

    ota_http_userdata_t *userdata = evt->user_data;
    int ota_type = userdata->ota_type;
    struct view_data_ota_status ota_status;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            content_len = 0;
            written_len = 0;
            last_report_bytes = 0;
            //clear the ringbuffer
            void *tmp;
            size_t len;
            while ((tmp = xRingbufferReceiveUpTo(g_rb_ai_model, &len, 0, AI_MODEL_RINGBUFF_SIZE))) {
                vRingbufferReturnItem(g_rb_ai_model, tmp);
            }
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGV(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

            if (content_len == 0) {
                content_len = esp_http_client_get_content_length(evt->client);
                ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, content_len=%d", content_len);
                step_bytes = (int)(content_len / 10);
                last_report_bytes = step_bytes;
            }

            //push to ringbuffer
            if (xRingbufferSend(g_rb_ai_model, evt->data, evt->data_len, 0) != pdTRUE) {
                ESP_LOGW(TAG, "HTTP_EVENT_ON_DATA, ringbuffer full? this should never happen!");
            }

            UBaseType_t bytes_waiting = 0;
            vRingbufferGetInfo(g_rb_ai_model, NULL, NULL, NULL, NULL, &bytes_waiting);

            while (bytes_waiting >= SSCMA_FLASH_CHUNK_SIZE) {
                //it's certain that we have a SSCMA_FLASH_CHUNK_SIZE bytes
                int target = SSCMA_FLASH_CHUNK_SIZE;
                void *chunk = psram_calloc(1, SSCMA_FLASH_CHUNK_SIZE);
                size_t rcvlen = 0, rcvlen2 = 0;
                void *item = xRingbufferReceiveUpTo(g_rb_ai_model, &rcvlen, 0, SSCMA_FLASH_CHUNK_SIZE);
                if (!item) {
                    ESP_LOGW(TAG, "HTTP_EVENT_ON_DATA, ringbuffer insufficient? this should never happen!");
                    free(chunk);
                    break;
                }
                memcpy(chunk, item, rcvlen);
                target -= rcvlen;
                vRingbufferReturnItem(g_rb_ai_model, item);

                //rollover?
                if (target > 0) {
                    item = xRingbufferReceiveUpTo(g_rb_ai_model, &rcvlen2, 0, target);  //receive the 2nd part
                    if (!item) {
                        ESP_LOGW(TAG, "HTTP_EVENT_ON_DATA, ringbuffer insufficient? this should never happen [2]!");
                        free(chunk);
                        break;
                    }
                    memcpy(chunk + rcvlen, item, rcvlen2);
                    target -= rcvlen2;
                    vRingbufferReturnItem(g_rb_ai_model, item);
                }

                if (target != 0) ESP_LOGW(TAG, "HTTP_EVENT_ON_DATA, this really should never happen!");

                //write to sscma client
                ESP_LOGV(TAG, "HTTP_EVENT_ON_DATA, sscma_client_ota_write");
                if (sscma_client_ota_write(userdata->client, chunk, SSCMA_FLASH_CHUNK_SIZE) != ESP_OK)
                {
                    ESP_LOGW(TAG, "sscma_client_ota_write failed\n");
                    *(userdata->err) = ESP_ERR_OTA_SSCMA_WRITE_FAIL;
                } else {
                    written_len += SSCMA_FLASH_CHUNK_SIZE;
                    if (written_len >= last_report_bytes) {
                        ota_status.status = OTA_STATUS_DOWNLOADING;
                        ota_status.percentage = (int)(100 * written_len / content_len);
                        ota_status.err_code = ESP_OK;
                        int32_t eventid = ota_type == OTA_TYPE_HIMAX ? CTRL_EVENT_OTA_HIMAX_FW: CTRL_EVENT_OTA_AI_MODEL;
                        esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, eventid,
                                            &ota_status, sizeof(struct view_data_ota_status),
                                            portMAX_DELAY);
                        last_report_bytes += step_bytes;
                        ESP_LOGI(TAG, "%s ota, bytes written: %d, %d%%", ota_type_str(ota_type), written_len, ota_status.percentage);
                    }
                }

                free(chunk);

                vRingbufferGetInfo(g_rb_ai_model, NULL, NULL, NULL, NULL, &bytes_waiting);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            //there might be some bytes left in the ringbuffer, copy them out
            UBaseType_t bytes_left = 0;
            vRingbufferGetInfo(g_rb_ai_model, NULL, NULL, NULL, NULL, &bytes_left);

            if (bytes_left > 0) {
                if (bytes_left >= SSCMA_FLASH_CHUNK_SIZE) ESP_LOGW(TAG, "HTTP_EVENT_ON_FINISH, amazing!");
                ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH, byte left in ringbuffer: %d", (int)bytes_left);

                void *chunk = psram_calloc(1, SSCMA_FLASH_CHUNK_SIZE);
                void *tmp;
                size_t len, copied = 0;
                while ((tmp = xRingbufferReceiveUpTo(g_rb_ai_model, &len, 0, AI_MODEL_RINGBUFF_SIZE))) {
                    memcpy(chunk + copied, tmp, len);
                    copied += len;
                    vRingbufferReturnItem(g_rb_ai_model, tmp);
                    ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH, byte left in ringbuffer: %d, copied len: %d", (int)bytes_left, copied);
                }
                if (copied != bytes_left) ESP_LOGW(TAG, "HTTP_EVENT_ON_FINISH, amazing again!");

                //write to sscma client
                if (sscma_client_ota_write(userdata->client, chunk, SSCMA_FLASH_CHUNK_SIZE) != ESP_OK)
                {
                    ESP_LOGW(TAG, "sscma_client_ota_write failed\n");
                    *(userdata->err) = ESP_ERR_OTA_SSCMA_WRITE_FAIL;
                } else {
                    written_len += SSCMA_FLASH_CHUNK_SIZE;
                    if (written_len >= last_report_bytes) {
                        ota_status.status = OTA_STATUS_DOWNLOADING;
                        ota_status.percentage = (int)(100 * written_len / content_len);
                        ota_status.err_code = ESP_OK;
                        int32_t eventid = ota_type == OTA_TYPE_HIMAX ? CTRL_EVENT_OTA_HIMAX_FW: CTRL_EVENT_OTA_AI_MODEL;
                        esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, eventid,
                                            &ota_status, sizeof(struct view_data_ota_status),
                                            portMAX_DELAY);
                        last_report_bytes += step_bytes;
                        ESP_LOGI(TAG, "%s ota, bytes written: %d, %d%%", ota_type_str(ota_type), written_len, ota_status.percentage);
                    }
                }

                free(chunk);
            }

            sscma_client_ota_finish(userdata->client);
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT, auto redirection disabled?");
            break;
    }
    return ESP_OK;
}

static void sscma_ota_process(uint32_t ota_type)
{
    ESP_LOGI(TAG, "starting sscma ota, ota_type = %s ...", ota_type_str(ota_type));

    esp_err_t ret = ESP_OK;
    struct view_data_ota_status ota_status;
    int32_t ota_eventid = ota_type == OTA_TYPE_HIMAX ? CTRL_EVENT_OTA_HIMAX_FW: CTRL_EVENT_OTA_AI_MODEL;

    //himax interfaces
    esp_io_expander_handle_t io_expander = bsp_io_expander_init();
    assert(io_expander != NULL);

    sscma_client_handle_t sscma_client = bsp_sscma_client_init();
    assert(sscma_client != NULL);

    sscma_client_flasher_handle_t sscma_flasher = bsp_sscma_flasher_init();
    assert(sscma_flasher != NULL);

    //sscma_client_init(sscma_client);

    sscma_client_info_t *info;
    if (sscma_client_get_info(sscma_client, &info, true) == ESP_OK)
    {
        ESP_LOGI(TAG, "--------------------------------------------");
        ESP_LOGI(TAG, "           sscma client info");
        ESP_LOGI(TAG, "ID: %s", (info->id != NULL) ? info->id : "NULL");
        ESP_LOGI(TAG, "Name: %s", (info->name != NULL) ? info->name : "NULL");
        ESP_LOGI(TAG, "Hardware Version: %s", (info->hw_ver != NULL) ? info->hw_ver : "NULL");
        ESP_LOGI(TAG, "Software Version: %s", (info->sw_ver != NULL) ? info->sw_ver : "NULL");
        ESP_LOGI(TAG, "Firmware Version: %s", (info->fw_ver != NULL) ? info->fw_ver : "NULL");
        ESP_LOGI(TAG, "--------------------------------------------");
    }
    else
    {
        ESP_LOGW(TAG, "sscma client get info failed\n");
    }

    int64_t start = esp_timer_get_time();
    uint32_t flash_addr = 0x0;
    if (ota_type == OTA_TYPE_HIMAX) {
        ESP_LOGI(TAG, "flash Himax firmware ...");
    } else {
        ESP_LOGI(TAG, "flash Himax 4th ai model ...");
        flash_addr = 0xA00000;
    }

    if (sscma_client_ota_start(sscma_client, sscma_flasher, flash_addr) != ESP_OK)
    {
        ESP_LOGI(TAG, "sscma_client_ota_start failed\n");
        g_result_err = ESP_ERR_OTA_SSCMA_START_FAIL;
        ota_status.status = OTA_STATUS_FAIL;
        ota_status.err_code = g_result_err;
        esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, ota_eventid, 
                                        &ota_status, sizeof(struct view_data_ota_status),
                                        portMAX_DELAY);
        return;
    }

    //build the bridge struct
    esp_err_t err_in_http_events = ESP_OK;
    ota_http_userdata_t ota_http_userdata = {
        .client = sscma_client,
        .flasher = sscma_flasher,
        .ota_type = (int)ota_type,
        .err = &err_in_http_events
    };

    //https init
    esp_http_client_config_t *http_client_config = NULL;
    esp_http_client_handle_t http_client = NULL;

    http_client_config = psram_calloc(1, sizeof(esp_http_client_config_t));
    ESP_GOTO_ON_FALSE(http_client_config != NULL, ESP_ERR_NO_MEM, sscma_ota_end,
                      TAG, "sscma ota, mem alloc fail [1]");
    http_client_config->url = g_url_himax;
    http_client_config->method = HTTP_METHOD_GET;
    http_client_config->timeout_ms = HTTPS_TIMEOUT_MS;
    http_client_config->crt_bundle_attach = esp_crt_bundle_attach;
    http_client_config->user_data = &ota_http_userdata;
    http_client_config->buffer_size = SSCMA_FLASH_CHUNK_SIZE;
    http_client_config->event_handler = __http_event_handler;
#ifdef CONFIG_SKIP_COMMON_NAME_CHECK
    http_client_config->skip_cert_common_name_check = true;
#endif

    http_client = esp_http_client_init(http_client_config);
    ESP_GOTO_ON_FALSE(http_client != NULL, ESP_ERR_OTA_CONNECTION_FAIL, sscma_ota_end,
                      TAG, "sscma ota, http client init fail");

    esp_err_t err = esp_http_client_perform(http_client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "sscma ota, HTTP GET Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(http_client),
                esp_http_client_get_content_length(http_client));
        if (err_in_http_events != ESP_OK) {
            ret = err_in_http_events;
        } else {
            vTaskDelay(50 / portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "%s ota success, take %lld us\n", ota_type_str(ota_type), esp_timer_get_time() - start);
        }
    } else {
        ESP_LOGE(TAG, "sscma ota, HTTP GET request failed: %s", esp_err_to_name(err));
        //error defines:
        //https://docs.espressif.com/projects/esp-idf/zh_CN/v5.2.1/esp32s3/api-reference/protocols/esp_http_client.html#macros
        ret = ESP_ERR_OTA_DOWNLOAD_FAIL;  //we sum all these errors as download failure, easier for upper caller
    }

sscma_ota_end:
    if (http_client_config) free(http_client_config);
    if (http_client) esp_http_client_cleanup(http_client);

    g_result_err = ret;

    ota_status.status = g_result_err == ESP_OK ? OTA_STATUS_SUCCEED : OTA_STATUS_FAIL;
    ota_status.err_code = g_result_err;
    esp_event_post_to(app_event_loop_handle, CTRL_EVENT_BASE, ota_eventid, 
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
        //give the time to more important tasks right after the network is established
        vTaskDelay(pdMS_TO_TICKS(3000));

        ESP_LOGI(TAG, "network established, waiting for OTA request ...");

        do {
            ota_type = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            g_ota_running = true;
            if (ota_type == OTA_TYPE_ESP32) {
                //xTaskNotifyGive(g_task_worker);  // wakeup the task
                esp32_ota_process();
            } else if (ota_type == OTA_TYPE_HIMAX) {
                sscma_ota_process(OTA_TYPE_HIMAX);
            } else if (ota_type == OTA_TYPE_AI_MODEL) {
                sscma_ota_process(OTA_TYPE_AI_MODEL);
                xSemaphoreGive(g_sem_ai_model_downloaded);
            } else {
                ESP_LOGW(TAG, "unknown ota type: %" PRIu32, ota_type);
            }
            g_ota_running = false;

        } while (network_connect_flag);
    }
}

static void ota_status_report(struct view_data_ota_status *ota_status)
{
    ESP_LOGW(TAG, "ota_status_report: status: 0x%x, err_code: 0x%x, percent: %d",
                                              ota_status->status, ota_status->err_code, ota_status->percentage);
    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_OTA_STATUS, 
                            ota_status, sizeof(struct view_data_ota_status),
                            portMAX_DELAY);
    // MQTT report
    const char *fmt = \
                "\"3578\": %d,"
                "\"3579\": %d,"
                "\"3580\": %d";
    const int buffsz = 1024;
    char *buff = psram_calloc(1, buffsz);
    sniprintf(buff, buffsz, fmt, ota_status->status, ota_status->err_code, ota_status->percentage);

    int len = strlen(buff);
    if (g_cur_ota_version_esp32) {
        sniprintf(buff + len, buffsz - len, ",\"3502\": \"%s\"", g_cur_ota_version_esp32);
        len = strlen(buff);
    }
    if (g_cur_ota_version_himax) {
        sniprintf(buff + len, buffsz - len, ",\"3577\": \"%s\"", g_cur_ota_version_himax);
    }

    app_sensecraft_mqtt_report_device_status_generic(buff);

    free(buff);
}

static void ota_status_report_error(esp_err_t err)
{
    struct view_data_ota_status ota_status;

    ota_status.status = SENSECRAFT_OTA_STATUS_FAIL;
    ota_status.err_code = err;
    ota_status.percentage = 0;
    
    ota_status_report(&ota_status);
}

/**
 * process the ota status, calculate a final percentage, report to mqtt broker
 * 
 * ota_event_type: [in] himax or esp32?
 * total_progress: [in] 100 or 200
 * return:
 * - ESP_OK: succeed for this ota subpart, can proceed next MCU ota if any
 * - ESP_FAIL: fail for this ota subpart, should abort the whole ota process
*/
static esp_err_t ota_status(int ota_event_type, int total_progress)
{
    esp_err_t ret = ESP_FAIL;
    ota_status_q_item_t ota_status_q_item;
    struct view_data_ota_status ota_status;
    bool ever_processed = false;
    int percentage = 0;

    int timeout = ota_event_type == CTRL_EVENT_OTA_ESP32_FW ? (60000 * 2) : 60000;  //chunk of esp32 may be larger

    while (xQueueReceive(g_Q_ota_status, &ota_status_q_item, pdMS_TO_TICKS(timeout/*long enough?*/))) {
        if (ota_status_q_item.ota_src != ota_event_type) break;
        else ever_processed = true;

        if (ota_status_q_item.ota_status.status == OTA_STATUS_DOWNLOADING) {
            int progress = ota_status_q_item.ota_status.percentage;
            if (total_progress == 200 && ota_event_type == CTRL_EVENT_OTA_ESP32_FW) {
                progress += 100;  //himax must be succeeded
            }
            percentage = (int)(100 * progress / (total_progress));
            // report
            ota_status.status = SENSECRAFT_OTA_STATUS_UPGRADING;
            ota_status.err_code = ESP_OK;
            ota_status.percentage = percentage;
            ota_status_report(&ota_status);
        }
        else if (ota_status_q_item.ota_status.status == OTA_STATUS_SUCCEED) {
            if (total_progress == 200 && ota_event_type == CTRL_EVENT_OTA_HIMAX_FW) {
                ota_status.status = SENSECRAFT_OTA_STATUS_UPGRADING;
                ota_status.percentage = 50;
            } else {
                ota_status.status = SENSECRAFT_OTA_STATUS_SUCCEED;
                ota_status.percentage = 100;
            }
            ret = ESP_OK;
            break;
        }
        else if (ota_status_q_item.ota_status.status == OTA_STATUS_FAIL) {
            break;
        }
    }

    if (!ever_processed) {
        // timeout
        ota_status.status = SENSECRAFT_OTA_STATUS_FAIL;
        ota_status.err_code = ESP_ERR_OTA_TIMEOUT;
        ota_status.percentage = 0;
    } else if (ret == ESP_OK) {
        // succeed
        ota_status.err_code = ESP_OK;
    } else {
        ota_status.status = SENSECRAFT_OTA_STATUS_FAIL;
        ota_status.err_code = ota_status_q_item.ota_status.err_code;
        ota_status.percentage = 0;
    }
    ota_status_report(&ota_status);

    return ret;
}

static void __mqtt_ota_executor_task(void *p_arg)
{
    ESP_LOGI(TAG, "starting mqtt ota executor task ...");

    while (!network_connect_flag) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    //give the time to more important tasks right after the network is established
    //there might be queued ota requests in the `g_Q_ota_msg`, but no worry.
    vTaskDelay(pdMS_TO_TICKS(4000));

    cJSON *ota_msg_cjson;

    while (1) {
        if (xQueueReceive(g_Q_ota_msg, &ota_msg_cjson, portMAX_DELAY)) {
            //lightly check validation
            bool valid = false;
            do {
                if (!cJSON_IsObject(ota_msg_cjson)) break;

                cJSON *intent = cJSONUtils_GetPointer(ota_msg_cjson, "/intent");
                if (!intent || !cJSON_IsString(intent) || strcmp(intent->valuestring, "order") != 0) break;

                cJSON *order = cJSONUtils_GetPointer(ota_msg_cjson, "/order");
                if (!order || !cJSON_IsArray(order) || cJSON_GetArraySize(order) == 0 || cJSON_GetArraySize(order) > 2) break;

                cJSON *order_name = cJSONUtils_GetPointer(ota_msg_cjson, "/order/0/name");
                if (!order_name || !cJSON_IsString(order_name) || strcmp(order_name->valuestring, "version-notify") != 0) break;

                valid = true;
            } while (0);

            if (!valid) {
                ESP_LOGW(TAG, "incoming ota cjson invalid!");
                ota_status_report_error(ESP_ERR_OTA_JSON_INVALID);
                goto cleanup;
            }

            //parse the json order
            cJSON *orders = cJSONUtils_GetPointer(ota_msg_cjson, "/order");
            int num_orders = cJSON_GetArraySize(orders);
            int total_progress = 0;

            //search himax and esp32
            int found = 0;
            cJSON *order_value_himax = NULL, *order_value_esp32 = NULL;
            g_cur_ota_version_esp32 = NULL;
            g_cur_ota_version_himax = NULL;
            bool new_himax = false, new_esp32 = false;
            cJSON *one_order;
            cJSON_ArrayForEach(one_order, orders)
            {
                cJSON *order_value = cJSON_GetObjectItem(one_order, "value");
                cJSON *order_value_sku = cJSON_GetObjectItem(order_value, "sku");
                if (order_value_sku && cJSON_IsString(order_value_sku)) {
                    if (strstr(order_value_sku->valuestring, "himax")) {
                        found++;
                        order_value_himax = order_value;
                        cJSON *fwv = cJSON_GetObjectItem(order_value_himax, "fwv");
                        if (fwv && cJSON_IsString(fwv)) {
                            //version compare
                            char *himax_version = tf_module_ai_camera_himax_version_get();
                            if (himax_version) {
                                int res = cmp_versions(fwv->valuestring, himax_version);
                                if (res <= 0) {
                                    ESP_LOGW(TAG, "himax version too old (%s <= %s), skip ...", fwv->valuestring, himax_version);
                                } else {
                                    ESP_LOGI(TAG, "will upgrade himax (%s > %s)", fwv->valuestring, himax_version);
                                    g_cur_ota_version_himax = fwv->valuestring;
                                    new_himax = true;
                                    total_progress += 100;
                                }
                            } else {
                                ESP_LOGW(TAG, "can not get himax version! Always do OTA.");
                                g_cur_ota_version_himax = fwv->valuestring;
                                new_himax = true;
                                total_progress += 100;
                            }
                        } else {
                            ESP_LOGW(TAG, "incoming ota cjson invalid, no fwv field!");
                            ota_status_report_error(ESP_ERR_OTA_JSON_INVALID);
                            goto cleanup;
                        }
                    }
                    else if (strstr(order_value_sku->valuestring, "esp32")) {
                        found++;
                        order_value_esp32 = order_value;
                        cJSON *fwv = cJSON_GetObjectItem(order_value_esp32, "fwv");
                        if (fwv && cJSON_IsString(fwv)) {
                            //version compare
                            ota_worker_task_data_t worker_data;
                            worker_call(&worker_data, CMD_esp_ota_get_running_partition);
                            esp_app_desc_t *running_app_info = psram_calloc(1, sizeof(esp_app_desc_t));
                            worker_data.app_desc = running_app_info;
                            worker_call(&worker_data, CMD_esp_ota_get_partition_description);

                            if (worker_data.err == ESP_OK) {
                                ESP_LOGI(TAG, "Running firmware version: %s", running_app_info->version);
                                app_ota_any_ignore_version_check(false);
                                int res = cmp_versions(fwv->valuestring, running_app_info->version);
                                if (res <= 0) {
                                    ESP_LOGW(TAG, "esp32 version too old (%s <= %s), skip ...", fwv->valuestring, running_app_info->version);
                                } else {
                                    ESP_LOGI(TAG, "will upgrade esp32 (%s > %s)", fwv->valuestring, running_app_info->version);
                                    g_cur_ota_version_esp32 = fwv->valuestring;
                                    new_esp32 = true;
                                    total_progress += 100;
                                }
                            } else {
                                ESP_LOGW(TAG, "Failed to get running_app_info! Always do OTA [2].");
                                g_cur_ota_version_esp32 = fwv->valuestring;
                                new_esp32 = true;
                                total_progress += 100;
                            }
                            free(running_app_info);
                        } else {
                            ESP_LOGW(TAG, "incoming ota cjson invalid, no fwv field [2]!");
                            ota_status_report_error(ESP_ERR_OTA_JSON_INVALID);
                            goto cleanup;
                        }
                    }
                }
            }
            if (found != num_orders) {
                ESP_LOGW(TAG, "incoming ota cjson invalid [2]!");
                ota_status_report_error(ESP_ERR_OTA_JSON_INVALID);
                goto cleanup;
            }

            bool need_reboot = false;
            //upgrade himax
            if (order_value_himax && new_himax) {
                cJSON *file_url = cJSON_GetObjectItem(order_value_himax, "file_url");
                if (file_url && cJSON_IsString(file_url)) {
                    esp_err_t err = app_ota_himax_fw_download(file_url->valuestring);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "app_ota_himax_fw_download err: 0x%x", err);
                        ota_status_report_error(err);
                        goto cleanup;
                    } else {
                        // now it's downloading, listen to the status events
                        // this is blocking
                        esp_err_t ret = ota_status(CTRL_EVENT_OTA_HIMAX_FW, total_progress);
                        if (ret != ESP_OK) {
                            ESP_LOGW(TAG, "himax firmware ota aborted!!!");
                            //status already reported in ota_status();
                            goto cleanup;
                        } else {
                            ESP_LOGI(TAG, "himax firmware ota succeeded!!!");
                            need_reboot = true;
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "incoming ota cjson invalid, no file_url field!");
                    ota_status_report_error(ESP_ERR_OTA_JSON_INVALID);
                    goto cleanup;
                }
            }

            //upgrade esp32
            if (order_value_esp32 && new_esp32) {
                cJSON *file_url = cJSON_GetObjectItem(order_value_esp32, "file_url");
                if (file_url && cJSON_IsString(file_url)) {
                    app_ota_any_ignore_version_check(false);
                    esp_err_t err = app_ota_esp32_fw_download(file_url->valuestring);
                    if (err != ESP_OK) {
                        ESP_LOGW(TAG, "app_ota_esp32_fw_download err: 0x%x", err);
                        ota_status_report_error(err);
                        goto cleanup;
                    } else {
                        // now it's downloading, listen to the status events
                        // this is blocking
                        esp_err_t ret = ota_status(CTRL_EVENT_OTA_ESP32_FW, total_progress);
                        if (ret != ESP_OK) {
                            ESP_LOGW(TAG, "esp32 firmware ota aborted!!!");
                            //status already reported in ota_status();
                            goto cleanup;
                        } else {
                            ESP_LOGI(TAG, "esp32 firmware ota succeeded!!!");
                            need_reboot = true;
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "incoming ota cjson invalid, no file_url field [2]!");
                    ota_status_report_error(ESP_ERR_OTA_JSON_INVALID);
                    goto cleanup;
                }
            }

            //lucky passthrough -_-!!
            if (need_reboot) {
                ESP_LOGW(TAG, "!!! WILL REBOOT IN 3 SEC !!!");
                vTaskDelay(pdMS_TO_TICKS(3000));  // let the last mqtt msg sent
                esp_restart();
            }

            //the json is used up
cleanup:
            cJSON_Delete(ota_msg_cjson);
        }
    }
}

/* Event handler for catching system events */
static void __sys_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == ESP_HTTPS_OTA_EVENT) {
        switch (event_id) {
            case ESP_HTTPS_OTA_START:
                ESP_LOGI(TAG, "ota event: OTA started");
                break;
            case ESP_HTTPS_OTA_CONNECTED:
                ESP_LOGI(TAG, "ota event: Connected to server");
                break;
            case ESP_HTTPS_OTA_GET_IMG_DESC:
                ESP_LOGI(TAG, "ota event: Reading Image Description");
                break;
            case ESP_HTTPS_OTA_VERIFY_CHIP_ID:
                ESP_LOGI(TAG, "ota event: Verifying chip id of new image: %d", *(esp_chip_id_t *)event_data);
                break;
            case ESP_HTTPS_OTA_DECRYPT_CB:
                ESP_LOGI(TAG, "ota event: Callback to decrypt function");
                break;
            case ESP_HTTPS_OTA_WRITE_FLASH:
                ESP_LOGV(TAG, "ota event: Writing to flash: %d written", *(int *)event_data);
                break;
            case ESP_HTTPS_OTA_UPDATE_BOOT_PARTITION:
                ESP_LOGI(TAG, "ota event: Boot partition updated. Next Partition: %d", *(esp_partition_subtype_t *)event_data);
                break;
            case ESP_HTTPS_OTA_FINISH:
                ESP_LOGI(TAG, "ota event: OTA finish");
                break;
            case ESP_HTTPS_OTA_ABORT:
                ESP_LOGI(TAG, "ota event: OTA abort");
                break;
        }
    }
    else if (event_base == ESP_HTTP_CLIENT_EVENT) {
        switch (event_id) {
            case HTTP_EVENT_REDIRECT:
                ESP_LOGI(TAG, "http event: Redirection");
                break;
            default:
                break;
        }
    }
}

static void __app_event_handler(void *handler_args, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == VIEW_EVENT_BASE) {
        switch (event_id) {
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
    else if (event_base == CTRL_EVENT_BASE) {
        switch (event_id) {
            case CTRL_EVENT_MQTT_OTA_JSON:
                ESP_LOGI(TAG, "event: CTRL_EVENT_MQTT_OTA_JSON");
                if (xQueueSend(g_Q_ota_msg, event_data, pdMS_TO_TICKS(5000)) != pdPASS) {
                    ESP_LOGW(TAG, "can not push to ota msg Q, maybe full? drop this item!");
                }
                break;
            case CTRL_EVENT_OTA_HIMAX_FW:
            case CTRL_EVENT_OTA_ESP32_FW:
                ESP_LOGD(TAG, "event: CTRL_EVENT_OTA_%s_FW", event_id == CTRL_EVENT_OTA_HIMAX_FW ? "HIMAX" : "ESP32");
                //event_data is ptr to struct view_data_ota_status
                ota_status_q_item_t *item = psram_calloc(1, sizeof(ota_status_q_item_t));
                item->ota_src = event_id;
                memcpy(&(item->ota_status), event_data, sizeof(struct view_data_ota_status));
                if (xQueueSend(g_Q_ota_status, item, pdMS_TO_TICKS(5000)) != pdPASS) {
                    ESP_LOGW(TAG, "can not push to ota status Q, maybe full? drop this item!");
                }
                free(item);  //item is copied to Q
                break;
            default:
                break;
        }
    }
}

#if CONFIG_ENABLE_TEST_ENV
static void __ota_test_task(void *p_arg)
{
    while (!network_connect_flag) {
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    vTaskDelay(pdMS_TO_TICKS(3000));

    // esp_err_t res = app_ota_esp32_fw_download("https://new.pxspeed.site/factory_firmware.bin");
    // ESP_LOGI(TAG, "test app_ota_esp32_fw_download: 0x%x", res);

    // esp_err_t res = app_ota_ai_model_download("https://new.pxspeed.site/human_pose.tflite", 0);
    // ESP_LOGI(TAG, "test app_ota_ai_model_download: 0x%x", res);

    vTaskDelete(NULL);
}
#endif

esp_err_t app_ota_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    g_sem_network = xSemaphoreCreateBinary();
    g_sem_ai_model_downloaded = xSemaphoreCreateBinary();
    g_sem_worker_done = xSemaphoreCreateBinary();

    // Q init
    const int q_size = 2;
    StaticQueue_t *q_buf = heap_caps_calloc(1, sizeof(StaticQueue_t), MALLOC_CAP_INTERNAL);
    uint8_t *q_storage = psram_calloc(1, q_size * sizeof(void *));
    g_Q_ota_msg = xQueueCreateStatic(q_size, sizeof(void *), q_storage, q_buf);

    const int q_size1 = 10;
    StaticQueue_t *q_buf1 = heap_caps_calloc(1, sizeof(StaticQueue_t), MALLOC_CAP_INTERNAL);
    uint8_t *q_storage1 = psram_calloc(1, q_size1 * sizeof(ota_status_q_item_t));
    g_Q_ota_status = xQueueCreateStatic(q_size1, sizeof(ota_status_q_item_t), q_storage1, q_buf1);

    // Ringbuffer init
    StaticRingbuffer_t *buffer_struct = (StaticRingbuffer_t *)psram_calloc(1, sizeof(StaticRingbuffer_t));
    uint8_t *buffer_storage = (uint8_t *)psram_calloc(1, AI_MODEL_RINGBUFF_SIZE);
    g_rb_ai_model = xRingbufferCreateStatic(AI_MODEL_RINGBUFF_SIZE, RINGBUF_TYPE_BYTEBUF, buffer_storage, buffer_struct);

    // ota main task
    const uint32_t stack_size = 10 * 1024;
    StackType_t *task_stack = (StackType_t *)psram_calloc(1, stack_size * sizeof(StackType_t));
    g_task = xTaskCreateStatic(__app_ota_task, "app_ota", stack_size, NULL, 1, task_stack, &g_task_tcb);

    // task for handling incoming mqtt
    StackType_t *task_stack1 = (StackType_t *)psram_calloc(1, stack_size * sizeof(StackType_t));
    StaticTask_t *task_tcb1 = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    xTaskCreateStatic(__mqtt_ota_executor_task, "mqtt_ota", stack_size, NULL, 2, task_stack1, task_tcb1);

    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_OTA_EVENT, ESP_EVENT_ANY_ID, __sys_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTP_CLIENT_EVENT, ESP_EVENT_ANY_ID, __sys_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                                    __app_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_OTA_JSON,
                                                    __app_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, OTA_EVENT_BASE, ESP_EVENT_ANY_ID,
                                                    __ota_event_handler, NULL));
    // ota status handling
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_OTA_HIMAX_FW,
                                                    __app_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_OTA_ESP32_FW,
                                                    __app_event_handler, NULL));

#if CONFIG_ENABLE_TEST_ENV
    StackType_t *task_stack2 = (StackType_t *)psram_calloc(1, stack_size * sizeof(StackType_t));
    StaticTask_t *task_tcb2 = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    xTaskCreateStatic(__ota_test_task, "ota_test", stack_size, NULL, 1, task_stack2, task_tcb2);
#endif

    return ESP_OK;
}

esp_err_t app_ota_ai_model_download(char *url, int size_bytes)
{
    if (g_ota_running) return ESP_ERR_OTA_ALREADY_RUNNING;

    g_url_himax = url;
    g_result_err = ESP_OK;

    if (xTaskNotify(g_task, OTA_TYPE_AI_MODEL, eSetValueWithoutOverwrite) == pdFAIL) {
        return ESP_ERR_OTA_ALREADY_RUNNING;
    }

    // block until ai model download completed or failed
    xSemaphoreTake(g_sem_ai_model_downloaded, portMAX_DELAY);

    return g_result_err;
}

esp_err_t app_ota_esp32_fw_download(char *url)
{
    if (g_ota_running) return ESP_ERR_OTA_ALREADY_RUNNING;

    g_url_esp32 = url;

    if (xTaskNotify(g_task, OTA_TYPE_ESP32, eSetValueWithoutOverwrite) == pdFAIL) {
        return ESP_ERR_OTA_ALREADY_RUNNING;
    }

    return ESP_OK;
}

esp_err_t app_ota_himax_fw_download(char *url)
{
    if (g_ota_running) return ESP_ERR_OTA_ALREADY_RUNNING;

    g_url_himax = url;

    if (xTaskNotify(g_task, OTA_TYPE_HIMAX, eSetValueWithoutOverwrite) == pdFAIL) {
        return ESP_ERR_OTA_ALREADY_RUNNING;
    }

    return ESP_OK;
}

void  app_ota_any_ignore_version_check(bool ignore)
{
    g_ignore_version_check = ignore;
}
