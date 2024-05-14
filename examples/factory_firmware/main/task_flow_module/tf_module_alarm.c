#include "tf_module_alarm.h"
#include "tf_module_util.h"
#include <string.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"
#include <mbedtls/base64.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "tfm.alarm";
static void __data_lock( tf_module_alarm_t *p_module)
{

}
static void __data_unlock( tf_module_alarm_t *p_module)
{

}
static void __parmas_default(struct tf_module_alarm_params *p_params)
{
    p_params->duration = 10;
    p_params->rgb      = true;
    p_params->sound    = true;
}
static int __params_parse(struct tf_module_alarm_params *p_params, cJSON *p_json)
{
    cJSON *json_sound = cJSON_GetObjectItem(p_json, "sound");
    if (json_sound != NULL  && cJSON_IsNumber(json_sound)) {
        p_params->sound = json_sound->valueint;
    }

    cJSON *json_rgb = cJSON_GetObjectItem(p_json, "rgb");
    if (json_rgb != NULL  && cJSON_IsNumber(json_rgb)) {
        p_params->rgb = json_rgb->valueint;
    }

    cJSON *json_duration = cJSON_GetObjectItem(p_json, "duration");
    if (json_duration != NULL  && cJSON_IsNumber(json_duration)) {
        p_params->duration = json_duration->valueint;
    }
    return 0;
}

static void __timer_callback(void* p_arg)
{
    tf_module_alarm_t *p_module_ins = (tf_module_alarm_t *)p_arg;
    struct tf_module_alarm_params *p_params = &p_module_ins->params;
    if( p_params->rgb) {
        // TODO RGB OFF
        ESP_LOGI(TAG, "RGB OFF");
    }
    if( p_params->sound) {
        // TODO SOUND OFF
        ESP_LOGI(TAG, "SOUND OFF");
    }
}

static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    tf_module_alarm_t *p_module_ins = (tf_module_alarm_t *)handler_args;
    struct tf_module_alarm_params *p_params = &p_module_ins->params;

    uint8_t type = ((uint8_t *)p_event_data)[0];

    if( type !=  TF_DATA_TYPE_DUALIMAGE_WITH_AUDIO_TEXT) {
        ESP_LOGW(TAG, "unsupport type %d", type);
        tf_data_free(p_event_data);
        return;
    }

    if(p_params->rgb) {
        // TODO RGB ON
        ESP_LOGI(TAG, "RGB ON");
    }

    if(p_params->sound) {
        // TODO SOUND ON
        ESP_LOGI(TAG, "SOUND ON");
    }
    esp_timer_start_once(p_module_ins->timer_handle, p_params->duration * 1000000);

    tf_data_free(p_event_data);
}
/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    tf_module_alarm_t *p_module_ins = (tf_module_alarm_t *)p_module;
    return 0;
}
static int __stop(void *p_module)
{
    tf_module_alarm_t *p_module_ins = (tf_module_alarm_t *)p_module;
    esp_timer_stop(p_module_ins->timer_handle);
    esp_timer_delete(p_module_ins->timer_handle);
    __timer_callback((void *)p_module_ins); // stop alarm
    esp_err_t ret = tf_event_handler_unregister(p_module_ins->input_evt_id, __event_handler);
    return ret;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_alarm_t *p_module_ins = (tf_module_alarm_t *)p_module;
    __data_lock(p_module_ins);
    __parmas_default(&p_module_ins->params);
    __params_parse(&p_module_ins->params, p_json);
    __data_unlock(p_module_ins);
    return 0;
}
static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_alarm_t *p_module_ins = (tf_module_alarm_t *)p_module;
    esp_err_t ret;
    p_module_ins->input_evt_id = evt_id;
    ret = tf_event_handler_register(evt_id, __event_handler, p_module_ins);
    return ret;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_alarm_t *p_module_ins = (tf_module_alarm_t *)p_module;
    if ( num > 0) {
        ESP_LOGW(TAG, "only support output");
    }
    return 0;
}

static tf_module_t * __module_instance(void)
{
    tf_module_alarm_t *p_module_ins = (tf_module_alarm_t *) tf_malloc(sizeof(tf_module_alarm_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    memset(p_module_ins, 0, sizeof(tf_module_alarm_t));
    return tf_module_alarm_init(p_module_ins);
}

static  void __module_destroy(tf_module_t *handle)
{
    if( handle ) {
        free(handle->p_module);
    }
}

const static struct tf_module_ops  __g_module_ops = {
    .start = __start,
    .stop = __stop,
    .cfg = __cfg,
    .msgs_sub_set = __msgs_sub_set,
    .msgs_pub_set = __msgs_pub_set
};

const static struct tf_module_mgmt __g_module_mgmt = {
    .tf_module_instance = __module_instance,
    .tf_module_destroy = __module_destroy,
};

/*************************************************************************
 * API
 ************************************************************************/
tf_module_t * tf_module_alarm_init(tf_module_alarm_t *p_module_ins)
{
    esp_err_t ret = ESP_OK;
    if ( NULL == p_module_ins)
    {
        return NULL;
    }
#if CONFIG_ENABLE_TF_MODULE_ALARM_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    p_module_ins->module_serv.p_module = p_module_ins;
    p_module_ins->module_serv.ops = &__g_module_ops;
    
    __parmas_default(&p_module_ins->params);

    p_module_ins->input_evt_id = 0;

    const esp_timer_create_args_t timer_args = {
            .callback = &__timer_callback,
            .arg = (void*) p_module_ins,
            .name = "alarm timer"
    };
    ret = esp_timer_create(&timer_args, &p_module_ins->timer_handle);
    if(ret != ESP_OK) {
        return NULL;
    }

    return &p_module_ins->module_serv;
}

esp_err_t tf_module_alarm_register(void)
{
    return tf_module_register(TF_MODULE_ALARM_NAME,
                              TF_MODULE_ALARM_DESC,
                              TF_MODULE_ALARM_RVERSION,
                              &__g_module_mgmt);
}