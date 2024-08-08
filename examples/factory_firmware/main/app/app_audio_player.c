#include "app_audio_player.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "util.h"
#include "app_device_info.h"
#include "audio_player.h"
#include "storage.h"

static const char *TAG = "audio_player";

struct app_audio_player *gp_audio_player = NULL;

#define EVENT_STREAM_START      BIT0
#define EVENT_STREAM_STOP       BIT1
#define EVENT_STREAM_STOP_DONE  BIT2
#define EVENT_FILE_MEM_START    BIT3
#define EVENT_FILE_MEM_DONE     BIT4


static int __volume_get(void)
{
    return get_sound(MAX_CALLER);
}

static void __data_lock(struct app_audio_player  *p_audio_player)
{
    xSemaphoreTake(p_audio_player->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_audio_player *p_audio_player)
{
    xSemaphoreGive(p_audio_player->sem_handle);  
}

static esp_err_t __audio_player_mute_set(AUDIO_PLAYER_MUTE_SETTING setting)
{
    bsp_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);
    // restore the voice volume upon unmuting
    if (setting == AUDIO_PLAYER_UNMUTE) {
        bsp_codec_volume_set(__volume_get(), NULL);
    }
    return ESP_OK;
}

static esp_err_t __audio_player_set_fs(uint32_t sample_rate,
                                        uint8_t  bits_per_sample,
                                        i2s_slot_mode_t  channel)
{
    esp_err_t ret = ESP_OK;

    ret = bsp_codec_set_fs( sample_rate, 
                            bits_per_sample, 
                            channel);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bsp_codec_mute_set(true);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bsp_codec_mute_set(false);
    if (ret != ESP_OK) {
        return ret;
    }    
    ret = bsp_codec_volume_set(__volume_get(), NULL);
    if (ret != ESP_OK) {
        return ret;
    }
    return ESP_OK;
}

static bool __is_wav(struct app_audio_player *p_audio_player, uint8_t *p_buf, size_t len) 
{
    esp_err_t ret = ESP_OK;

    if(len < sizeof(audio_wav_header_t)) {
        return false;
    }

    //may have other data? TODO
    audio_wav_header_t *wav_head = ( audio_wav_header_t *)p_buf;
    if((NULL == strstr((char *)wav_head->ChunkID, "RIFF")) ||
        (NULL == strstr((char *)wav_head->Format, "WAVE"))) {
        return false;
    }
    ESP_LOGI(TAG,"sample_rate=%d, channels=%d, bps=%d",
                wav_head->SampleRate,
                wav_head->NumChannels,
                wav_head->BitsPerSample);

    if( wav_head->SampleRate !=  p_audio_player->sample_rate || 
        wav_head->NumChannels != p_audio_player->channel ||
        wav_head->BitsPerSample != p_audio_player->bits_per_sample) {

        ESP_LOGI(TAG, "need change fs");
        __data_lock(p_audio_player);
        p_audio_player->sample_rate = wav_head->SampleRate;
        p_audio_player->channel = wav_head->NumChannels;
        p_audio_player->bits_per_sample = wav_head->BitsPerSample;
        __data_unlock(p_audio_player);

        ret = __audio_player_set_fs(p_audio_player->sample_rate, p_audio_player->bits_per_sample, p_audio_player->channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "set fs fail %d!", ret);
        }
    }
        
    return true;
}


