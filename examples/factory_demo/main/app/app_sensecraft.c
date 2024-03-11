#include "app_sensecraft.h"
#include "esp_log.h"
#include <mbedtls/base64.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/cdefs.h>
#include <inttypes.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "app_tasklist.h"
#include "cJSON.h"
// #include "audio_player.h"


#define UPLOAD_FLAG_READY      0
#define UPLOAD_FLAG_WAITING    1
#define UPLOAD_FLAG_UPLOADING  2
#define UPLOAD_FLAG_DONE       3


static const char *TAG = "app-sensecraft";

static struct view_data_image_invoke image_invoke;
static struct view_data_image image_640_480;

static struct view_data_record record_data; // 不需要重新buf

static uint8_t image_upload_flag = UPLOAD_FLAG_READY;
static uint8_t audio_upload_flag = UPLOAD_FLAG_READY;
static uint8_t network_connect_flag = 0;

static SemaphoreHandle_t __g_data_mutex;
static SemaphoreHandle_t __g_event_sem;

static uint8_t scene_id = SCENE_ID_DEFAULT;

#define ERROR_CHECK(a, str, ret)                                               \
    if (!(a))                                                                  \
    {                                                                          \
        ESP_LOGE(TAG, "%s:%d (%s):%s", __FILE__, __LINE__, __FUNCTION__, str); \
        return (ret);                                                          \
    }

#define ERROR_CHECK_GOTO(a, str, label)                                        \
    if (!(a))                                                                  \
    {                                                                          \
        ESP_LOGE(TAG, "%s:%d (%s):%s", __FILE__, __LINE__, __FUNCTION__, str); \
        goto label;                                                            \
    }

static void  image_upload_flag_set(uint8_t flag)
{
    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    image_upload_flag = flag;
    xSemaphoreGive(__g_data_mutex);    
}

static char *__request(const char *base_url, const char *api_key, const char *endpoint, const char *content_type, esp_http_client_method_t method, const char *boundary, uint8_t *data, size_t len)
{
    char *url = NULL;
    char *result = NULL;
    asprintf(&url, "%s%s", base_url, endpoint);
    ERROR_CHECK(url != NULL, "Failed to allocate url!", NULL);
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char *headers = NULL;
    if (boundary)
    {
        asprintf(&headers, "%s; boundary=%s", content_type, boundary);
    }
    else
    {
        asprintf(&headers, "%s", content_type);
    }
    ERROR_CHECK_GOTO(headers != NULL, "Failed to allocate headers!", end);
    esp_http_client_set_header(client, "Content-Type", headers);
    free(headers);

    if (api_key != NULL)
    {
        asprintf(&headers, "Bearer %s", api_key);
        ERROR_CHECK_GOTO(headers != NULL, "Failed to allocate headers!", end);
        esp_http_client_set_header(client, "Authorization", headers);
        free(headers);
    }

    esp_err_t err = esp_http_client_open(client, len);
    ERROR_CHECK_GOTO(err == ESP_OK, "Failed to open client!", end);
    if (len > 0)
    {
        int wlen = esp_http_client_write(client, (const char *)data, len);
        ERROR_CHECK_GOTO(wlen >= 0, "Failed to write client!", end);
    }
    int content_length = esp_http_client_fetch_headers(client);
    if (esp_http_client_is_chunked_response(client))
    {
        esp_http_client_get_chunk_length(client, &content_length);
    }
    ESP_LOGD(TAG, "content_length=%d", content_length);
    ERROR_CHECK_GOTO(content_length >= 0, "HTTP client fetch headers failed!", end);
    result = (char *)malloc(content_length + 1);
    int read = esp_http_client_read_response(client, result, content_length);
    if (read != content_length)
    {
        ESP_LOGE(TAG, "HTTP_ERROR: read=%d, length=%d", read, content_length);
        free(result);
        result = NULL;
    }
    else
    {
        result[content_length] = 0;
        ESP_LOGD(TAG, "result: %s, size: %d", result, strlen(result));
    }

end:
    free(url);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result != NULL ? result : NULL;
}

