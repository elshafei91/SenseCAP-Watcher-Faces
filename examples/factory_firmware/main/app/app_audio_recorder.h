#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"

#define AUDIO_RECORDER_TASK_STACK_SIZE  5*1024
#define AUDIO_RECORDER_TASK_PRIO        13
#define AUDIO_RECORDER_TASK_CORE        1

// sample rate: 16000, bit depth: 16, channels: 1; 32000 size per second;
// 5*32000  can cache 5s of audio. 
#define AUDIO_RECORDER_RINGBUF_SIZE         5*32000  //TODO
#define AUDIO_RECORDER_RINGBUF_CHUNK_SIZE   16000

enum app_audio_recorder_status {
    AUDIO_RECORDER_STATUS_IDLE = 0,
    AUDIO_RECORDER_STATUS_FILE,
    AUDIO_RECORDER_STATUS_STREAM,
};

struct app_audio_recorder {
    SemaphoreHandle_t sem_handle;
    RingbufHandle_t rb_handle;
    StaticRingbuffer_t rb_ins;
    uint8_t * p_rb_storage;
    EventGroupHandle_t event_group;
    TaskHandle_t task_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    enum app_audio_recorder_status status;
};

esp_err_t app_audio_recorder_init(void);

int app_audio_recorder_status_get(void);

esp_err_t app_audio_recorder_stop(void);

esp_err_t app_audio_recorder_stream_start(void);

uint8_t *app_audio_recorder_stream_recv(size_t *p_recv_len,
                                        TickType_t xTicksToWait);

esp_err_t app_audio_recorder_stream_free(uint8_t *p_data);

esp_err_t app_audio_recorder_stream_stop(void);

esp_err_t app_audio_recorder_file_start(void *p_filepath);
esp_err_t app_audio_recorder_file_end(void);