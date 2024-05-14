
#pragma once
#include "tf_module.h"
#include "tf_module_data_type.h"
#include "esp_err.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_MODULE_ALARM_NAME     "alarm"
#define TF_MODULE_ALARM_RVERSION "1.0.0"
#define TF_MODULE_ALARM_DESC     "alarm module"

#define TF_MODULE_ALARM_DEFAULT_AUDIO_FILE  "xxx.wav"

struct tf_module_alarm_params
{
    bool rgb;
    bool sound;
    int  duration; //seconds
};

typedef struct tf_module_alarm
{
    tf_module_t module_serv;
    int input_evt_id; // no output
    struct tf_module_alarm_params params;
    esp_timer_handle_t timer_handle;
} tf_module_alarm_t;

tf_module_t * tf_module_alarm_init(tf_module_alarm_t *p_module_ins);

esp_err_t tf_module_alarm_register(void);

#ifdef __cplusplus
}
#endif