static void app_audio_player_task(void *p_arg)
{
    struct app_audio_player *p_audio_player = (struct app_audio_player *)p_arg;
    EventBits_t bits = 0;
    while(1) {
        bits = xEventGroupWaitBits(p_audio_player->event_group, 
                            EVENT_STREAM_START, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
        if(bits & EVENT_STREAM_START) {
            ESP_LOGI(TAG, "EVENT_STREAM_START");

            UBaseType_t available_bytes = 0;
            uint8_t *p_data = NULL;
            size_t recv_len = 0;
            size_t data_write = 0; // useless
            bool is_play_done = false;

            while(1) {
                bits = xEventGroupWaitBits(p_audio_player->event_group, 
                                        EVENT_STREAM_STOP, pdTRUE, pdTRUE, pdMS_TO_TICKS(0));
                if(bits & EVENT_STREAM_STOP) {
                    ESP_LOGI(TAG, "EVENT_STREAM_STOP");
                    break;
                }
                // xRingbufferReceiveUpTo and vRingbufferReturnItem can be interrupted by other tasks.
                p_data = xRingbufferReceiveUpTo( p_audio_player->rb_handle, &recv_len, pdMS_TO_TICKS(500), AUDIO_PLAYER_RINGBUF_CHUNK_SIZE);
                if(p_data != NULL) {
                    bsp_i2s_write(p_data, recv_len, &data_write, 0); // maybe take 500ms to write data
                    vRingbufferReturnItem(p_audio_player->rb_handle, p_data);

                    __data_lock(p_audio_player);
                    p_audio_player->stream_play_len += recv_len;
                    __data_unlock(p_audio_player);

                } else {
                    //maybe finished
                    __data_lock(p_audio_player);
                    if( p_audio_player->stream_finished) {
                        p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
                        is_play_done = true;
                    }
                    __data_unlock(p_audio_player);
                }

                if(is_play_done) {  
                    ESP_LOGI(TAG, "play done");
                    break;
                }
            }
        }
    }
}


static void __audio_player_cb(audio_player_cb_ctx_t *ctx)
{   
    struct app_audio_player *p_audio_player = (struct app_audio_player *)ctx->user_ctx;
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
        ESP_LOGI(TAG, "Player IDLE");
        __data_lock(p_audio_player);
        if( p_audio_player->mem_need_free && p_audio_player->p_mem_buf != NULL) {
            ESP_LOGI(TAG, "free mem");
            free(p_audio_player->p_mem_buf);
            p_audio_player->mem_need_free = false;
            p_audio_player->p_mem_buf = NULL;
        }
        p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
        __data_unlock(p_audio_player);
        
        xEventGroupSetBits(p_audio_player->event_group, EVENT_FILE_MEM_DONE);

        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_COMPLETED_PLAYING_NEXT:
        ESP_LOGI(TAG, "Player NEXT");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        ESP_LOGI(TAG, "Player PLAYING");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        ESP_LOGI(TAG, "Player PAUSE");
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_SHUTDOWN:
        ESP_LOGI(TAG, "Player SHUTDOWN");
        break;
    default:
        break;
    }
}

/*************************************************************************
 * API
 ************************************************************************/

esp_err_t app_audio_player_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    esp_err_t ret = ESP_OK;
    struct app_audio_player * p_audio_player = NULL;
    gp_audio_player = (struct app_audio_player *) psram_malloc(sizeof(struct app_audio_player));
    if (gp_audio_player == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    p_audio_player = gp_audio_player;
    memset(p_audio_player, 0, sizeof( struct app_audio_player ));
    
    p_audio_player->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_audio_player->p_rb_storage = (uint8_t *)psram_malloc(AUDIO_PLAYER_RINGBUF_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->p_rb_storage, ESP_ERR_NO_MEM, err, TAG, "Failed to create rb storage");

    p_audio_player->rb_handle = xRingbufferCreateStatic(AUDIO_PLAYER_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, p_audio_player->p_rb_storage, &p_audio_player->rb_ins);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->rb_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create rb");

    p_audio_player->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");

    p_audio_player->p_task_stack_buf = (StackType_t *)psram_malloc(AUDIO_PLAYER_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_audio_player->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_audio_player->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_audio_player->task_handle = xTaskCreateStaticPinnedToCore(app_audio_player_task,
                                                                "app_audio_player",
                                                                AUDIO_PLAYER_TASK_STACK_SIZE,
                                                                (void *)p_audio_player,
                                                                AUDIO_PLAYER_TASK_PRIO,
                                                                p_audio_player->p_task_stack_buf,
                                                                p_audio_player->p_task_buf,
                                                                AUDIO_PLAYER_TASK_CORE);
    ESP_GOTO_ON_FALSE(p_audio_player->task_handle, ESP_FAIL, err, TAG, "Failed to create task");


    audio_player_config_t config = { .mute_fn = __audio_player_mute_set,
                                    .write_fn = bsp_i2s_write,
                                    .clk_set_fn = __audio_player_set_fs,
                                    .priority = 13
                                };
    ret = audio_player_new(config);
    ESP_GOTO_ON_FALSE(ret==ESP_OK, ESP_FAIL, err, TAG, "Failed to create audio player");
    audio_player_callback_register(__audio_player_cb, (void *)p_audio_player);

    return ESP_OK;  
err:
    if(p_audio_player->task_handle ) {
        vTaskDelete(p_audio_player->task_handle);
        p_audio_player->task_handle = NULL;
    }
    if( p_audio_player->p_task_stack_buf ) {
        free(p_audio_player->p_task_stack_buf);
        p_audio_player->p_task_stack_buf = NULL;
    }
    if( p_audio_player->p_task_buf ) {   
        free(p_audio_player->p_task_buf);
        p_audio_player->p_task_buf = NULL;
    }
    if (p_audio_player->event_group) {
        vEventGroupDelete(p_audio_player->event_group);
        p_audio_player->event_group = NULL;
    }
    if( p_audio_player->rb_handle ) {
        vRingbufferDelete(p_audio_player->rb_handle);
        p_audio_player->rb_handle = NULL;
    }
    if( p_audio_player->p_rb_storage ) {
        free(p_audio_player->p_rb_storage);
        p_audio_player->p_rb_storage = NULL;
    }   
    if (p_audio_player->sem_handle) {
        vSemaphoreDelete(p_audio_player->sem_handle);
        p_audio_player->sem_handle = NULL;
    }
    if (p_audio_player) {
        free(p_audio_player);
        gp_audio_player = NULL;
    }
    ESP_LOGE(TAG, "app_audio_player_init fail %d!", ret);
    return ret;
}

int app_audio_player_status_get(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return AUDIO_PLAYER_STATUS_IDLE;
    }
    return p_audio_player->status;
}