static char *__https_upload_audio(uint8_t *audio_data, size_t audio_len)
{
    uint8_t last_flag = 0;
    const char *boundary = "------FormBoundaryShouldDifferAtRuntime";

    char *itemPrefix = NULL;
    asprintf(&itemPrefix, "--%s\r\nContent-Disposition: form-data; name=", boundary);

    char *reqBody = NULL;
    asprintf(&reqBody, "%s\"audio\"; filename=\"audio.%s\"\r\nContent-Type: %s\r\n\r\n", itemPrefix, "wav", "audio/wav");
    ERROR_CHECK(reqBody != NULL, "Failed to allocate reqBody!", NULL);

    char *reqEndBody = NULL;
    asprintf(&reqEndBody, "\r\n--%s--\r\n", boundary);
    ERROR_CHECK(reqEndBody != NULL, "Failed to allocate reqEndBody!", NULL);

    uint8_t *data = NULL;
    size_t len = 0;

    len = strlen(reqBody) + strlen(reqEndBody) + audio_len;
    data = (uint8_t *)malloc(len + 1);
    ERROR_CHECK(data != NULL, "Failed to allocate request buffer!", NULL);
    uint8_t *d = data;
    memcpy(d, reqBody, strlen(reqBody));
    d += strlen(reqBody);

    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    memcpy(d, audio_data, audio_len);
    last_flag = audio_upload_flag;
    audio_upload_flag = 0;
    xSemaphoreGive(__g_data_mutex);

    if (!last_flag)
    {
        // audio_data 数据被其他任务打断修改了
        free(reqBody);
        free(reqEndBody);
        free(itemPrefix);
        free(data);
        ESP_LOGI(TAG, "audio_data data has been modified!");
        return NULL;
    }

    d += audio_len;
    memcpy(d, reqEndBody, strlen(reqEndBody));
    d += strlen(reqEndBody);
    *d = 0;
    free(reqBody);
    free(reqEndBody);
    free(itemPrefix);

    char *result = __request(SENSECRAFT_HTTPS_URL, NULL, "/ai/scene_select", "multipart/form-data", HTTP_METHOD_POST, boundary, data, len);
    free(data);

    FILE *fp;
    if (result != NULL)
    {
        tasklist_parse(result);
        free(result);
    }
    else
    {
        ESP_LOGE(TAG, "tts failed!");
        struct view_data_audio_play_data data;
        data.type = AUDIO_DATA_TYPE_FILE;
        strcpy(data.file_name, "/spiffs/tts_failed.mp3");
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_AUDIO_PALY, &data, sizeof(data), portMAX_DELAY);
    }
    return NULL;
}

static char * __https_upload_image(uint8_t *image_data, size_t image_len, uint8_t scene_id)
{

    uint8_t *p_data = NULL;
    int index = 0;
    size_t len = 0;

    //{"sceneId":1,"image":""}
    p_data = (uint8_t *)malloc(image_len + 32);
    ERROR_CHECK(p_data != NULL, "Failed to allocate request buffer!", NULL);

    index += sprintf((char *)p_data + index, "{\"sceneId\":%d,\"image\":\"", scene_id);

    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    memcpy(p_data + index, image_data, image_len);
    image_upload_flag = UPLOAD_FLAG_UPLOADING; // 不考虑上传失败的情况
    xSemaphoreGive(__g_data_mutex);

    index += image_len;
    index += sprintf((char *)p_data + index, "\"}");
    len = index;

    char *result = __request(SENSECRAFT_HTTPS_URL, NULL, "/ai/scene_detection", "application/json", HTTP_METHOD_POST, NULL, p_data, len);
    free(p_data);
    if (result != NULL)
    {
        tasklist_parse(result);
        free(result);
    }
    else
    {
        ESP_LOGE(TAG, "request failed");
    }

    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    image_upload_flag = UPLOAD_FLAG_READY; 
    xSemaphoreGive(__g_data_mutex);

    return NULL;
}

void __app_sensecraft_task(void *p_arg)
{
    ESP_LOGI(TAG, "start");
    while (1)
    {
        xSemaphoreTake(__g_event_sem, pdMS_TO_TICKS(5000));
        if (!network_connect_flag)
        {
            continue;
        }
        if (image_upload_flag == UPLOAD_FLAG_WAITING)
        {
            ESP_LOGI(TAG, "image upload: %ld", image_640_480.len);
            __https_upload_image(image_640_480.p_buf, image_640_480.len, scene_id);
        }

        if (audio_upload_flag)
        {
            ESP_LOGI(TAG, "audio upload: %ld", record_data.len);
            __https_upload_audio(record_data.p_buf, record_data.len);
        }
    }
}

