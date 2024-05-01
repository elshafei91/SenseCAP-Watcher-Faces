#include "tf_module_ai_camera.h"
#include <string.h>
#include <time.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"
#include "sscma_client_io.h"
#include "sscma_client_ops.h"

static const char *TAG = "tfm.ai_camera";

tf_module_t *g_handle = NULL;

static void __data_lock( tf_module_ai_camera_t *p_module)
{
    xSemaphoreTake(p_module->sem_handle, portMAX_DELAY);
}
static void __data_unlock( tf_module_ai_camera_t *p_module)
{
    xSemaphoreGive(p_module->sem_handle);  
}

static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)handler_args;
    tf_module_data_type_err_handle(p_event_data);
    
    if (p_module_ins->params.shutter != TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_BY_INPUT) {
        return;
    }

    __data_lock(p_module_ins);
    p_module_ins->shutter_trigger = true;
    __data_unlock(p_module_ins);
}

static int __class_num_get(struct tf_module_ai_camera_inference_info *p_inference, const char *p_class_name, uint8_t score)
{
    uint8_t target = 0;
    
    if (p_inference->classes[0] != NULL)
    {
        for (int i = 0; p_inference->classes[i] != NULL; i++)
        {
            if(strcmp(p_inference->classes[i], p_class_name) == 0) {
                target = i;
                break;
            }
        }
    } else {
        return 0;
    }

    int cnt = 0;

    switch (p_inference->type)
    {
    case AI_CAMERA_INFERENCE_TYPE_BOX:{
        for (int i = 0; i < p_inference->cnt; i++)
        {
            if(p_inference->data.p_box[i].target == target && p_inference->data.p_box[i].score > score) {
                cnt++;
            }
        }
        break;
    }
    case AI_CAMERA_INFERENCE_TYPE_CLASS:{
        for (int i = 0; i < p_inference->cnt; i++)
        {
            if(p_inference->data.p_class[i].target == target && p_inference->data.p_class[i].score > score) {
                cnt++;
            }
        }
        break;
    }
    default:
        break;
    }
    return cnt;
}
static bool __condition_check(struct tf_module_ai_camera_params         *p_params,  
                              struct tf_module_ai_camera_inference_info *p_inference)
{
    if( p_params->mode ==  TF_MODULE_AI_CAMERA_MODES_SAMPLE ) {
        return true;
    }
    for(int i = 0; i < p_params->condition_num; i++) {
        
        struct tf_module_ai_camera_condition condition = p_params->conditions[i];
        int cnt = 0;
        cnt = __class_num_get(p_inference, condition.class_name, 0); // is score need to be judged？
        
        // TODO filter
        switch (condition.mode)
        {
            case TF_MODULE_AI_CAMERA_CONDITION_MODE_PRESENCE_DETECTION:
            {
                if(cnt > 0) {
                    return true;
                }
                break;
            }
            case TF_MODULE_AI_CAMERA_CONDITION_MODE_VALUE_COMPARE:
            {
                switch (condition.type)
                {
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_LESS: {
                    if( cnt < condition.num ) {
                        return true;
                    }
                    break;
                }
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_EQUAL: {
                    if( cnt == condition.num ) {
                        return true;
                    }
                    break;
                }
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_GREATER: {
                    if( cnt > condition.num ) {
                        return true;
                    }
                    break;
                }
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_NOT_EQUAL: {
                    if( cnt != condition.num ) {
                        return true;
                    }
                    break;
                }
                default:
                    break;
                }
                break;
            }
            case TF_MODULE_AI_CAMERA_CONDITION_MODE_NUM_CHANGE:
            {
       
                break;
            }
            default:
                break;
        }

    }
}

static bool __silent_period_check(struct tf_module_ai_camera_params  *p_params)
{
    //TODO
    return false;
}
static bool __output_check(tf_module_ai_camera_t *p_module_ins, struct tf_module_ai_camera_inference_info *p_inference)
{
    struct tf_module_ai_camera_params *p_params = &p_module_ins->params;

    bool condition_check = true;

    // condition check
    if( (p_params->mode  == TF_MODULE_AI_CAMERA_MODES_INFERENCE)  && p_inference ) {
        if( !__condition_check(p_params, p_inference)) {
            return false;
        }
    }
    // shutter check
    if ( p_params->shutter != TF_MODULE_AI_CAMERA_SHUTTER_DISABLE ) {
        if( !p_module_ins->shutter_trigger ) {
            return false;
        }
    }
    // silent period check
    if( !__silent_period_check(p_params)) {
        return false;
    }
    
    return true;
}

