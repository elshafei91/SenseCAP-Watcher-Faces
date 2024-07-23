#include "app_audio_player.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "util.h"
#include "app_device_info.h"

static const char *TAG = "audio_player";

struct app_audio_player *gp_audio_player = NULL;


static void __data_lock(struct app_audio_player  *p_audio_player)
{
    xSemaphoreTake(p_audio_player->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_audio_player *p_audio_player)
{
    xSemaphoreGive(p_audio_player->sem_handle);  
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
    ret = bsp_codec_volume_set(get_sound(MAX_CALLER), NULL);
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
    
    while(1) {

        switch (p_audio_player->status)
        {
            case AUDIO_PLAYER_STATUS_IDLE:{
                vTaskDelay(10 / portTICK_PERIOD_MS);
                break;
            }
            case AUDIO_PLAYER_STATUS_PLAYING_STREAM:{

                UBaseType_t available_bytes = 0;
                uint8_t *p_data = NULL;
                size_t recv_len = 0;
                size_t data_write = 0; // useless
                
                // TODO catche 
                vRingbufferGetInfo( p_audio_player->rb_handle, NULL, NULL, NULL, NULL, &available_bytes);
                if( available_bytes== 0 ) {
                    // ESP_LOGI(TAG, "no data");
                    __data_lock(p_audio_player);
                    if( p_audio_player->stream_finished) {
                        p_audio_player->status = AUDIO_PLAYER_STATUS_IDLE;
                    } 
                    __data_unlock(p_audio_player);
                    vTaskDelay(200 / portTICK_PERIOD_MS);
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
                    } 
                    __data_unlock(p_audio_player);
                }
                break;
            }
            default:
                vTaskDelay(10 / portTICK_PERIOD_MS);
                break;
        }
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
    return ESP_OK;
}

esp_err_t app_audio_player_stream_start(size_t len)
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
    p_audio_player->stream_finished = true;
    __data_unlock(p_audio_player);

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

    return ESP_OK;
}

esp_err_t app_audio_player_mem(uint8_t *p_buf, size_t len)
{

    return ESP_OK;
}

