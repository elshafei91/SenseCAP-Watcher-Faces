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
#include <mbedtls/base64.h>

#include "sensecap-watcher.h"
#include "util.h"
#include "uuid.h"
#include "app_audio_player.h"
#include "app_audio_recorder.h"
#include "app_rgb.h"
#include "factory_info.h"
#include "app_device_info.h"

static const char *TAG = "vi";

struct app_voice_interaction *gp_voice_interaction = NULL;

#define EVENT_RECORD_START        BIT0
#define EVENT_RECORD_STOP         BIT1
#define EVENT_VI_STOP             BIT2
#define EVENT_VI_EXIT             BIT3


static void __data_lock(struct app_voice_interaction  *p_vi)
{
    xSemaphoreTake(p_vi->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_voice_interaction *p_vi)
{
    xSemaphoreGive(p_vi->sem_handle);  
}

static void __vi_stop( struct app_voice_interaction *p_vi)
{
    xEventGroupSetBits(p_vi->event_group, EVENT_VI_STOP);
    if( p_vi->is_wait_resp  &&  p_vi->client != NULL ){
        ESP_LOGI(TAG, " stop wait resp");
        esp_http_client_cancel_request(p_vi->client);
    }
}

static void __vi_exit( struct app_voice_interaction *p_vi)
{
    xEventGroupSetBits(p_vi->event_group, EVENT_VI_EXIT);
}

static void __record_start( struct app_voice_interaction *p_vi)
{
    xEventGroupSetBits(p_vi->event_group, EVENT_RECORD_START);
    //Maybe it has already started
    if( p_vi->cur_status !=  VI_STATUS_IDLE ){
        ESP_LOGI(TAG, "vi not idle, stop it first");
        __vi_stop(p_vi);
    }
}

static void __record_stop( struct app_voice_interaction *p_vi)
{
    xEventGroupSetBits(p_vi->event_group, EVENT_RECORD_STOP);
}   

static bool __is_need_start_record(struct app_voice_interaction *p_vi, int ms)
{
    return !((xEventGroupWaitBits( p_vi->event_group, EVENT_RECORD_START, pdTRUE, pdFALSE, pdMS_TO_TICKS(ms)) & EVENT_RECORD_START) == 0 );
}

static bool __is_need_stop_record(struct app_voice_interaction *p_vi)
{
    return !((xEventGroupWaitBits( p_vi->event_group, EVENT_RECORD_STOP, pdTRUE, pdFALSE, 0) & EVENT_RECORD_STOP) == 0 );
}

static bool __is_need_stop(struct app_voice_interaction *p_vi)
{
    return !((xEventGroupWaitBits( p_vi->event_group, EVENT_VI_STOP, pdTRUE, pdFALSE, 0) & EVENT_VI_STOP) == 0 );
}
static bool __is_need_exit(struct app_voice_interaction *p_vi)
{
    return !((xEventGroupWaitBits( p_vi->event_group, EVENT_VI_EXIT, pdTRUE, pdFALSE, 0) & EVENT_VI_EXIT) == 0 );
}

static char * __default_token_gen(void)
{
    static char token[41] = {0};
    esp_err_t ret = ESP_OK;
    const char *eui = NULL;
    const char *key = NULL;
    size_t str_len = 0;
    size_t token_len = 0;
    char deviceinfo_buf[40];

    if( strlen(token) > 0 ) {
        return token;
    }

    eui = factory_info_eui_get();
    key = factory_info_ai_key_get();
    if( eui == NULL || key == NULL ) {
        ESP_LOGE(TAG, "EUI or key not set");
        return NULL;
    }

    memset(deviceinfo_buf, 0, sizeof(deviceinfo_buf));
    str_len = snprintf(deviceinfo_buf, sizeof(deviceinfo_buf), "%s:%s", eui, key);
    if( str_len >= 30 ) {
        ESP_LOGE(TAG, "EUI or key too long");
        return NULL;
    }
    ret = mbedtls_base64_encode(( uint8_t *)token, sizeof(token), &token_len, ( uint8_t *)deviceinfo_buf, str_len);
    if( ret != 0  ||  token_len < 40 ) {
        ESP_LOGE(TAG, "mbedtls_base64_encode failed:%d,", ret);
        return NULL;
    }
    return token;
}

static void __url_token_set(struct app_voice_interaction *p_vi)
{
    char *p_token = NULL, *p_host = NULL;

    local_service_cfg_type1_t local_svc_cfg = { .enable = false, .url = NULL };
    esp_err_t ret = get_local_service_cfg_type1(MAX_CALLER, CFG_ITEM_TYPE1_AUDIO_TASK_COMPOSER, &local_svc_cfg);
    if (ret == ESP_OK && local_svc_cfg.enable) {
        if (local_svc_cfg.url != NULL && strlen(local_svc_cfg.url) > 7) {
            ESP_LOGI(TAG, "got local service cfg, url=%s", local_svc_cfg.url);
            int len = strlen(local_svc_cfg.url);
            if (local_svc_cfg.url[len - 1] == '/') local_svc_cfg.url[len - 1] = '\0';  //remove trail '/'
            p_host = local_svc_cfg.url;
        }
        // token
        if (local_svc_cfg.token != NULL && strlen(local_svc_cfg.token) > 0) {
            ESP_LOGI(TAG, "got local service cfg, token=%s", local_svc_cfg.token);
            p_token = local_svc_cfg.token;
        }
    }

    // host
    if (p_host == NULL) p_host = CONFIG_TALK_SERV_HOST;
    snprintf(p_vi->stream_url, sizeof(p_vi->stream_url), "%s%s", p_host, CONFIG_TALK_AUDIO_STREAM_PATH);
    snprintf(p_vi->taskflow_url, sizeof(p_vi->taskflow_url), "%s%s", p_host, CONFIG_TASKFLOW_DETAIL_PATH);
    
    // token
    if (p_token == NULL) p_token = __default_token_gen();
    if (p_token) {
        snprintf(p_vi->token, sizeof(p_vi->token), "Device %s", p_token);
    } else {
        p_vi->token[0] = '\0';
    }
    
    if (local_svc_cfg.url != NULL) {
        free(local_svc_cfg.url);
    }
    if (local_svc_cfg.token != NULL) {
        free(local_svc_cfg.token);
    }
}

static int __audio_stream_http_connect(struct app_voice_interaction *p_vi)
{
    esp_err_t  ret = ESP_OK;
    ESP_LOGI(TAG, "URL: %s", p_vi->stream_url);
    esp_http_client_config_t config = {
        .url = p_vi->stream_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    p_vi->need_delete_client = true;
    p_vi->client = esp_http_client_init(&config);

    esp_http_client_set_header(p_vi->client, "Transfer-Encoding", "chunked");
    esp_http_client_set_header(p_vi->client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(p_vi->client, "session_id", p_vi->session_id);
    
    char *token =  p_vi->token;
    if( token !=NULL && strlen(token) > 0 ) {
        ESP_LOGI(TAG, "token: %s", token);
        esp_http_client_set_header(p_vi->client, "Authorization", token);
    }
    // when len=-1, will use transfer-encoding: chunked
    return esp_http_client_open(p_vi->client, -1);
}


// The conversation process is as follows:
// 1. Long press to wake up
// 2. Wake-up preprocessing (generate session ID, RGB blue breathing, play wake-up sound effect, pause task)
// 3. Start recording, establish http connection, send recording data, wait for recording to end
// 4. Wait for analysis results
// 5. Extract task results, read audio, play audio,
// 6. Single conversation ends, close connection
// 7. Exit (resume task, clear session ID)
static void __status_machine_handle(struct app_voice_interaction *p_vi)
{
    esp_err_t  ret = ESP_OK;
    switch (p_vi->cur_status) {
        case VI_STATUS_IDLE:{
            if(__is_need_exit(p_vi)) {
                p_vi->next_status = VI_STATUS_EXIT;
                break;
            }
            if(__is_need_start_record(p_vi, 20)) {
                // maybe have cached
                xEventGroupClearBits(p_vi->event_group, EVENT_RECORD_STOP | EVENT_VI_STOP | EVENT_VI_EXIT);
                p_vi->next_status = VI_STATUS_WAKE_START;
            } else {
                p_vi->next_status = VI_STATUS_IDLE;
            }
            break;
        }
        case VI_STATUS_WAKE_START: {
            ESP_LOGI(TAG, "VI_STATUS_WAKE_START");  
            if( p_vi->session_id[0] == 0 ) {
                UUIDGen( p_vi->session_id );
                ESP_LOGI(TAG, "session_id:%s", p_vi->session_id);
            }
            // pause taskflow TODO

            app_rgb_set(SR, RGB_BREATH_BLUE); //set RGB
            // TODO play wake sound
            p_vi->next_status = VI_STATUS_RECORDING;

            break;
        }
        case VI_STATUS_RECORDING: {
            ESP_LOGI(TAG, "VI_STATUS_RECORDING");
            enum app_voice_interaction_status next_status = VI_STATUS_ANALYZING;
            int64_t start = 0, end = 0;
            uint8_t *p_data = NULL;
            size_t data_len = 0;
            bool first = true;
            char chunk_size_str[32+3];
            size_t chunk_size_str_len = 0;
            size_t send_len = 0;
            int64_t net_send_tm_us = 0;
            esp_http_client_handle_t client;

            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                                    VIEW_EVENT_VI_RECORDING, NULL, NULL, pdMS_TO_TICKS(10000));

            app_audio_recorder_stream_start();
            ret = __audio_stream_http_connect(p_vi);
            if (ret != ESP_OK) {
                app_audio_recorder_stream_stop();
                ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(ret));
                p_vi->next_status = VI_STATUS_ERROR;
                p_vi->err_code = ESP_ERR_VI_HTTP_CONNECT;
                break;
            } else {
                client = p_vi->client;
            }

            start = esp_timer_get_time();
            while(1) {
                p_data = app_audio_recorder_stream_recv(&data_len, pdMS_TO_TICKS(500));
                if( p_data != NULL ) {
                    if( first ) {
                        first = false;
                        chunk_size_str_len = snprintf(chunk_size_str,sizeof(chunk_size_str), "%X\r\n", data_len);
                    } else {
                        chunk_size_str_len = snprintf(chunk_size_str,sizeof(chunk_size_str), "\r\n%X\r\n", data_len);
                    }
                    int64_t chunk_send_start= esp_timer_get_time();
                    send_len += esp_http_client_write(client, (const char *)chunk_size_str, chunk_size_str_len);
                    send_len += esp_http_client_write(client, (const char *)p_data, data_len);
                    int64_t chunk_send_end= esp_timer_get_time();
                    app_audio_recorder_stream_free((uint8_t *)p_data);
                    net_send_tm_us += (chunk_send_end - chunk_send_start);
                    ESP_LOGI(TAG, "send:%d", data_len);
                } else {
                    ESP_LOGI(TAG, "no data, wait for 10ms");
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                }
                if(__is_need_stop_record(p_vi)) {
                    app_audio_recorder_stream_stop();
                    ESP_LOGI(TAG, "EVENT_RECORD_STOP");
                    break;
                }
            }

            // The audio may not be sent completely, but the UI needs to show that it is being analyzed.
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                        VIEW_EVENT_VI_ANALYZING, NULL, NULL, pdMS_TO_TICKS(10000));

            // continue to send data from ringbuffer
            while(1) {
                p_data = app_audio_recorder_stream_recv(&data_len, pdMS_TO_TICKS(500));
                if( p_data != NULL ) {
                    chunk_size_str_len = snprintf(chunk_size_str,sizeof(chunk_size_str), "\r\n%X\r\n", data_len);
                    int64_t chunk_send_start= esp_timer_get_time();
                    send_len += esp_http_client_write(client, (const char *)chunk_size_str, chunk_size_str_len);
                    send_len += esp_http_client_write(client, (const char *)p_data, data_len);
                    int64_t chunk_send_end= esp_timer_get_time();
                    app_audio_recorder_stream_free((uint8_t *)p_data);
                    net_send_tm_us += (chunk_send_end - chunk_send_start);
                    ESP_LOGI(TAG, "send:%d", data_len);
                } else {
                    ESP_LOGI(TAG, "send finish");
                    break;
                }
            }
            send_len += esp_http_client_write(client, "\r\n0\r\n\r\n", strlen("\r\n0\r\n\r\n"));
            end = esp_timer_get_time();
            ESP_LOGI(TAG, " === Record stop === ");
            ESP_LOGI(TAG, "send:%d, time:%lld ms, Net rate:%.1fKB/s, average rate=%.1fKB/s, ", send_len,  (end - start) / 1000, \
                        (send_len * 976.5625) / net_send_tm_us,  (send_len * 976.5625) / (end - start));

            p_vi->next_status = next_status;
            break;
        }

        case VI_STATUS_ANALYZING: {
            ESP_LOGI(TAG, "VI_STATUS_ANALYZING");
            enum app_voice_interaction_status next_status = VI_STATUS_PLAYING;
            int64_t start = 0, end = 0;
            esp_http_client_handle_t client = p_vi->client;
            int code = 0;

            p_vi->is_wait_resp = true;
            start = esp_timer_get_time();
            int content_length = esp_http_client_fetch_headers(client); //TODO how to stop?
            if (esp_http_client_is_chunked_response(client))
            {
                ESP_LOGI(TAG, "chunk data");
                esp_http_client_get_chunk_length(client, &content_length);
            }
            end = esp_timer_get_time();
            code = esp_http_client_get_status_code(client);
            p_vi->is_wait_resp = false;

            ESP_LOGI(TAG, "code=%d, content_length=%d, time=%lld ms", code, content_length, (end - start) / 1000);
            
            if( __is_need_stop(p_vi)) {
                ESP_LOGI(TAG, "STOP analy");
                next_status = VI_STATUS_STOP;
            } else if( content_length >= 0  &&  code == 200 ) {
                p_vi->content_length = content_length;
                next_status = VI_STATUS_PLAYING;
            } else {
                ESP_LOGE(TAG, "response error");
                next_status = VI_STATUS_ERROR;
                p_vi->err_code = ESP_ERR_VI_HTTP_RESP;
            }
            p_vi->next_status = next_status;
            break;
        }

        case VI_STATUS_PLAYING: {
            ESP_LOGI(TAG, "VI_STATUS_PLAYING");

            enum app_voice_interaction_status next_status = VI_STATUS_FINISH;
            int64_t start = 0, end = 0;
            esp_http_client_handle_t client = p_vi->client;
            int read_len = 0;
            size_t read_total_len = 0;
            int64_t net_read_tm_us = 0;
            size_t chunk_len = AUDIO_PLAYER_RINGBUF_CHUNK_SIZE * 2;
            int content_length = p_vi->content_length;
            bool  first_start = true;
            int   cache_len =  MIN( content_length/2, AUDIO_PLAYER_RINGBUF_CACHE_SIZE);
            struct view_data_vi_result  result; //TODO
            int player_status = 0;

            memset(&result, 0, sizeof(result));

            char* recv_buf = (char *)psram_malloc(chunk_len);
            if (recv_buf == NULL) {
                ESP_LOGE(TAG, "psram_malloc failed");
                next_status = VI_STATUS_ERROR;
                p_vi->err_code = ESP_ERR_VI_NO_MEM;
                p_vi->next_status = next_status;
                break;
            }
            app_rgb_set(SR, RGB_FLARE_BLUE);    

            start = esp_timer_get_time();
            app_audio_player_stream_init(content_length);
            while (read_total_len < content_length) {
                int64_t read_start= esp_timer_get_time();
                read_len = esp_http_client_read(client, recv_buf, chunk_len);
                int64_t read_end= esp_timer_get_time();
                net_read_tm_us = (read_end - read_start) + net_read_tm_us;

                if (read_len <= 0) {
                    ESP_LOGI(TAG, "recv:%d", read_len);
                    break;
                } else {
                    ESP_LOGI(TAG, "recv:%d", read_len);
                    read_total_len += read_len;
                    app_audio_player_stream_send((uint8_t *)recv_buf, read_len, pdMS_TO_TICKS(500));
                }
                if(__is_need_stop(p_vi)) {
                    ESP_LOGI(TAG, "stop play");
                    app_audio_player_stream_stop();
                    next_status = VI_STATUS_STOP;
                    break;
                }
                if( read_total_len >= cache_len  && first_start) {
                    first_start = false;
                    app_audio_player_stream_start();
                    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                            VIEW_EVENT_VI_PLAYING, &result,  sizeof(result), pdMS_TO_TICKS(10000));
                }
            }
            free(recv_buf);
            app_audio_player_stream_finish();
            end = esp_timer_get_time();

            ESP_LOGI(TAG, " === Download end === ");
            ESP_LOGI(TAG, "recv:%d, time:%lld ms, Net rate:%.1fKB/s, average rate=%.1fKB/s, ", read_total_len,  (end - start) / 1000, \
                        (read_total_len * 976.5625) / net_read_tm_us,  (read_total_len * 976.5625) / (end - start));
            
            // wait play finish
            while(1) {
                player_status = app_audio_player_status_get();
                if( player_status == AUDIO_PLAYER_STATUS_PLAYING_STREAM ) {
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                } else {
                    break;
                }
                if(__is_need_stop(p_vi)) {
                    ESP_LOGI(TAG, "stop play");
                    app_audio_player_stream_stop();
                    next_status = VI_STATUS_STOP;
                    break;
                }
            }

            p_vi->next_status = next_status;
            break;
        }
        case VI_STATUS_STOP:
        case VI_STATUS_FINISH: {
            ESP_LOGI(TAG, "VI_STATUS_FINISH");
            esp_http_client_handle_t client = p_vi->client;

            app_rgb_set(SR, RGB_OFF);

            if( p_vi->need_delete_client && client != NULL) {
                p_vi->need_delete_client = false;
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                p_vi->client = NULL;
            }
            p_vi->next_status = VI_STATUS_IDLE;
            break;
        }
        case VI_STATUS_EXIT: {
            ESP_LOGI(TAG, "VI_STATUS_EXIT");
            memset(p_vi->session_id, 0, sizeof(p_vi->session_id));
            p_vi->next_status = VI_STATUS_IDLE;
            //TODO: resume taskflow
            break;
        }
        case VI_STATUS_ERROR: {
            ESP_LOGI(TAG, "VI_STATUS_ERROR");
            esp_http_client_handle_t client = p_vi->client;
            if( p_vi->need_delete_client && client != NULL) {
                p_vi->need_delete_client = false;
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                p_vi->client = NULL;
            }
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, \
                        VIEW_EVENT_VI_ERROR, p_vi->err_code, sizeof(p_vi->err_code), pdMS_TO_TICKS(10000));

            p_vi->next_status = VI_STATUS_IDLE;
            break;
        }
        default: {
            ESP_LOGW(TAG, "unknown status:%d", p_vi->cur_status);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            p_vi->next_status = VI_STATUS_IDLE;
            break; 
        }
    }

    if( p_vi->next_status != p_vi->cur_status ) {
        p_vi->cur_status = p_vi->next_status;
    }
}

