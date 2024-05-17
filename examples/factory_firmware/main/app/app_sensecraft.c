#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <mbedtls/base64.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "sensecap-watcher.h"

#include "app_sensecraft.h"
#include "event_loops.h"
#include "app_tasklist.h"
#include "app_audio.h"
#include "app_mqtt_client.h"
#include "app_taskengine.h"
#include "util.h"

#include "view_image_preview.h"
#include "ui/ui.h"

// #include "audio_player.h"

#define USE_TESTENV_LLM_API    1
// #define SENSECRAFT_HTTPS_URL  "http://192.168.100.10:8888"
#define SENSECRAFT_HTTPS_URL  "https://csg-demo.exposeweb.pro/ai001tusuknyshiphjexy/v1/watcher/vision"

#define GLOBAL_SILENT_TIME     30  //temp, this is ugly

#define UPLOAD_FLAG_READY      0
#define UPLOAD_FLAG_REQ        1
#define UPLOAD_FLAG_WAITING    2
#define UPLOAD_FLAG_UPLOADING  3
#define UPLOAD_FLAG_DONE       4


static const char *TAG = "app-sensecraft";

static TaskHandle_t g_task;
static StaticTask_t g_task_tcb;

static struct view_data_image image_640_480;

static struct view_data_record record_data; // 不需要重新buf

static uint8_t image_upload_flag = UPLOAD_FLAG_READY;
static uint8_t audio_upload_flag = UPLOAD_FLAG_READY;
static uint8_t network_connect_flag = 0;
static time_t last_image_upload_time = 0;

static SemaphoreHandle_t __g_data_mutex;
static SemaphoreHandle_t __g_event_sem;

static uint8_t scene_id = SCENE_ID_DEFAULT;

static struct ctrl_data_taskinfo7 *g_ctrl_data_taskinfo_7;
static SemaphoreHandle_t g_mtx_task7_cjson;
static cJSON *g_task7_cjson;

uint8_t g_predet = 1; // 1 for no human, 2 for human

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

// this is ugly
static void tmp_parse_cloud_result(char *result)
{
    intmax_t tlid = app_taskengine_get_current_tlid();

    if (tlid == 0) {
        ESP_LOGW(TAG, "parse_cloud_result, no tasklist running, skip parsing...");
        return;
    }

    cJSON *json = cJSON_Parse(result);
    if (json) {
        if (cJSON_GetObjectItem(json, "code")->valueint == 0) {
            if ((json = cJSON_GetObjectItem(json, "data"))) {
                if ((json = cJSON_GetObjectItem(json, "tl"))) {
                    int array_len = cJSON_GetArraySize(json);
                    if (array_len > 0) {
                        // cloud warn
                        const char *warn_str = "cloud warn";
                        audio_play_task("/spiffs/alarm-di.wav");
                        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_ALARM_ON, warn_str, strlen(warn_str), portMAX_DELAY);
                        app_mqtt_client_report_warn_event(tlid, "The monitored event happened.", 1/*cloud warn*/);
                    }
                }
            }
        } else {
            ESP_LOGW(TAG, "cloud result, code != 0, code=%d", cJSON_GetObjectItem(json, "code")->valueint);
        }
    } else {
        ESP_LOGW(TAG, "cloud result is not valid json");
    }
}

static char *__request(const char *url, const char *api_key, const char *content_type, esp_http_client_method_t method, const char *boundary, uint8_t *data, size_t len)
{
    char *result = NULL;
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 29000,
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

    char *result = __request(SENSECRAFT_HTTPS_URL"/ai/scene_select", NULL,"multipart/form-data", HTTP_METHOD_POST, boundary, data, len);
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

static char * __https_upload_image(uint8_t *image_data, size_t image_len, const char *p_token)
{

    //{"sceneId":1,"image":""}
    char *p_data = malloc(image_len + 1);  //to ensure adding trailing '\0' to image_data
    ERROR_CHECK(p_data != NULL, "Failed to allocate request buffer!", NULL);
    memset(p_data, 0, image_len + 1);

    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    memcpy(p_data, image_data, image_len);  //tmp use
    cJSON *image = cJSON_CreateString(p_data);  //char * will be copied
    image_upload_flag = UPLOAD_FLAG_UPLOADING; // 不考虑上传失败的情况
    xSemaphoreGive(__g_data_mutex);

    free(p_data);

    xSemaphoreTake(g_mtx_task7_cjson, portMAX_DELAY);
    char *json_str = cJSON_PrintUnformatted(g_task7_cjson);
    ESP_LOGD(TAG, "task 7 json:\r\n%s", json_str);
    free(json_str);

    cJSON *detail = cJSON_GetObjectItem(g_task7_cjson, "detail");
    cJSON_ReplaceItemInObject(detail, "i", image);
#if USE_TESTENV_LLM_API
    if (!cJSON_GetObjectItem(detail, "token")) {
        cJSON_AddItemToObject(detail, "token", cJSON_CreateString("CEZHpciAM4LymJ_74gjUaApJ\""));
    }
#endif
    char *json_dl_str = cJSON_PrintUnformatted(detail);
    xSemaphoreGive(g_mtx_task7_cjson);

    //ESP_LOGD(TAG, "upload image, post body:\r\n%s", json_dl_str);

    char *api_host = SENSECRAFT_HTTPS_URL;

#if !USE_TESTENV_LLM_API
    api_host = cJSON_GetObjectItem(g_task7_cjson, "url")->valuestring;
#endif

    char *result = __request(api_host, NULL, "application/json", 
                            HTTP_METHOD_POST, NULL, (uint8_t *)json_dl_str, strlen(json_dl_str));
    
    free(json_dl_str);

    if (result != NULL)
    {
        //tasklist_parse(result);
        tmp_parse_cloud_result(result);
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
    cJSON *tmpjson;

    ESP_LOGI(TAG, "start:%s", SENSECRAFT_HTTPS_URL );
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

            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_IMAGE_640_480_SEND, NULL, 0, portMAX_DELAY);

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

static void __ctrl_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    case CTRL_EVENT_BROADCAST_TASK7:
    {
        ESP_LOGI(TAG, "received event: CTRL_EVENT_BROADCAST_TASK7");

        g_ctrl_data_taskinfo_7 = *(struct ctrl_data_taskinfo7 **)event_data;

        xSemaphoreTake(g_ctrl_data_taskinfo_7->mutex, portMAX_DELAY);
        cJSON *tmp = cJSON_Duplicate(g_ctrl_data_taskinfo_7->task7, 1);
        if (!g_ctrl_data_taskinfo_7->no_task7) {
            last_image_upload_time = 0;
        }
        xSemaphoreGive(g_ctrl_data_taskinfo_7->mutex);

        if (tmp) {
            // this is ugly
            xSemaphoreTake(g_mtx_task7_cjson, portMAX_DELAY);
            if (g_task7_cjson) cJSON_Delete(g_task7_cjson);
            g_task7_cjson = tmp;
            xSemaphoreGive(g_mtx_task7_cjson);
        }

        break;
    }
    default:
        break;
    }
}

