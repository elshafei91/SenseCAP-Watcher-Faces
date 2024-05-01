
#pragma once
#include "tf_module.h"
#include "tf_module_util.h"
#include "tf_module_data_type.h"
#include "esp_err.h"
#include "indoor_ai_camera.h"
#include "sscma_client_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_MODULE_AI_CAMERA_NAME "ai_camera"
#define TF_MODULE_AI_CAMERA_VERSION "1.0.0"
#define TF_MODULE_AI_CAMERA_DESC "ai_camera module"

/*************************************************************************
 * param config define
 ************************************************************************/

#define TF_MODULE_AI_CAMERA_MODES_SAMPLE     0
#define TF_MODULE_AI_CAMERA_MODES_INFERENCE  1

#define TF_MODULE_AI_CAMERA_MODEL_TYPE_URL           0
#define TF_MODULE_AI_CAMERA_MODEL_TYPE_LOCAL_PERSON  1  
#define TF_MODULE_AI_CAMERA_MODEL_TYPE_LOCAL_APPLE   2
#define TF_MODULE_AI_CAMERA_MODEL_TYPE_LOCAL_GESTURE 3

#define TF_MODULE_AI_CAMERA_SHUTTER_DISABLE             0
#define TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_BY_UI       1
#define TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_BY_INPUT    2


#define TF_MODULE_AI_CAMERA_CONDITION_TYPE_LESS      0
#define TF_MODULE_AI_CAMERA_CONDITION_TYPE_EQUAL     1
#define TF_MODULE_AI_CAMERA_CONDITION_TYPE_GREATER   2
#define TF_MODULE_AI_CAMERA_CONDITION_TYPE_NOT_EQUAL 3

#define TF_MODULE_AI_CAMERA_CONDITION_MODE_PRESENCE_DETECTION   0
#define TF_MODULE_AI_CAMERA_CONDITION_MODE_VALUE_COMPARE        1
#define TF_MODULE_AI_CAMERA_CONDITION_MODE_NUM_CHANGE           2

struct tf_module_ai_camera_model
{
    char id[64]; // TODO max len?
    char url[256];
    int size;
    int select;
};

struct tf_module_ai_camera_condition
{
    char class_name[64];
    int type;
    int num;
    int mode;
};

struct tf_module_ai_camera_silent_period
{
    int repeat[7];
    char time_period[2][9];
    int silence_duration;
};

struct tf_module_ai_camera_params
{
    int mode;
    struct tf_module_ai_camera_model model;
    struct tf_module_ai_camera_condition *conditions;
    int condition_num;
    struct tf_module_ai_camera_silent_period silent_period;
    int shutter;
};

/*************************************************************************
 *  data define
 ************************************************************************/

enum tf_module_ai_camera_inference_type {
    AI_CAMERA_INFERENCE_TYPE_BOX = 0,
    AI_CAMERA_INFERENCE_TYPE_CLASS,
    AI_CAMERA_INFERENCE_TYPE_POINT
};

union tf_module_ai_camera_inference_data  {
    sscma_client_box_t   *p_box;
    sscma_client_class_t *p_class;
    sscma_client_point_t *p_point;
};

struct tf_module_ai_camera_inference_info
{
    enum tf_module_ai_camera_inference_type   type;
    union tf_module_ai_camera_inference_data  data;
    uint32_t cnt;
    char *classes[80];
};

struct tf_module_ai_camera_preview_info
{
    int    mode;
    struct tf_data_image                      img_preview;
    struct tf_module_ai_camera_inference_info inference;
};

typedef struct tf_module_ai_camera
{
    tf_module_t module_serv;
    int input_evt_id;
    int *p_output_evt_id;
    int output_evt_num;
    sscma_client_handle_t  sscma_client_handle;
    struct tf_module_ai_camera_params params;
    SemaphoreHandle_t sem_handle;
    bool shutter_trigger;
} tf_module_ai_camera_t;

tf_module_t * tf_module_ai_camera_init(tf_module_ai_camera_t *p_module_ins);

esp_err_t tf_module_ai_camera_register(void);

#ifdef __cplusplus
}
#endif