esp_err_t app_audio_player_stop(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    __data_lock(p_audio_player);
    if( p_audio_player->status != AUDIO_PLAYER_STATUS_IDLE) {
        xEventGroupSetBits(p_audio_player->event_group, EVENT_STREAM_STOP); // TODO
    }
    p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
    __data_unlock(p_audio_player);
    return ESP_OK;
}

esp_err_t app_audio_player_stream_init(size_t len)
{ 
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    void *tmp = NULL;
    size_t tmp_len = 0;

    //clear the ringbuffer
    while ((tmp = xRingbufferReceiveUpTo(p_audio_player->rb_handle, &tmp_len, 0, AUDIO_PLAYER_RINGBUF_SIZE))) {
        vRingbufferReturnItem(p_audio_player->rb_handle, tmp);
    }

    ret = __audio_player_set_fs(DRV_AUDIO_SAMPLE_RATE, DRV_AUDIO_SAMPLE_BITS, DRV_AUDIO_CHANNELS );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set fs fail %d!", ret);
    }

    __data_lock(p_audio_player);
    p_audio_player->status = AUDIO_PLAYER_STATUS_PLAYING_STREAM;
    p_audio_player->audio_type = AUDIO_TYPE_UNKNOWN;
    p_audio_player->sample_rate = DRV_AUDIO_SAMPLE_RATE;
    p_audio_player->bits_per_sample = DRV_AUDIO_SAMPLE_BITS;
    p_audio_player->channel = DRV_AUDIO_CHANNELS;
    p_audio_player->stream_finished = false;
    p_audio_player->stream_total_len = len;
    p_audio_player->stream_play_len = 0;
    p_audio_player->stream_need_cache = true; // start need cache
    __data_unlock(p_audio_player);

    return ret;
}


esp_err_t app_audio_player_stream_start(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(p_audio_player->event_group, EVENT_STREAM_STOP);
    xEventGroupSetBits(p_audio_player->event_group, EVENT_STREAM_START);
    return ESP_OK;
}

esp_err_t app_audio_player_stream_send(uint8_t *p_buf, 
                                        size_t len, 
                                        TickType_t xTicksToWait)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    switch (p_audio_player->audio_type)
    {
        case AUDIO_TYPE_UNKNOWN: {

            if( __is_wav(p_audio_player, p_buf, len) ) {
                ESP_LOGI(TAG, "WAV audio stream");
                p_audio_player->audio_type = AUDIO_TYPE_WAV;
                int header_len = sizeof(audio_wav_header_t);
                if(xRingbufferSend(p_audio_player->rb_handle, p_buf + header_len, len - header_len, xTicksToWait) == pdTRUE) {
                    return ESP_FAIL;
                }
            } else {
                ESP_LOGE(TAG, "unsupport audio stream");
            }
            break;
        }
        case AUDIO_TYPE_WAV: {
            if(xRingbufferSend(p_audio_player->rb_handle, p_buf, len, xTicksToWait) != pdTRUE) {
                return ESP_FAIL;
            }
            break;
        }  
        case AUDIO_TYPE_MP3: {
            ESP_LOGE(TAG, "unsupport mp3 by stream");
            //TODO need to decode
            break;
        }  
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t app_audio_player_stream_finish(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    __data_lock(p_audio_player);
    p_audio_player->stream_finished = true;
    __data_unlock(p_audio_player);

    return ESP_OK;
}

esp_err_t app_audio_player_stream_stop(void)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }

    __data_lock(p_audio_player);
    p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
    __data_unlock(p_audio_player);

    xEventGroupSetBits(p_audio_player->event_group, EVENT_STREAM_STOP);
    // xEventGroupWaitBits(p_audio_player->event_group, EVENT_STREAM_STOP_DONE, 1, 1, pdMS_TO_TICKS(1000));

    //clear the ringbuffer
    void *tmp = NULL;
    size_t len = 0;
    while ((tmp = xRingbufferReceiveUpTo(p_audio_player->rb_handle, &len, 0, AUDIO_PLAYER_RINGBUF_SIZE))) {
        vRingbufferReturnItem(p_audio_player->rb_handle, tmp);
    }
    bsp_codec_dev_stop(); //TODO
    return ESP_OK;
}