int app_sensecraft_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    __g_data_mutex = xSemaphoreCreateMutex();
    __g_event_sem = xSemaphoreCreateBinary();
    g_mtx_task7_cjson = xSemaphoreCreateMutex();
    g_ctrl_data_taskinfo_7 = NULL;
    g_task7_cjson = NULL;

    image_640_480.len = 0;
    image_640_480.p_buf = (uint8_t *)malloc(IMAGE_240_240_BUF_SIZE);
    assert(image_640_480.p_buf);

    const uint32_t stack_size = 3 * 1024;
    StackType_t *task_stack = (StackType_t *)psram_malloc(stack_size);
    g_task = xTaskCreateStatic(__app_sensecraft_task, "app_sensecraft", stack_size, NULL, 6, task_stack, &g_task_tcb);


    // xTaskCreate(__app_sensecraft_task, "app_sensecraft", 1024 * 3, NULL,  6, NULL);

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle,
                                                             VIEW_EVENT_BASE, VIEW_EVENT_AUDIO_VAD_TIMEOUT,
                                                             __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle,
                                                             VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                                             __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(ctrl_event_handle,
                                                             CTRL_EVENT_BASE, CTRL_EVENT_BROADCAST_TASK7,
                                                             __ctrl_event_handler, NULL, NULL));
    audio_player_init();
    
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
    time_t now = 0;


    // printf( "check: %d, %d, %d, %lld\r\n", p_data->boxes_cnt, network_connect_flag, image_upload_flag, time(NULL) - last_image_upload_time);
    
    // 未检测到目标
    if (p_data->boxes_cnt <= 0)
    {
        return 0;
    }

    // 等待taskengine下发任务流，不管是从flash加载的还是从云端获取的
    if (g_ctrl_data_taskinfo_7 == NULL) {
        return 0;
    }
    // 如果没有tasklist在跑，没有必要做下面检测
    intmax_t tlid = app_taskengine_get_current_tlid();
    if (tlid == 0) {
        return 0;
    }

    uint8_t max_score = 0;
    for (size_t i = 0; i < p_data->boxes_cnt; i++)
    {
        max_score =  p_data->boxes[i].score  > max_score ? p_data->boxes[i].score: max_score;
    }
    if( max_score < 60) {
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
        now = time(NULL);
        if ((now - last_image_upload_time) > GLOBAL_SILENT_TIME)
        {
            image_upload_flag = UPLOAD_FLAG_READY;
        } 
        return 0;  //这一次不行，下次可以上传
    }

    // 判断静默期是否已过
    now = time(NULL);
    if ((now - last_image_upload_time) < GLOBAL_SILENT_TIME && last_image_upload_time != 0) {
        ESP_LOGD(TAG, "golbal silent time not passed, wait ...");
        return 0;
    }

    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    image_upload_flag = UPLOAD_FLAG_REQ;
    xSemaphoreGive(__g_data_mutex);

    now = time(NULL);
    last_image_upload_time = now;

    bool notask7 = true;
    if (g_ctrl_data_taskinfo_7) {
        xSemaphoreTake(g_ctrl_data_taskinfo_7->mutex, portMAX_DELAY);
        notask7 = g_ctrl_data_taskinfo_7->no_task7;
        xSemaphoreGive(g_ctrl_data_taskinfo_7->mutex);
    }
    ESP_LOGI(TAG, "local warn? %d", notask7);
    
    if (notask7) {
        const char *warn_str = "local warn";
        audio_play_task("/spiffs/alarm-di.wav");
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_ALARM_ON, warn_str, strlen(warn_str), portMAX_DELAY);

        app_mqtt_client_report_warn_event(tlid, "The monitored object was detected on the device.", 0/*local warn*/);
        
    } else {
        ESP_LOGI(TAG, "Need upload image!");
        // sample 640*480 image
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_IMAGE_640_480_REQ, NULL, 0, portMAX_DELAY);
    }

    return 1;
}