#include "app_voice_interaction.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "event_loops.h" 

#include "sensecap-watcher.h"
#include "util.h"
#include "app_audio_player.h"
#include "app_audio_recorder.h"

static const char *TAG = "voice_interaction";

struct app_voice_interaction *gp_voice_interaction = NULL;

#define EVENT_RECORD_START        BIT0
#define EVENT_RECORD_STOP         BIT1


static void __data_lock(struct app_voice_interaction  *p_voice_interaction)
{
    xSemaphoreTake(p_voice_interaction->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_voice_interaction *p_voice_interaction)
{
    xSemaphoreGive(p_voice_interaction->sem_handle);  
}

static char *__request( const char *url,
                        esp_http_client_method_t method, 
                        const char *token, 
                        const char *session_id,
                        uint8_t *data, size_t len)
{
    esp_err_t  ret = ESP_OK;
    char *result = NULL;

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // set header
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "session_id", session_id);
    if( token !=NULL && strlen(token) > 0 ) {
        ESP_LOGI(TAG, "token: %s", token);
        esp_http_client_set_header(client, "Authorization", token);
    }

    ret = esp_http_client_open(client, len);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to open client!");

    if (len > 0)
    {
        int wlen = esp_http_client_write(client, (const char *)data, len);
        ESP_GOTO_ON_FALSE(wlen >= 0, ESP_FAIL, err, TAG, "Failed to write client!");
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (esp_http_client_is_chunked_response(client))
    {
        ESP_LOGI(TAG, "chunked response");
        esp_http_client_get_chunk_length(client, &content_length);
    }
    ESP_LOGI(TAG, "content_length=%d", content_length);
    ESP_GOTO_ON_FALSE(content_length >= 0, ESP_FAIL, err, TAG, "HTTP client fetch headers failed!");

    result = (char *)psram_malloc(content_length + 1);
    ESP_GOTO_ON_FALSE(NULL != result, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc:%d", content_length+1);

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
        // ESP_LOGD(TAG, "content: %s, size: %d", result, strlen(result));
    }
err:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result != NULL ? result : NULL;
}

static char *__audio_stream_request(struct app_voice_interaction *p_voice_interaction,
                                    const char *url,
                                    esp_http_client_method_t method, 
                                    const char *token, 
                                    const char *session_id)
{
    esp_err_t  ret = ESP_OK;
    char *result = NULL;
    int64_t start = 0, end = 0;
    ESP_LOGI(TAG, "URL: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // set header
    esp_http_client_set_header(client, "Transfer-Encoding", "chunked");
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");


    if( session_id !=NULL && strlen(session_id) > 0 ) {
        ESP_LOGI(TAG, "token: %s", session_id);
        esp_http_client_set_header(client, "session_id", session_id);
    }
    
    if( token !=NULL && strlen(token) > 0 ) {
        ESP_LOGI(TAG, "token: %s", token);
        esp_http_client_set_header(client, "Authorization", token);
    }
    
    // when len=-1, will use transfer-encoding: chunked
    ret = esp_http_client_open(client, -1);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "Failed to open client!");


    // record audio
    uint8_t *p_data = NULL;
    size_t data_len = 0;
    bool first = true;
    char chunk_size_str[32+3];
    size_t chunk_size_str_len = 0;
    size_t send_len = 0;

    start = esp_timer_get_time();
    app_audio_recorder_stream_start();
    do {
        
        p_data = app_audio_recorder_stream_recv(&data_len, pdMS_TO_TICKS(500));
        if( p_data != NULL ) {
            if( first ) {
                first = false;
                chunk_size_str_len = snprintf(chunk_size_str,sizeof(chunk_size_str), "%X\r\n", data_len);
            } else {
                chunk_size_str_len = snprintf(chunk_size_str,sizeof(chunk_size_str), "\r\n%X\r\n", data_len);
            }
            send_len += esp_http_client_write(client, (const char *)chunk_size_str, chunk_size_str_len);
            send_len += esp_http_client_write(client, (const char *)p_data, data_len);
            app_audio_recorder_stream_free((uint8_t *)p_data);
            ESP_LOGI(TAG, "send:%d", data_len);
        }
    } while ( (xEventGroupWaitBits( p_voice_interaction->event_group,EVENT_RECORD_STOP, pdTRUE, pdFALSE, 0) & EVENT_RECORD_STOP) == 0 );
    send_len += esp_http_client_write(client, "\r\n0\r\n\r\n", strlen("\r\n0\r\n\r\n"));
    app_audio_recorder_stream_stop();

    end = esp_timer_get_time();
    ESP_LOGI(TAG, "record stop, send:%d, time:%lld ms", send_len, (end - start) / 1000);


    // wait response
    start = esp_timer_get_time();
    int content_length = esp_http_client_fetch_headers(client);
    if (esp_http_client_is_chunked_response(client))
    {
        ESP_LOGI(TAG, "chunk data");
        esp_http_client_get_chunk_length(client, &content_length);
    }
    end = esp_timer_get_time();
    ESP_LOGI(TAG, "content_length=%d, time=%lld ms", content_length, (end - start) / 1000);
    ESP_GOTO_ON_FALSE(content_length >= 0, ESP_FAIL, err, TAG, "HTTP client fetch headers failed!");


    // read data
    int read_len = 0;
    size_t read_total_len = 0;
    size_t chunk_len = AUDIO_PLAYER_RINGBUF_CHUNK_SIZE * 2;
    char* recv_buf = (char *)psram_malloc(chunk_len);
    
    start = esp_timer_get_time();
    app_audio_player_stream_start(content_length);
    while (read_total_len <= content_length) {
        read_len = esp_http_client_read(client, recv_buf, chunk_len);
        if (read_len <= 0) {
            ESP_LOGE(TAG, "esp_http_client_read=%d", read_len);
            break;
        } else {
            ESP_LOGI(TAG, "recv:%d", read_len);
            read_total_len += read_len;
            app_audio_player_stream_send((uint8_t *)recv_buf, read_len, pdMS_TO_TICKS(500));
        }
    }
    free(recv_buf);
    app_audio_player_stream_finish();
    end = esp_timer_get_time();
    ESP_LOGI(TAG, "read stop, read:%d, time:%lld ms", read_total_len, (end - start) / 1000);

err:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return result != NULL ? result : NULL;
}



static void app_voice_interaction_task(void *p_arg)
{
    struct app_voice_interaction *p_voice_interaction = (struct app_voice_interaction *)p_arg;
    EventBits_t bits;
    while(1) {
        bits = xEventGroupWaitBits(p_voice_interaction->event_group, \
                EVENT_RECORD_START, pdTRUE, pdFALSE, pdMS_TO_TICKS(1));
        if( ( bits & EVENT_RECORD_START ) != 0 ) {
            ESP_LOGI(TAG, "EVENT_RECORD_START");
            __audio_stream_request(p_voice_interaction,
                                    "https://sensecap-watcher-demo.seeed.cc/api/v2/watcher/talk/audio_stream",
                                    HTTP_METHOD_POST,
                                    NULL,
                                    NULL);
        }
    }
}


static void __long_press_event_cb(void)
{
    ESP_LOGI(TAG, "long_press_event_cb");
    struct app_voice_interaction *p_voice_interaction = (struct app_voice_interaction *)gp_voice_interaction;

    xEventGroupSetBits(p_voice_interaction->event_group, EVENT_RECORD_START);
}

static void __long_release_event_cb(void)
{
    ESP_LOGI(TAG, "long_release_event_cb");
    struct app_voice_interaction *p_voice_interaction = (struct app_voice_interaction *)gp_voice_interaction;
    xEventGroupSetBits(p_voice_interaction->event_group, EVENT_RECORD_STOP);
}

static void __event_loop_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    struct app_voice_interaction *p_voice_interaction = (struct app_voice_interaction *)handler_args;

     if( base == VIEW_EVENT_BASE) {
        switch (id) {
            case VIEW_EVENT_WIFI_ST:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
                struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
                if (p_st->is_network) {
                    p_voice_interaction->net_flag = true;
                } else {
                    p_voice_interaction->net_flag = false;
                }
                break;
            }
        default:
            break;
        }
    }
}