esp_err_t app_audio_player_file(void *p_filepath)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    FILE *fp = NULL;
    storage_file_open(p_filepath, &fp);
    // fp = fopen(p_filepath, "r");
    if( fp ) {
        esp_err_t status  = audio_player_play(fp);
        if( status == ESP_OK ) {
            __data_lock(p_audio_player);
            p_audio_player->status = AUDIO_PLAYER_STATUS_PLAYING_FILE;
            __data_unlock(p_audio_player);
            ESP_LOGI(TAG, "play file %s", p_filepath);
            return ESP_OK;
        }
    } else {
        ESP_LOGE(TAG, "open file %s fail", p_filepath);
    }
    return ESP_FAIL;
}

esp_err_t app_audio_player_file_block(void *p_filepath, TickType_t xTicksToWait)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(p_audio_player->event_group, EVENT_FILE_MEM_DONE);
    esp_err_t ret = app_audio_player_file(p_filepath);
    if( ret == ESP_OK ) {
        EventBits_t bits = xEventGroupWaitBits(p_audio_player->event_group, 
                            EVENT_FILE_MEM_DONE, pdTRUE, pdTRUE, xTicksToWait);
        if( !(bits & EVENT_FILE_MEM_DONE)) {
            ESP_LOGW(TAG, "play timeout");
            audio_player_stop();
            __data_lock(p_audio_player);
            p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
            __data_unlock(p_audio_player); 
        }
    }
    return ret;
}

esp_err_t app_audio_player_mem(uint8_t *p_buf, size_t len, bool is_need_free)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    FILE *fp = NULL;
    fp = fmemopen((void *)p_buf, len, "rb");
    if( fp ) {
        esp_err_t status  = audio_player_play(fp);
        if( status == ESP_OK ) {
            __data_lock(p_audio_player);
            p_audio_player->status = AUDIO_PLAYER_STATUS_PLAYING_MEM;
            p_audio_player->mem_need_free = is_need_free;
            p_audio_player->p_mem_buf = p_buf;
            __data_unlock(p_audio_player);
            ESP_LOGI(TAG, "play mem: %d", len);
            return ESP_OK;
        }
    } else {
        ESP_LOGE(TAG, "open mem fail");
    }
    return ESP_FAIL;
}

esp_err_t app_audio_player_mem_block(uint8_t *p_buf, size_t len, bool is_need_free, TickType_t xTicksToWait)
{
    struct app_audio_player * p_audio_player = gp_audio_player;
    if( p_audio_player == NULL) {
        return ESP_FAIL;
    }
    xEventGroupClearBits(p_audio_player->event_group, EVENT_FILE_MEM_DONE);
    esp_err_t ret = app_audio_player_mem(p_buf,len, is_need_free);
    if( ret == ESP_OK ) {
        EventBits_t bits = xEventGroupWaitBits(p_audio_player->event_group, 
                            EVENT_FILE_MEM_DONE, pdTRUE, pdTRUE, xTicksToWait);
        if( !(bits & EVENT_FILE_MEM_DONE)) {
            ESP_LOGW(TAG, "play timeout");
            audio_player_stop();
            
            __data_lock(p_audio_player);
            if( p_audio_player->mem_need_free && p_audio_player->p_mem_buf != NULL) {
                free(p_audio_player->p_mem_buf);
                p_audio_player->mem_need_free = false;
                p_audio_player->p_mem_buf = NULL;
            }
            p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
            __data_unlock(p_audio_player);
        }
    }
    return ret;
}
