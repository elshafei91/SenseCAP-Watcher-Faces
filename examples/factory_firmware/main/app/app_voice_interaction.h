#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "data_defs.h"

#define VOICE_INTERACTION_TASK_STACK_SIZE  5*1024
#define VOICE_INTERACTION_TASK_PRIO        11
#define VOICE_INTERACTION_TASK_CORE        0

enum app_voice_interaction_status {
    VOICE_INTERACTION_STATUS_IDLE = 0,
};

struct app_voice_interaction {
    SemaphoreHandle_t sem_handle;
    EventGroupHandle_t event_group;
    TaskHandle_t task_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    enum app_voice_interaction_status status;
    bool net_flag;
};

esp_err_t app_voice_interaction_init(void);


