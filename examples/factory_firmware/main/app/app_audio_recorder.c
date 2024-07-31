#include "app_audio_recorder.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "sensecap-watcher.h"
#include "util.h"

static const char *TAG = "audio_recorder";

struct app_audio_recorder *gp_audio_recorder = NULL;

#define EVENT_RECORD_STREAM_START     BIT0
#define EVENT_RECORD_STREAM_STOP       BIT1
#define EVENT_RECORD_STREAM_STOP_DONE  BIT2

static void __data_lock(struct app_audio_recorder  *p_audio_recorder)
{
    xSemaphoreTake(p_audio_recorder->sem_handle, portMAX_DELAY);
}
static void __data_unlock(struct app_audio_recorder *p_audio_recorder)
{
    xSemaphoreGive(p_audio_recorder->sem_handle);  
}

static void app_audio_recorder_task(void *p_arg)
{
    struct app_audio_recorder *p_audio_recorder = (struct app_audio_recorder *)p_arg;
    EventBits_t bits = 0;
    while(1) {
        
        bits = xEventGroupWaitBits(p_audio_recorder->event_group, 
                EVENT_RECORD_STREAM_START, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000));
        
        if(bits & EVENT_RECORD_STREAM_START) {
            ESP_LOGI(TAG, "EVENT_RECORD_STREAM_START");
            
            int16_t *audio_buffer = psram_malloc( AUDIO_RECORDER_RINGBUF_CHUNK_SIZE );

            while(1) {
                bits = xEventGroupWaitBits(p_audio_recorder->event_group, 
                        EVENT_RECORD_STREAM_STOP, pdTRUE, pdTRUE, 0);
                if(bits & EVENT_RECORD_STREAM_STOP) {
                    ESP_LOGI(TAG, "EVENT_RECORD_STREAM_STOP");
                    break;
                }

                bsp_get_feed_data(false, audio_buffer, AUDIO_RECORDER_RINGBUF_CHUNK_SIZE);
                if(xRingbufferSend(p_audio_recorder->rb_handle, audio_buffer, AUDIO_RECORDER_RINGBUF_CHUNK_SIZE, 0) != pdTRUE) {
                    ESP_LOGE(TAG, "xRingbufferSend failed");
                }
            }

            free(audio_buffer);
            xEventGroupSetBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_STOP_DONE);
        }
    }
}

/*************************************************************************
 * API
 ************************************************************************/

esp_err_t app_audio_recorder_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    esp_err_t ret = ESP_OK;
    struct app_audio_recorder * p_audio_recorder = NULL;
    gp_audio_recorder = (struct app_audio_recorder *) psram_malloc(sizeof(struct app_audio_recorder));
    if (gp_audio_recorder == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    p_audio_recorder = gp_audio_recorder;
    memset(p_audio_recorder, 0, sizeof( struct app_audio_recorder ));
    
    p_audio_recorder->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_audio_recorder->p_rb_storage = (uint8_t *)psram_malloc(AUDIO_RECORDER_RINGBUF_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->p_rb_storage, ESP_ERR_NO_MEM, err, TAG, "Failed to create rb storage");

    p_audio_recorder->rb_handle = xRingbufferCreateStatic(AUDIO_RECORDER_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, p_audio_recorder->p_rb_storage, &p_audio_recorder->rb_ins);
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->rb_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create rb");

    p_audio_recorder->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");

    p_audio_recorder->p_task_stack_buf = (StackType_t *)psram_malloc(AUDIO_RECORDER_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_audio_recorder->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_audio_recorder->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_audio_recorder->task_handle = xTaskCreateStaticPinnedToCore(app_audio_recorder_task,
                                                                "app_audio_recorder",
                                                                AUDIO_RECORDER_TASK_STACK_SIZE,
                                                                (void *)p_audio_recorder,
                                                                AUDIO_RECORDER_TASK_PRIO,
                                                                p_audio_recorder->p_task_stack_buf,
                                                                p_audio_recorder->p_task_buf,
                                                                AUDIO_RECORDER_TASK_CORE);
    ESP_GOTO_ON_FALSE(p_audio_recorder->task_handle, ESP_FAIL, err, TAG, "Failed to create task");

    return ESP_OK;  
err:
    if(p_audio_recorder->task_handle ) {
        vTaskDelete(p_audio_recorder->task_handle);
        p_audio_recorder->task_handle = NULL;
    }
    if( p_audio_recorder->p_task_stack_buf ) {
        free(p_audio_recorder->p_task_stack_buf);
        p_audio_recorder->p_task_stack_buf = NULL;
    }
    if( p_audio_recorder->p_task_buf ) {   
        free(p_audio_recorder->p_task_buf);
        p_audio_recorder->p_task_buf = NULL;
    }
    if (p_audio_recorder->event_group) {
        vEventGroupDelete(p_audio_recorder->event_group);
        p_audio_recorder->event_group = NULL;
    }
    if( p_audio_recorder->rb_handle ) {
        vRingbufferDelete(p_audio_recorder->rb_handle);
        p_audio_recorder->rb_handle = NULL;
    }
    if( p_audio_recorder->p_rb_storage ) {
        free(p_audio_recorder->p_rb_storage);
        p_audio_recorder->p_rb_storage = NULL;
    }   
    if (p_audio_recorder->sem_handle) {
        vSemaphoreDelete(p_audio_recorder->sem_handle);
        p_audio_recorder->sem_handle = NULL;
    }
    if (p_audio_recorder) {
        free(p_audio_recorder);
        gp_audio_recorder = NULL;
    }
    ESP_LOGE(TAG, "app_audio_recorder_init fail %d!", ret);
    return ret;
}

int app_audio_recorder_status_get(void)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return AUDIO_RECORDER_STATUS_IDLE;
    }
    return p_audio_recorder->status;
}

