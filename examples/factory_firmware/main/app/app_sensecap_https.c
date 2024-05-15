#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/cdefs.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <mbedtls/base64.h>
#include "cJSON.h"


#include "app_sensecap_https.h"
#include "event_loops.h"
#include "data_defs.h"
#include "deviceinfo.h"
#include "util.h"

static const char *TAG = "sensecap-https";

static TaskHandle_t g_task;
static StaticTask_t g_task_tcb;

static uint8_t network_connect_flag = 0;
static SemaphoreHandle_t __g_data_mutex;
static SemaphoreHandle_t __g_event_sem;
static struct view_data_mqtt_connect_info mqttinfo;

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

static char *__request( const char *base_url, 
                        const char *api_key, 
                        const char *endpoint, 
                        const char *content_type, 
                        esp_http_client_method_t method, 
                        const char *boundary, 
                        uint8_t *data, size_t len)
{
    char *url = NULL;
    char *result = NULL;
    asprintf(&url, "%s%s", base_url, endpoint);
    ERROR_CHECK(url != NULL, "Failed to allocate url!", NULL);
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 15000,
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
        asprintf(&headers, "Device %s", api_key);
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
        ESP_LOGD(TAG, "content: %s, size: %d", result, strlen(result));
    }

end:
    free(url);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result != NULL ? result : NULL;
}

static int __https_token_get(struct view_data_mqtt_connect_info *p_info, 
                                const char *p_token)
{
    char *result = __request(SENSECAP_URL, p_token, SENSECAP_PATH_TOKEN_GET, "application/json", HTTP_METHOD_GET, NULL, NULL,0);
    if (result == NULL)
    {
        ESP_LOGE(TAG, "request failed");
        return -1;
    }

    // 解析JSON
    cJSON *root = cJSON_Parse(result);
    if (root == NULL) {
        ESP_LOGE(TAG,"Error before: [%s]\n", cJSON_GetErrorPtr());
        return -1;
    }

    // 获取code
    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (  code->valueint != 0) {
        ESP_LOGE(TAG,"Code: %d\n", code->valueint);
        free(result);
        return -1;
    }

    // 获取data对象
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (data != NULL && cJSON_IsObject(data)) {
        xSemaphoreTake(p_info->mutex, portMAX_DELAY);
        // 获取serverUrl
        cJSON *serverUrl_json = cJSON_GetObjectItem(data, "serverUrl");
        if (cJSON_IsString(serverUrl_json)) {
            strcpy(p_info->serverUrl, serverUrl_json->valuestring);
        }

        // 获取token
        cJSON *token_json = cJSON_GetObjectItem(data, "token");
        if (cJSON_IsString(token_json)) {
            strcpy(p_info->token, token_json->valuestring);
        }

        // 获取expiresIn
        cJSON *expiresIn_json = cJSON_GetObjectItem(data, "expiresIn");
        if (cJSON_IsString(expiresIn_json)) {
            p_info->expiresIn = atoll(expiresIn_json->valuestring)/1000;
        }

        // 获取mqttPort
        cJSON *mqttPort_json = cJSON_GetObjectItem(data, "mqttPort");
        if (cJSON_IsString(mqttPort_json)) {
            p_info->mqttPort = atoi(mqttPort_json->valuestring);
        }

        // 获取mqttsPort
        cJSON *mqttsPort_json = cJSON_GetObjectItem(data, "mqttsPort");
        if (cJSON_IsString(mqttsPort_json)) {
            p_info->mqttsPort = atoi(mqttsPort_json->valuestring);
        }
        xSemaphoreGive(p_info->mutex);
    }

    // 打印值
    ESP_LOGI(TAG, "Server URL: %s", p_info->serverUrl);
    ESP_LOGI(TAG, "Token: %s", p_info->token);
    ESP_LOGI(TAG, "Expires In: %d", p_info->expiresIn);
    ESP_LOGI(TAG, "MQTT Port: %d", p_info->mqttPort);
    ESP_LOGI(TAG, "MQTTS Port: %d", p_info->mqttsPort);

    // 释放资源
    cJSON_Delete(root);

    free(result);
    return 0;
}

void __app_sensecap_https_task(void *p_arg)
{
    ESP_LOGI(TAG, "start %s", SENSECAP_URL );
    
    int ret = 0;
    time_t now = 0;
    static struct view_data_mqtt_connect_info *p_mqttinfo = &mqttinfo;

    char deviceinfo_buf[70];
    char token[71];
    size_t token_len = 0;
    size_t len =0;
    memset(deviceinfo_buf, 0, sizeof(deviceinfo_buf));
    memset(token, 0, sizeof(token));

    struct view_data_deviceinfo deviceinfo;

    ret  = deviceinfo_get(&deviceinfo);
    if( ret == ESP_OK ) {
        len = snprintf(deviceinfo_buf, sizeof(deviceinfo_buf), "%s:%s", deviceinfo.eui, deviceinfo.key);
        ret = mbedtls_base64_encode(( uint8_t *)token, sizeof(token), &token_len, ( uint8_t *)deviceinfo_buf, len);
        if( ret != 0  ||  token_len < 60 ) {
            token_len = 0;
            ESP_LOGE(TAG, "mbedtls_base64_encode failed:%d,", ret);
        } else {
            ESP_LOGI(TAG, "EUI:%s,KEY:%s,Token:%s", deviceinfo.eui, deviceinfo.key, token);
        }
    } else {
        ESP_LOGE(TAG, "deviceinfo read fail %d!", ret);
        token_len = 0;
    }

    while (1)
    {
        xSemaphoreTake(__g_event_sem, pdMS_TO_TICKS(10000));
        if (!network_connect_flag || token_len==0)
        {
            continue;
        }
        time(&now);  // now is seconds since unix epoch
        if ((p_mqttinfo->expiresIn) < ((int)now + 60)) // 至少提前一分钟获取token
        {
            ESP_LOGI(TAG, "mqtt token is near expiration, now: %d, expire: %d, refresh it ...", (int)now, (p_mqttinfo->expiresIn));
            ret = __https_token_get(p_mqttinfo, (const char *)token);
            if( ret == 0 ) {
                // mqttinfo is big, we post pointer of it along with a mutex
                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_MQTT_CONNECT_INFO, 
                                    &p_mqttinfo, sizeof(p_mqttinfo), portMAX_DELAY);
            }
        }
    }
}

static void __view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
    //wifi connection state changed
    case VIEW_EVENT_WIFI_ST:
    {
        static bool fist = true;
        ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
        struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
        if (p_st->is_network)
        {
            network_connect_flag = 1;
        }
        else
        {
            network_connect_flag = 0;
        }
        xSemaphoreGive(__g_event_sem);
        break;
    }
    default:
        break;
    }
}

int app_sensecap_https_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    __g_data_mutex = xSemaphoreCreateMutex();
    __g_event_sem = xSemaphoreCreateBinary();

    mqttinfo.mutex = xSemaphoreCreateMutex();

    // xTaskCreate(__app_sensecap_https_task, "app_sensecap_https_task", 1024 * 5, NULL, 3, NULL);

    const uint32_t stack_size = 3 * 1024 + 256;
    StackType_t *task_stack = (StackType_t *)psram_alloc(stack_size);
    g_task = xTaskCreateStatic(__app_sensecap_https_task, "app_sensecap_https", stack_size, NULL, 3, task_stack, &g_task_tcb);

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle,
                                                             VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                                             __view_event_handler, NULL, NULL));

    return 0;
}