static void app_voice_interaction_task(void *p_arg)
{
    struct app_voice_interaction *p_vi = (struct app_voice_interaction *)p_arg;
    EventBits_t bits;
    while(1) {
        __status_machine_handle(p_vi);
    }
}

/*************************************************************************
 * callback or event handle
 ************************************************************************/

static void __long_press_event_cb(void)
{
    ESP_LOGI(TAG, "long_press_event_cb");
    struct app_voice_interaction *p_vi = (struct app_voice_interaction *)gp_voice_interaction;
    __record_start(p_vi);
}

static void __long_release_event_cb(void)
{
    ESP_LOGI(TAG, "long_release_event_cb");
    struct app_voice_interaction *p_vi = (struct app_voice_interaction *)gp_voice_interaction;
    __record_stop(p_vi);
}

static void __event_handler(void* handler_args, 
                                 esp_event_base_t base, 
                                 int32_t id, 
                                 void* event_data)
{
    struct app_voice_interaction *p_vi = (struct app_voice_interaction *)handler_args;

     if( base == VIEW_EVENT_BASE) {
        switch (id) {
            case VIEW_EVENT_WIFI_ST:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
                struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
                if (p_st->is_network) {
                    p_vi->net_flag = true;
                } else {
                    p_vi->net_flag = false;
                }
                break;
            }
            case VIEW_EVENT_VI_STOP:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_STOP");
                __vi_stop(p_vi);
                break;
            }
            case VIEW_EVENT_VI_EXIT:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_VI_EXIT");
                __vi_exit(p_vi);
                break;
            }
        default:
            break;
        }

    } else if( base == CTRL_EVENT_BASE ) {
        switch (id)
        {
            case CTRL_EVENT_LOCAL_SVC_CFG_PUSH2TALK:
            {
                ESP_LOGI(TAG, "event: CTRL_EVENT_LOCAL_SVC_CFG_PUSH2TALK");
                __url_token_set(p_vi);
                break;
            }
            case CTRL_EVENT_VI_RECORD_WAKEUP:
            {   
                ESP_LOGI(TAG, "event: CTRL_EVENT_VI_RECORD_WAKEUP");
                __record_start(p_vi);
                break;
            }
            case CTRL_EVENT_VI_RECORD_STOP:
            {
                ESP_LOGI(TAG, "event: CTRL_EVENT_VI_RECORD_STOP");
                __record_stop(p_vi);
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
    struct app_voice_interaction * p_vi = NULL;
    gp_voice_interaction = (struct app_voice_interaction *) psram_malloc(sizeof(struct app_voice_interaction));
    if (gp_voice_interaction == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    p_vi = gp_voice_interaction;
    memset(p_vi, 0, sizeof( struct app_voice_interaction ));
    
    __url_token_set(p_vi); 

    p_vi->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_vi->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_vi->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_vi->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");

    p_vi->p_task_stack_buf = (StackType_t *)psram_malloc(VOICE_INTERACTION_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_vi->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_vi->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_vi->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_vi->task_handle = xTaskCreateStaticPinnedToCore(app_voice_interaction_task,
                                                                "app_voice_interaction",
                                                                VOICE_INTERACTION_TASK_STACK_SIZE,
                                                                (void *)p_vi,
                                                                VOICE_INTERACTION_TASK_PRIO,
                                                                p_vi->p_task_stack_buf,
                                                                p_vi->p_task_buf,
                                                                VOICE_INTERACTION_TASK_CORE);
    ESP_GOTO_ON_FALSE(p_vi->task_handle, ESP_FAIL, err, TAG, "Failed to create task");

    // view event
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_WIFI_ST, 
                                                    __event_handler, 
                                                    p_vi));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_VI_STOP, 
                                                    __event_handler, 
                                                    p_vi));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    VIEW_EVENT_BASE, 
                                                    VIEW_EVENT_VI_EXIT, 
                                                    __event_handler, 
                                                    p_vi));

    // ctrl event
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    CTRL_EVENT_BASE, 
                                                    CTRL_EVENT_LOCAL_SVC_CFG_PUSH2TALK, 
                                                    __event_handler,
                                                    p_vi));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    CTRL_EVENT_BASE, 
                                                    CTRL_EVENT_VI_RECORD_WAKEUP, 
                                                    __event_handler,
                                                    p_vi));
    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                    CTRL_EVENT_BASE, 
                                                    CTRL_EVENT_VI_RECORD_STOP, 
                                                    __event_handler,
                                                    p_vi));

    bsp_set_btn_long_press_cb(__long_press_event_cb);
    bsp_set_btn_long_release_cb(__long_release_event_cb);

    return ESP_OK;