esp_err_t app_audio_recorder_stop(void)
{
    return ESP_OK;
}

esp_err_t app_audio_recorder_stream_start(void)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    void *tmp = NULL;
    size_t len = 0;

    //clear the ringbuffer
    while ((tmp = xRingbufferReceiveUpTo(p_audio_recorder->rb_handle, &len, 0, AUDIO_RECORDER_RINGBUF_SIZE))) {
        vRingbufferReturnItem(p_audio_recorder->rb_handle, tmp);
    }

    ret = bsp_codec_set_fs( DRV_AUDIO_SAMPLE_RATE, 
                            DRV_AUDIO_SAMPLE_BITS, 
                            DRV_AUDIO_CHANNELS);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bsp_codec_mute_set(true);
    if (ret != ESP_OK) {
        return ret;
    }

    __data_lock(p_audio_recorder);
    p_audio_recorder->status = AUDIO_RECORDER_STATUS_STREAM;
    __data_unlock(p_audio_recorder);

    xEventGroupClearBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_STOP | EVENT_RECORD_STREAM_STOP_DONE);
    xEventGroupSetBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_START);
    return ret;
}

uint8_t *app_audio_recorder_stream_recv(size_t *p_recv_len,
                                        TickType_t xTicksToWait)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return NULL;
    }
    return xRingbufferReceiveUpTo( p_audio_recorder->rb_handle, p_recv_len, xTicksToWait, AUDIO_RECORDER_RINGBUF_CHUNK_SIZE);
}

esp_err_t app_audio_recorder_stream_free(uint8_t *p_data)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return ESP_FAIL;
    }
    vRingbufferReturnItem(p_audio_recorder->rb_handle, p_data);
    return ESP_OK;
}


esp_err_t app_audio_recorder_stream_stop(void)
{
    struct app_audio_recorder * p_audio_recorder = gp_audio_recorder;
    if( p_audio_recorder == NULL) {
        return ESP_FAIL;
    }

    __data_lock(p_audio_recorder);
    p_audio_recorder->status = AUDIO_RECORDER_STATUS_IDLE;
    __data_unlock(p_audio_recorder);

    xEventGroupSetBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_STOP);
    xEventGroupWaitBits(p_audio_recorder->event_group, EVENT_RECORD_STREAM_STOP_DONE, 1, 1, pdMS_TO_TICKS(1000));

    // don't clear ringbuffer

    bsp_codec_dev_stop(); //TODO
    return ESP_OK;
}

esp_err_t app_audio_recorder_file_start(void *p_filepath)
{
    return ESP_OK;
}

esp_err_t app_audio_recorder_file_end(void)
{
    return ESP_OK;
}