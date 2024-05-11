
#pragma once
#include "tf_module.h"
#include "tf_module_util.h"
#include "tf_module_data_type.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "tf_module_ai_camera.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_MODULE_IMG_ANALYZER_RNAME "img_analyzer"
#define TF_MODULE_IMG_ANALYZER_RVERSION "1.0.0"
#define TF_MODULE_IMG_ANALYZER_RDESC "img_analyzer module"

#define TF_MODULE_IMG_ANALYZER_TASK_STACK_SIZE 1024 * 5
#define TF_MODULE_IMG_ANALYZER_TASK_PRIO       3
#define TF_MODULE_IMG_ANALYZER_QUEUE_SIZE      5

#define TF_MODULE_IMG_ANALYZER_TYPE_RECOGNIZE    0  // Analyze pictures
#define TF_MODULE_IMG_ANALYZER_TYPE_MONITORING   1  // Monitor behavior

#define TF_MODULE_IMG_ANALYZER_SERV_TYPE_SENSECRAFT   0
#define TF_MODULE_IMG_ANALYZER_SERV_TYPE_PROXY        1

#define CONFIG_TF_MODULE_IMG_ANALYZER_SERV_HOST       "http://172.22.1.107:8810"
#define CONFIG_TF_MODULE_IMG_ANALYZER_SERV_REQ_PATH   "/v1/watcher/vision"

struct tf_module_img_analyzer_params
{
    char *p_prompt;
    char *p_audio_txt;
    int type;
};

struct tf_module_img_analyzer_config
{
    int serv_type;
    char url[256];  //proxy server url
    char head[256]; //proxy server https head
};

struct tf_module_img_analyzer_result
{
    int status;
    char *p_audio;
    char *p_img;
};


typedef struct tf_module_img_analyzer
{
    tf_module_t module_serv;
    int input_evt_id;
    int *p_output_evt_id;
    int output_evt_num;
    struct tf_module_img_analyzer_params params;
    struct tf_module_img_analyzer_config configs;
    SemaphoreHandle_t sem_handle;
    EventGroupHandle_t event_group;
    QueueHandle_t queue_handle;
    TaskHandle_t task_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    char url[256];
} tf_module_img_analyzer_t;

tf_module_t * tf_module_img_analyzer_init(tf_module_img_analyzer_t *p_module_ins);

esp_err_t tf_module_img_analyzer_register(void);

#ifdef __cplusplus
}
#endif