static void sscma_on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)user_ctx;

    //TODO
}

static int __params_parse(struct tf_module_ai_camera_params *p_params, cJSON *p_json)
{

    cJSON *modes_json = cJSON_GetObjectItem(p_json, "modes");
    if (modes_json != NULL && cJSON_IsNumber(modes_json))
    {
        p_params->mode = modes_json->valueint;
    }
    else
    {
        p_params->mode = 0;
    }

    cJSON *model_json = cJSON_GetObjectItem(p_json, "model");
    if (model_json != NULL)
    {
        cJSON *id_json = cJSON_GetObjectItem(model_json, "id");
        cJSON *url_json = cJSON_GetObjectItem(model_json, "url");
        cJSON *size_json = cJSON_GetObjectItem(model_json, "size");
        cJSON *select_json = cJSON_GetObjectItem(model_json, "select");

        if (id_json && cJSON_IsString(id_json) &&
            url_json && cJSON_IsString(url_json) &&
            size_json && cJSON_IsNumber(size_json) &&
            select_json && cJSON_IsNumber(select_json))
        {

            strncpy(p_params->model.id, id_json->valuestring, sizeof(p_params->model.id) - 1);
            strncpy(p_params->model.url, url_json->valuestring, sizeof(p_params->model.url) - 1);
            p_params->model.size = size_json->valueint;
            p_params->model.select = select_json->valueint;
        }
    }

    cJSON *conditions_json = cJSON_GetObjectItem(p_json, "conditions");
    if (conditions_json != NULL && cJSON_IsArray(conditions_json))
    {
        p_params->condition_num = cJSON_GetArraySize(conditions_json);
        p_params->conditions = (struct tf_module_ai_camera_condition *)tf_malloc(p_params->condition_num * sizeof(struct tf_module_ai_camera_condition));
        for (int i = 0; i < p_params->condition_num; i++)
        {
            cJSON *condition_json = cJSON_GetArrayItem(conditions_json, i);
            if (condition_json)
            {
                cJSON *class_name_json = cJSON_GetObjectItem(condition_json, "class");
                cJSON *type_json = cJSON_GetObjectItem(condition_json, "type");
                cJSON *num_json = cJSON_GetObjectItem(condition_json, "num");
                cJSON *mode_json = cJSON_GetObjectItem(condition_json, "mode");

                if (class_name_json && cJSON_IsString(class_name_json) &&
                    type_json && cJSON_IsNumber(type_json) &&
                    num_json && cJSON_IsNumber(num_json) &&
                    mode_json && cJSON_IsNumber(mode_json))
                {

                    strncpy(p_params->conditions[i].class_name, class_name_json->valuestring, sizeof(p_params->conditions[i].class_name) - 1);
                    p_params->conditions[i].type = type_json->valueint;
                    p_params->conditions[i].num = num_json->valueint;
                    p_params->conditions[i].mode = mode_json->valueint;
                }
            }
        }
    }

    cJSON *silent_period_json = cJSON_GetObjectItem(p_json, "silent_period");
    if (silent_period_json != NULL)
    {
        cJSON *repeat_json = cJSON_GetObjectItem(silent_period_json, "repeat");
        if (repeat_json && cJSON_IsArray(repeat_json))
        {
            for (int i = 0; i < 7 && i < cJSON_GetArraySize(repeat_json); i++)
            {
                cJSON *item = cJSON_GetArrayItem(repeat_json, i);
                if (item && cJSON_IsNumber(item))
                {
                    p_params->silent_period.repeat[i] = item->valueint;
                }
            }
        }
        cJSON *time_period_json = cJSON_GetObjectItem(silent_period_json, "time_period");
        if (time_period_json && cJSON_IsArray(time_period_json))
        {
            for (int i = 0; i < 2 && i < cJSON_GetArraySize(time_period_json); i++)
            {
                cJSON *item = cJSON_GetArrayItem(time_period_json, i);
                if (item && cJSON_IsString(item))
                {
                    strncpy(p_params->silent_period.time_period[i], item->valuestring, sizeof(p_params->silent_period.time_period[i]) - 1);
                }
            }
        }
        cJSON *silence_duration_json = cJSON_GetObjectItem(silent_period_json, "silence_duration");
        if (silence_duration_json && cJSON_IsNumber(silence_duration_json))
        {
            p_params->silent_period.silence_duration = silence_duration_json->valueint;
        }
    }

    cJSON *shutter_json = cJSON_GetObjectItem(p_json, "shutter");
    if (shutter_json != NULL && cJSON_IsNumber(shutter_json))
    {
        p_params->shutter = shutter_json->valueint;
    }
    
    return 0;
}
/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)p_module;
    return 0;
}
static int __stop(void *p_module)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)p_module;

    __data_lock(p_module_ins);
    p_module_ins->p_output_evt_id = NULL;
    p_module_ins->output_evt_num = 0;
    p_module_ins->params.conditions  = NULL;
    p_module_ins->params.condition_num = 0;
    __data_unlock(p_module_ins);

    tf_free(p_module_ins->p_output_evt_id);
    tf_free(p_module_ins->params.conditions);

    esp_err_t ret = tf_event_handler_unregister(p_module_ins->input_evt_id, __event_handler);

    return ret;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)p_module;
    int ret = __params_parse(&p_module_ins->params, p_json);
    return ret;
}
static int __msgs_sub_set(void *p_module, int evt_id)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)p_module;
    esp_err_t ret;
    p_module_ins->input_evt_id = evt_id;
    ret = tf_event_handler_register(evt_id, __event_handler, p_module_ins);
    return ret;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)p_module;
    if (output_index == 0 && num > 0)
    {
        p_module_ins->p_output_evt_id = (int *)tf_malloc(sizeof(int) * num); // todo check
        memcpy(p_module_ins->p_output_evt_id, p_evt_id, sizeof(int) * num);
        p_module_ins->output_evt_num = num;
    }
    else
    {
        ESP_LOGW(TAG, "only support output port 0, ignore %d", output_index);
    }
    return 0;
}