/*************************************************************************
 * API
 ************************************************************************/

esp_err_t app_voice_interaction_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    esp_err_t ret = ESP_OK;
    struct app_voice_interaction * p_voice_interaction = NULL;
    gp_voice_interaction = (struct app_voice_interaction *) psram_malloc(sizeof(struct app_voice_interaction));
    if (gp_voice_interaction == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    p_voice_interaction = gp_voice_interaction;
    memset(p_voice_interaction, 0, sizeof( struct app_voice_interaction ));
    
    p_voice_interaction->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_voice_interaction->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_voice_interaction->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_voice_interaction->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");

    p_voice_interaction->p_task_stack_buf = (StackType_t *)psram_malloc(VOICE_INTERACTION_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_voice_interaction->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_voice_interaction->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_voice_interaction->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_voice_interaction->task_handle = xTaskCreateStaticPinnedToCore(app_voice_interaction_task,
                                                                "app_voice_interaction",
                                                                VOICE_INTERACTION_TASK_STACK_SIZE,
                                                                (void *)p_voice_interaction,
                                                                VOICE_INTERACTION_TASK_PRIO,
                                                                p_voice_interaction->p_task_stack_buf,
                                                                p_voice_interaction->p_task_buf,
                                                                VOICE_INTERACTION_TASK_CORE);
    ESP_GOTO_ON_FALSE(p_voice_interaction->task_handle, ESP_FAIL, err, TAG, "Failed to create task");

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle,
                                                             VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
                                                             __event_loop_handler, p_voice_interaction, NULL));

    bsp_set_btn_long_press_cb(__long_press_event_cb);
    bsp_set_btn_long_release_cb(__long_release_event_cb);

    return ESP_OK;  
err:
    if(p_voice_interaction->task_handle ) {
        vTaskDelete(p_voice_interaction->task_handle);
        p_voice_interaction->task_handle = NULL;
    }
    if( p_voice_interaction->p_task_stack_buf ) {
        free(p_voice_interaction->p_task_stack_buf);
        p_voice_interaction->p_task_stack_buf = NULL;
    }
    if( p_voice_interaction->p_task_buf ) {   
        free(p_voice_interaction->p_task_buf);
        p_voice_interaction->p_task_buf = NULL;
    }
    if (p_voice_interaction->event_group) {
        vEventGroupDelete(p_voice_interaction->event_group);
        p_voice_interaction->event_group = NULL;
    }
    if (p_voice_interaction->sem_handle) {
        vSemaphoreDelete(p_voice_interaction->sem_handle);
        p_voice_interaction->sem_handle = NULL;
    }
    if (p_voice_interaction) {
        free(p_voice_interaction);
        gp_voice_interaction = NULL;
    }
    ESP_LOGE(TAG, "app_voice_interaction_init fail %d!", ret);
    return ret;
}