err:
    if(p_vi->task_handle ) {
        vTaskDelete(p_vi->task_handle);
        p_vi->task_handle = NULL;
    }
    if( p_vi->p_task_stack_buf ) {
        free(p_vi->p_task_stack_buf);
        p_vi->p_task_stack_buf = NULL;
    }
    if( p_vi->p_task_buf ) {   
        free(p_vi->p_task_buf);
        p_vi->p_task_buf = NULL;
    }
    if (p_vi->event_group) {
        vEventGroupDelete(p_vi->event_group);
        p_vi->event_group = NULL;
    }
    if (p_vi->sem_handle) {
        vSemaphoreDelete(p_vi->sem_handle);
        p_vi->sem_handle = NULL;
    }
    if (p_vi) {
        free(p_vi);
        gp_voice_interaction = NULL;
    }
    ESP_LOGE(TAG, "app_voice_interaction_init fail %d!", ret);
    return ret;
}


int app_vi_result_parse(const char *p_str, size_t len,
                        struct view_data_vi_result *p_ret)
{
    esp_err_t ret = ESP_OK;
    cJSON *p_json_root = NULL;
    cJSON *p_code = NULL;
    cJSON *p_data = NULL;

    memset(p_ret, 0, sizeof(struct view_data_vi_result));

    p_json_root = cJSON_ParseWithLength(p_str, len);
    ESP_GOTO_ON_FALSE(p_json_root, ESP_ERR_INVALID_ARG, err, TAG, "json parse failed");