static tf_module_t *__module_instance(void)
{
    if (g_handle)
    {
        return g_handle;
    }

    tf_module_ai_camera_t *p_module_ins =  \
                     (tf_module_ai_camera_t *)tf_malloc(sizeof(tf_module_ai_camera_t));
    if (p_module_ins == NULL)
    {
        return NULL;
    }
    memset(p_module_ins, 0, sizeof(tf_module_ai_camera_t));
    return tf_module_ai_camera_init(p_module_ins);
}

static void __module_destroy(tf_module_t *handle)
{
    //Don't free handle
}

const static struct tf_module_ops __g_module_ops = {
    .start = __start,
    .stop = __stop,
    .cfg = __cfg,
    .msgs_sub_set = __msgs_sub_set,
    .msgs_pub_set = __msgs_pub_set};

const static struct tf_module_mgmt __g_module_mgmt = {
    .tf_module_instance = __module_instance,
    .tf_module_destroy = __module_destroy,
};

/*************************************************************************
 * API
 ************************************************************************/

tf_module_t *tf_module_ai_camera_init(tf_module_ai_camera_t *p_module_ins)
{
    if (NULL == p_module_ins)
    {
        return NULL;
    }
    p_module_ins->module_serv.p_module = p_module_ins;
    p_module_ins->module_serv.ops = &__g_module_ops;

    //TODO
    // p_module_ins->sscma_client_handle = bsp_sscma_client_init();
    if (p_module_ins->sscma_client_handle == NULL)
    {
        return NULL;
    }

    const sscma_client_callback_t callback = {
        .on_event = sscma_on_event,
        .on_log = NULL,
    };

    if (sscma_client_register_callback(p_module_ins->sscma_client_handle, \
                                       &callback, p_module_ins) != ESP_OK)
    {
        return NULL;
    }

    sscma_client_init(p_module_ins->sscma_client_handle);

    sscma_client_break(p_module_ins->sscma_client_handle);

    // TODO event register

    return &p_module_ins->module_serv;
}

esp_err_t tf_module_ai_camera_register(void)
{
    g_handle = __module_instance(); // Must be instantiated
    if (g_handle == NULL)
    {
        return ESP_FAIL;
    }
    return tf_module_register(TF_MODULE_AI_CAMERA_NAME,
                              TF_MODULE_AI_CAMERA_DESC,
                              TF_MODULE_AI_CAMERA_VERSION,
                              &__g_module_mgmt);
}