static void __view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    case VIEW_EVENT_WIFI_ST:
    {
        static bool fist = true;
        ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
        struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
        if (p_st->is_network)
        { // todo
            network_connect_flag = 1;
        }
        else
        {
            network_connect_flag = 0;
        }
        break;
    }
    case VIEW_EVENT_AUDIO_WAKE:
    {
        ESP_LOGI(TAG, "event: VIEW_EVENT_AUDIO_WAKE");

        xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
        audio_upload_flag = 0; // wait data ready
        xSemaphoreGive(__g_data_mutex);

        break;
    }
    case VIEW_EVENT_AUDIO_VAD_TIMEOUT:
    {
        ESP_LOGI(TAG, "event: VIEW_EVENT_AUDIO_VAD_TIMEOUT");
        struct view_data_record *p_data = (struct view_data_record *)event_data;

        xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
        memcpy(&record_data, p_data, sizeof(struct view_data_record));
        audio_upload_flag = 1;
        xSemaphoreGive(__g_data_mutex);

        xSemaphoreGive(__g_event_sem); // notify task right now
        break;
    }
    default:
        break;
    }
}

int app_sensecraft_init(void)
{
    __g_data_mutex = xSemaphoreCreateMutex();
    __g_event_sem = xSemaphoreCreateBinary();

    memset(&image_invoke, 0, sizeof(image_invoke));
    image_invoke.image.p_buf = (uint8_t *)malloc(IMAGE_640_480_BUF_SIZE);
    assert(image_invoke.image.p_buf);

    image_640_480.len = 0;
    image_640_480.p_buf = (uint8_t *)malloc(IMAGE_240_240_BUF_SIZE);
    assert(image_640_480.p_buf);

    xTaskCreate(&__app_sensecraft_task, "__app_sensecraft_task", 1024 * 5, NULL, 10, NULL);

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle,
                                                             VIEW_EVENT_BASE, VIEW_EVENT_AUDIO_VAD_TIMEOUT,
                                                             __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle,
                                                             VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                                             __view_event_handler, NULL, NULL));

    return 0;
}

int app_sensecraft_image_upload(struct view_data_image *p_data)
{
    ESP_LOGI(TAG, "waitting upload image!");

    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    image_640_480.len = p_data->len;
    memcpy(image_640_480.p_buf, p_data->p_buf, p_data->len);
    image_upload_flag = UPLOAD_FLAG_WAITING;
    xSemaphoreGive(__g_data_mutex);

    xSemaphoreGive(__g_event_sem); // notify task right now

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_IMAGE_240_240_REQ, NULL, 0, portMAX_DELAY);

    return 0;
}

int app_sensecraft_image_invoke_check(struct view_data_image_invoke *p_data)
{
    static time_t last_image_upload_time = 0;
    time_t now = 0;

    printf( "check: %d, %d, %d, %lld\r\n", p_data->boxes_cnt, network_connect_flag, image_upload_flag, time(NULL) - last_image_upload_time);
    // 未检测到目标
    if (p_data->boxes_cnt <= 0)
    {
        return 0;
    }

    // 无网络
    if (!network_connect_flag)
    {
        return 0;
    }

    // 上次的数据是否上传完
    if (image_upload_flag != UPLOAD_FLAG_READY)
    {
        return 0;
    }

    // 上报间隔需要大于 IMAGE_UPLOAD_TIME_INTERVAL
    now = time(NULL);
    // if ((now - last_image_upload_time) < IMAGE_UPLOAD_TIME_INTERVAL)
    // {
    //     return 0;
    // }
    last_image_upload_time = now;

    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    image_invoke.boxes_cnt = p_data->boxes_cnt;
    memcpy(image_invoke.boxes, p_data->boxes, p_data->boxes_cnt * sizeof(struct view_data_boxes));
    memcpy(image_invoke.image.p_buf, p_data->image.p_buf, p_data->image.len);
    image_invoke.image.len = p_data->image.len;
    xSemaphoreGive(__g_data_mutex);

    ESP_LOGI(TAG, "Need upload image!");

    // sample 640*480 image
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_IMAGE_640_480_REQ, NULL, 0, portMAX_DELAY);

    return 1;
}