    p_code = cJSON_GetObjectItem(p_json_root, "type");
    if( p_code == NULL || cJSON_IsNumber(p_code)) {
        ESP_LOGE(TAG, "type is not number");
        goto err;
    } else {
        ESP_LOGI(TAG, "code:%d", p_code->valueint);
    }

    p_data = cJSON_GetObjectItem(p_json_root, "data");
    if( p_data == NULL || !cJSON_IsObject(p_data)) {
        ESP_LOGE(TAG, "data is not object");
        goto err;
    }

    cJSON *p_mode = cJSON_GetObjectItem(p_data, "mode");
    if ( p_mode && cJSON_IsNumber(p_mode)) {
        p_ret->mode = p_mode->valueint;
    } else {
        p_ret->mode = VI_MODE_CHAT;
    }

    cJSON *p_screen_text = cJSON_GetObjectItem(p_data, "screen_text");
    if ( p_screen_text && cJSON_IsString(p_screen_text)) {
        p_ret->p_audio_text = strdup(p_screen_text->valuestring);
    }

    cJSON *p_task_summary = cJSON_GetObjectItem(p_data, "task_summary");
    if ( p_task_summary && cJSON_IsObject(p_task_summary)) {

        cJSON *p_object = cJSON_GetObjectItem(p_task_summary, "object");
        if ( p_object && cJSON_IsString(p_object)) {
            p_ret->items[TASK_CFG_ID_OBJECT] = strdup(p_object->valuestring);
        }

        cJSON *p_condition = cJSON_GetObjectItem(p_task_summary, "condition");
        if ( p_condition && cJSON_IsString(p_condition)) {
            p_ret->items[TASK_CFG_ID_CONDITION] = strdup(p_condition->valuestring);
        }

        cJSON *p_behavior = cJSON_GetObjectItem(p_task_summary, "behavior");
        if ( p_behavior && cJSON_IsString(p_behavior)) {
            p_ret->items[TASK_CFG_ID_BEHAVIOR] = strdup(p_behavior->valuestring);
        }

        cJSON *p_feature = cJSON_GetObjectItem(p_task_summary, "feature");
        if ( p_feature && cJSON_IsString(p_feature)) {
            p_ret->items[TASK_CFG_ID_FEATURE] = strdup(p_feature->valuestring);
        }

        cJSON *p_notification = cJSON_GetObjectItem(p_task_summary, "notification");
        if ( p_notification && cJSON_IsString(p_notification)) {
            p_ret->items[TASK_CFG_ID_NOTIFICATION]= strdup(p_notification->valuestring);
        }

        cJSON *p_time = cJSON_GetObjectItem(p_task_summary, "time");
        if ( p_time && cJSON_IsString(p_time)) {
            p_ret->items[TASK_CFG_ID_TIME] = strdup(p_time->valuestring);
        }

        cJSON *p_frequency = cJSON_GetObjectItem(p_task_summary, "frequency");
        if ( p_frequency && cJSON_IsString(p_frequency)) {
            p_ret->items[TASK_CFG_ID_FREQUENCY] = strdup(p_frequency->valuestring);
        }
    }

err:
    if (p_json_root != NULL) {
        cJSON_Delete(p_json_root);
    }
    return 0;
}

int app_vi_result_free(struct view_data_vi_result *p_ret)
{
    if (p_ret->p_audio_text) {
        free(p_ret->p_audio_text);
        p_ret->p_audio_text = NULL;
    }

    for (int i = 0; i < TASK_CFG_ID_MAX; i++) {
        if (p_ret->items[i]) {
            free(p_ret->items[i]);
            p_ret->items[i] = NULL;
        }
    }
    return 0;

}