#include "tf_module_ai_camera.h"
#include <string.h>
#include <time.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sscma_client_io.h"
#include "sscma_client_ops.h"
#include "tf_module_util.h"
#include "data_defs.h"
#include "event_loops.h"
#include "view_image_preview.h"
#include "app_ota.h"

static const char *TAG = "tfm.ai_camera";

tf_module_t *g_handle = NULL;

#define EVENT_STATRT          BIT0
#define EVENT_STOP            BIT1
#define EVENT_SIMPLE_640_480  BIT2     
#define EVENT_PRVIEW_416_416  BIT3 

static int __time_to_seconds(struct tf_module_ai_camera_time *time) {
    if (!time) return -1;
    return (int)time->hour * 3600 + (int)time->minute * 60 + (int)time->second;
}

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
    tf_data_free(p_event_data);
    
    if (p_module_ins->params.shutter != TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_BY_INPUT) {
        return;
    }
    ESP_LOGI(TAG, "Input shutter");
    __data_lock(p_module_ins);
    p_module_ins->shutter_trigger_flag = 0x01 << TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_BY_INPUT;
    __data_unlock(p_module_ins); 
}

static int __class_num_get(struct tf_module_ai_camera_inference_info *p_inference, const char *p_class_name, uint8_t *p_target_id,uint8_t score)
{
    bool find_flag = false;
    uint8_t target = 0;
    *p_target_id = 0;

    if (p_inference->classes[0] != NULL)
    {
        for (int i = 0; p_inference->classes[i] != NULL; i++)
        {
            if(strcmp(p_inference->classes[i], p_class_name) == 0) {
                find_flag = true;
                target = i;
                break;
            }
        }
        if( !find_flag ) {
            return 0;
        }
    } else {
        return 0;
    }

    *p_target_id=target;

    int cnt = 0;

    switch (p_inference->type)
    {
    case AI_CAMERA_INFERENCE_TYPE_BOX:{
        sscma_client_box_t   *p_box = ( sscma_client_box_t *)p_inference->p_data;
        for (int i = 0; i < p_inference->cnt; i++)
        {
            if(p_box[i].target == target && p_box[i].score >= score) {
                cnt++;
            }
        }
        break;
    }
    case AI_CAMERA_INFERENCE_TYPE_CLASS:{
        sscma_client_class_t *p_class = ( sscma_client_class_t *)p_inference->p_data;
        for (int i = 0; i < p_inference->cnt; i++)
        {
            if(p_class[i].target == target && p_class[i].score > score) {
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
static bool __condition_check(tf_module_ai_camera_t                     *p_module_ins,  
                              struct tf_module_ai_camera_inference_info *p_inference)
{
    struct tf_module_ai_camera_params *p_params = &p_module_ins->params;

    if( p_params->mode ==  TF_MODULE_AI_CAMERA_MODES_SAMPLE ) {
        return true;
    }

    // none conditions
    if( p_params->conditions== NULL || p_params->condition_num == 0) {
        return true;
    }

    bool trigge_flag = true;

    if (p_params->conditions_combo == TF_MODULE_AI_CAMERA_CONDITIONS_COMBO_AND ) {
        trigge_flag = true;
    } else {
        trigge_flag = false;
    }

    for(int i = 0; i < p_params->condition_num; i++) {
        
        struct tf_module_ai_camera_condition condition = p_params->conditions[i];
        int cnt = 0;
        uint8_t target_id = 0;
        bool is_match = false;
        cnt = __class_num_get(p_inference, condition.class_name, &target_id, CONFIG_TF_MODULE_AI_CAMERA_CLASS_OBJECT_SCORE_THRESHOLD); // is score need to be judged？
        
        if( cnt > 0 ) {
            ESP_LOGD(TAG, " %s(%d) has %d", condition.class_name, target_id, cnt);
        }
        switch (condition.mode)
        {
            case TF_MODULE_AI_CAMERA_CONDITION_MODE_PRESENCE_DETECTION:
            {
                
                if(  target_id < CONFIG_TF_MODULE_AI_CAMERA_MODEL_CLASSES_MAX_NUM ) {
                    int last_cnt = p_module_ins->classes_num_cache[target_id];
                    // 0-N, N-0(N>=1): will be triggered
                    if( (!!cnt) ^ (!!last_cnt) ) {
                        is_match =  true;  
                    }
                    p_module_ins->classes_num_cache[target_id] = cnt;
                }
                break;
            }
            case TF_MODULE_AI_CAMERA_CONDITION_MODE_VALUE_COMPARE:
            {
                switch (condition.type)
                {
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_LESS: {
                    if( cnt < condition.num ) {
                        is_match =  true;
                    }
                    break;
                }
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_EQUAL: {
                    if( cnt == condition.num ) {
                        is_match =  true;
                    }
                    break;
                }
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_GREATER: {
                    if( cnt > condition.num ) {
                        is_match =  true;
                    }
                    break;
                }
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_NOT_EQUAL: {
                    if( cnt != condition.num ) {
                        is_match =  true;
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
                if(  target_id < CONFIG_TF_MODULE_AI_CAMERA_MODEL_CLASSES_MAX_NUM) {
                    int last_cnt = p_module_ins->classes_num_cache[target_id];
                    // when num change, will be triggered
                    if( cnt != last_cnt ) {
                        is_match =  true;  
                    }
                    p_module_ins->classes_num_cache[target_id] = cnt;
                }
                break;
            }
            default:
                break;
        }

        if (p_params->conditions_combo == TF_MODULE_AI_CAMERA_CONDITIONS_COMBO_AND ) {
            trigge_flag = trigge_flag && is_match;
        } else {
            trigge_flag = trigge_flag || is_match;
        }   
    }
    return trigge_flag;
}

static bool __condition_check_whith_filter(tf_module_ai_camera_t *p_module_ins, struct tf_module_ai_camera_inference_info *p_inference)
{
    struct tf_module_ai_camera_params *p_params = &p_module_ins->params;
    int trigge_cnt = 0;
    bool ret =  __condition_check(p_module_ins, p_inference);

    p_module_ins->condition_trigger_buf[p_module_ins->condition_trigger_buf_idx] = ret;
    p_module_ins->condition_trigger_buf_idx++;

    if( p_module_ins->condition_trigger_buf_idx >= CONFIG_TF_MODULE_AI_CAMERA_CONDITION_TRIGGER_BUF_SIZE ) {
        p_module_ins->condition_trigger_buf_idx = 0;
    }

    esp_log_write(ESP_LOG_VERBOSE, TAG,"Trigger buf[");
    for(int i = 0; i < CONFIG_TF_MODULE_AI_CAMERA_CONDITION_TRIGGER_BUF_SIZE; i++) {
        esp_log_write(ESP_LOG_VERBOSE, TAG,"%d, ", p_module_ins->condition_trigger_buf[i]);
        if( p_module_ins->condition_trigger_buf[i]) {
            trigge_cnt++;
        }
    }
    esp_log_write(ESP_LOG_VERBOSE, TAG,"]\n");

    if( trigge_cnt >= CONFIG_TF_MODULE_AI_CAMERA_CONDITION_TRIGGER_THRESHOLD) {
        ESP_LOGI(TAG, "Condition pass, trigge_cnt: %d", trigge_cnt);
        return true;
    } else {
        return false;
    }
}

static bool __silent_period_check(tf_module_ai_camera_t *p_module_ins)
{
    struct tf_module_ai_camera_params *p_params = &p_module_ins->params;
    
    time_t now = 0;
    struct tm timeinfo = { 0 };
    double diff = 0.0;
    time(&now);

    if( p_params->silent_period.time_is_valid ) {
        
        //TODO check time valid
        if( !p_params->silent_period.repeat[timeinfo.tm_wday] ) {
            return false;
        }

        struct tf_module_ai_camera_time now_time;
        now_time.hour = timeinfo.tm_hour;
        now_time.minute = timeinfo.tm_min;
        now_time.second = timeinfo.tm_sec;

        int start_s = __time_to_seconds(&p_params->silent_period.start);
        int end_s = __time_to_seconds(&p_params->silent_period.end);
        int now_s = __time_to_seconds(&now_time);;

        if( end_s < start_s ) {
            end_s = start_s + 24*60*60;
        }

        if( now_s < start_s || now_s > end_s ) {
            return false;
        }
    }

    diff = difftime(now, p_module_ins->last_output_time);
    if( p_params->silent_period.silence_duration > 0 ) {
        if( diff < p_params->silent_period.silence_duration ) {
            return false;
        }
    }

    ESP_LOGI(TAG, "Silence period pass, diff time:%d", diff);
    return true;
}

static bool __output_check(tf_module_ai_camera_t *p_module_ins, struct tf_module_ai_camera_inference_info *p_inference)
{
    struct tf_module_ai_camera_params *p_params = &p_module_ins->params;

    bool condition_check = true;

    // condition check
    if( (p_params->mode  == TF_MODULE_AI_CAMERA_MODES_INFERENCE)  && p_inference ) {
        if( !__condition_check_whith_filter(p_module_ins, p_inference)) {
            return false;
        }
    }
    
    // silent period check
    if( !__silent_period_check(p_module_ins)) {
        return false;
    }
    
    // shutter check
    if ( p_params->shutter != TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_CONSTANTLY ) {
        int flag = 0x01 << (p_params->shutter);
        if( !(p_module_ins->shutter_trigger_flag  &&  flag) ) {
            return false;
        } else {
            p_module_ins->shutter_trigger_flag = 0; //reset
        }
        ESP_LOGI(TAG, "Shutter trigger");
    }

    return true;
}


static int __get_camera_sensor_resolution(cJSON *payload)
{
    int width = 0, height = 0;
    cJSON *data = cJSON_GetObjectItem(payload, "data");
    if (data != NULL && cJSON_IsObject(data)) {
        cJSON *resolution = cJSON_GetObjectItem(data, "resolution");
        if (data != NULL && cJSON_IsArray(resolution) && cJSON_GetArraySize(resolution) == 2) {
            width = cJSON_GetArrayItem(resolution, 0)->valueint;
            height = cJSON_GetArrayItem(resolution, 1)->valueint;
        }
    }
    switch ((width+height)) {
        case (240+240):
            return TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_240_240;
        case (416+416):
            return TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_416_416;
        case (480+480):
            return TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_480_480;
        case (640+480):
            return TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_640_480;
        default:
            ESP_LOGE(TAG, "unknown resolution: %d, %d", (width+height));
            return -1;
    }
}

static int __get_camera_mode_get(cJSON *payload)
{
    int mode = 0;
    cJSON * name = cJSON_GetObjectItem(payload, "name");
    if( name != NULL  && name->valuestring != NULL ) {
       if(strcmp(name->valuestring, "SAMPLE") == 0) {
            mode = TF_MODULE_AI_CAMERA_MODES_SAMPLE;  
       } else {
            mode = TF_MODULE_AI_CAMERA_MODES_INFERENCE;
       }
    } else {
        mode = -1;
    }
    return mode;
}

static void sscma_on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)user_ctx;

    int resolution = __get_camera_sensor_resolution(reply->payload);
    int mode = __get_camera_mode_get(reply->payload);

    switch (resolution)
    {
        case TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_416_416: {

            struct tf_module_ai_camera_preview_info info;
            sscma_client_class_t *classes = NULL;
            sscma_client_box_t     *boxes = NULL;
            sscma_client_point_t  *points = NULL;
            int class_count = 0;
            int box_count = 0;
            int point_count = 0;

            char *img = NULL;
            int img_size = 0;
            bool is_need_output = false;

            info.img.p_buf = NULL;
            info.img.len = 0;

            info.inference.cnt = 0;
            info.inference.is_valid = false;
            info.inference.p_data = NULL;
            memcpy(info.inference.classes, p_module_ins->classes, sizeof(info.inference.classes));
            
            // printf("sscma:%s\r\n",reply->data);

            if ( sscma_utils_fetch_image_from_reply(reply, &img, &img_size) == ESP_OK ) {
                info.img.p_buf = (uint8_t *)img;
                info.img.len = img_size;
                info.img.time = time(NULL);
                ESP_LOGD(TAG, "Small img:%.3fk (%d), time: %d", (float)img_size/1024, img_size, time(NULL));
            }

            if( mode == TF_MODULE_AI_CAMERA_MODES_INFERENCE ) {
                info.inference.is_valid = true;

                if (sscma_utils_fetch_boxes_from_reply(reply, &boxes, &box_count) == ESP_OK) {
                    info.inference.type = AI_CAMERA_INFERENCE_TYPE_BOX;
                    info.inference.p_data = (void *)boxes;
                    info.inference.cnt = box_count;
                    if (box_count > 0) {
                        for (int i = 0; i < box_count; i++) {
                            ESP_LOGD(TAG, "[box %d]: x=%d, y=%d, w=%d, h=%d, score=%d, target=%d", i,  \
                                    boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, boxes[i].score, boxes[i].target);
                        }
                    }

                } else if (sscma_utils_fetch_classes_from_reply(reply, &classes, &class_count) == ESP_OK) {
                    info.inference.type = AI_CAMERA_INFERENCE_TYPE_CLASS;
                    info.inference.p_data = (void *)classes;
                    info.inference.cnt = class_count;
                    if (class_count > 0) {
                        for (int i = 0; i < class_count; i++) {
                            ESP_LOGD(TAG, "[class %d]: target=%d, score=%d", i, \
                                    classes[i].target, classes[i].score);
                        }
                    }
                } else if (sscma_utils_fetch_points_from_reply(reply, &points, &point_count) == ESP_OK ) {
                    info.inference.type = AI_CAMERA_INFERENCE_TYPE_POINT;
                    info.inference.p_data = (void *)points;
                    info.inference.cnt = point_count;
                    if (point_count > 0) {
                        for (int i = 0; i < point_count; i++) {
                            ESP_LOGD(TAG, "[point %d]: x=%d, y=%d, z=%d, score=%d, target=%d", i, \
                                    points[i].x, points[i].y, points[i].z, points[i].score, points[i].target);
                        }
                    }
                }
            }
            
            __data_lock(p_module_ins);
            is_need_output = __output_check(p_module_ins, &info.inference);
            if( is_need_output) {

                if( p_module_ins->params.output_type == TF_MODULE_AI_CAMERA_OUTPUT_TYPE_SMALL_IMG_AND_LARGE_IMG ) {
                    
                    xEventGroupSetBits(p_module_ins->event_group, EVENT_SIMPLE_640_480);

                    //save to preview_info_cache
                    tf_data_image_copy(&p_module_ins->preview_info_cache.img, &info.img);
                    tf_data_inference_copy(&p_module_ins->preview_info_cache.inference, &info.inference);

                } else {
                    p_module_ins->last_output_time = time(NULL);
                    p_module_ins->output_data.type = TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE;
                    p_module_ins->output_data.img_large.p_buf = NULL;
                    p_module_ins->output_data.img_large.len = 0;
                    p_module_ins->output_data.img_large.time = 0;
                    for (int i = 0; i < p_module_ins->output_evt_num; i++)
                    {
                        tf_data_image_copy(&p_module_ins->output_data.img_small, &info.img);
                        tf_data_inference_copy(&p_module_ins->output_data.inference, &info.inference);
                        tf_event_post(p_module_ins->p_output_evt_id[i], &p_module_ins->output_data, sizeof(p_module_ins->output_data), portMAX_DELAY);
                        ESP_LOGI(TAG, "Output --> %d", p_module_ins->p_output_evt_id[i]);
                    }

                }
            }
            __data_unlock(p_module_ins);
            
            //UI preview
            // esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  
            //                         VIEW_EVENT_AI_CAMERA_PREVIEW, &info, sizeof(info), portMAX_DELAY);
            
            // Reduce event bus usage
            lvgl_port_lock(0);
            view_image_preview_flush(&info);
            lvgl_port_unlock();

            tf_data_image_free(&info.img);
            tf_data_inference_free(&info.inference);

            break;
        }
        case TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_640_480:{
            char *img = NULL;
            int img_size = 0;
            struct tf_data_image img_large;
            // printf("sscma:%s\r\n",reply->data);
            if ( sscma_utils_fetch_image_from_reply(reply, &img, &img_size) == ESP_OK ) {
                img_large.p_buf = (uint8_t *)img;
                img_large.len = img_size;
                img_large.time = time(NULL);
                ESP_LOGI(TAG, "Large img:%.3fk(%d), time: %d", (float)img_size/1024, img_size, time(NULL));
            } else {
                img_large.p_buf = NULL;
                img_large.len = 0;
                img_large.time = 0;
            }

            __data_lock(p_module_ins);
            p_module_ins->last_output_time = time(NULL);
            p_module_ins->output_data.type = TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE;
            for (int i = 0; i < p_module_ins->output_evt_num; i++)
            {
                tf_data_image_copy(&p_module_ins->output_data.img_large, &img_large);
                tf_data_image_copy(&p_module_ins->output_data.img_small, &p_module_ins->preview_info_cache.img);
                tf_data_inference_copy(&p_module_ins->output_data.inference, &p_module_ins->preview_info_cache.inference);
                tf_event_post(p_module_ins->p_output_evt_id[i], &p_module_ins->output_data, sizeof(p_module_ins->output_data), portMAX_DELAY);
                ESP_LOGI(TAG, "Output --> %d", p_module_ins->p_output_evt_id[i]);
            }
            __data_unlock(p_module_ins);

            tf_data_image_free(&img_large);
            tf_data_image_free(&p_module_ins->preview_info_cache.img);
            tf_data_inference_free(&p_module_ins->preview_info_cache.inference);

            xEventGroupSetBits(p_module_ins->event_group, EVENT_PRVIEW_416_416);
            break;
        }
        default:
            xEventGroupSetBits(p_module_ins->event_group, EVENT_PRVIEW_416_416);
            ESP_LOGE(TAG, "Ignored this resolution: %d", resolution);
            break;
    }
}

static void __parmas_printf(struct tf_module_ai_camera_params *p_params)
{
    ESP_LOGD(TAG, "Output: %d", p_params->output_type);
    ESP_LOGD(TAG, "Mode: %d", p_params->mode);
    if(p_params->model.model_type == TF_MODULE_AI_CAMERA_MODEL_TYPE_CLOUD) {
        ESP_LOGD(TAG, "model cloud:");
        ESP_LOGD(TAG, "    id: %s", p_params->model.model_id);
        ESP_LOGD(TAG, "    url: %s", p_params->model.url);
        ESP_LOGD(TAG, "    version: %s", p_params->model.version);
        ESP_LOGD(TAG, "    size: %d", p_params->model.size);
        ESP_LOGD(TAG, "    checksum: %s", p_params->model.checksum);

    } else {
        ESP_LOGD(TAG, "Model local: %d", p_params->model.model_type);
    }

    ESP_LOGD(TAG, "Conditions combo: %d", p_params->conditions_combo);
    for (size_t i = 0; i < p_params->condition_num; i++)
    {
        ESP_LOGD(TAG, "  condition %d", i);
        if( p_params->conditions[i].mode == TF_MODULE_AI_CAMERA_CONDITION_MODE_PRESENCE_DETECTION) {
            ESP_LOGD(TAG, "    %s: 0~N, N~0", p_params->conditions[i].class_name);
        } else if( p_params->conditions[i].mode == TF_MODULE_AI_CAMERA_CONDITION_MODE_NUM_CHANGE ) {
            ESP_LOGD(TAG, "    %s num change", p_params->conditions[i].class_name);
        } else {
            char *type_str = "unkown";
            switch( p_params->conditions[i].type) {
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_EQUAL:
                    type_str = "==";
                    break;
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_GREATER:
                    type_str = ">";
                    break;
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_LESS:
                   type_str = "<";
                   break;
                case TF_MODULE_AI_CAMERA_CONDITION_TYPE_NOT_EQUAL:
                    type_str = "!=";
                    break;
                default:
                    break;
            }
            ESP_LOGD(TAG, "    %s %s %d", p_params->conditions[i].class_name, type_str, p_params->conditions[i].num);
        }
        
    }

    ESP_LOGD(TAG, "Silence duration: %d", p_params->silent_period.silence_duration);
    if( p_params->silent_period.time_is_valid ) {
        
        ESP_LOGD(TAG, "Wday:[%1d,%1d,%1d,%1d,%1d,%1d,%1d]", p_params->silent_period.repeat[0], p_params->silent_period.repeat[1], p_params->silent_period.repeat[2], \
                                                       p_params->silent_period.repeat[3],p_params->silent_period.repeat[4], p_params->silent_period.repeat[5], p_params->silent_period.repeat[6]);
        ESP_LOGD(TAG, "Time:[%02d:%02d:%02d~%02d:%02d:%02d]", \
            p_params->silent_period.start.hour,p_params->silent_period.start.minute,p_params->silent_period.start.second, \
            p_params->silent_period.end.hour,p_params->silent_period.end.minute,p_params->silent_period.end.second);
    }
}

static void __parmas_default(struct tf_module_ai_camera_params *p_params)
{
    p_params->mode = TF_MODULE_AI_CAMERA_MODES_INFERENCE;
    p_params->model.model_type = TF_MODULE_AI_CAMERA_MODEL_TYPE_LOCAL_PERSON;
    p_params->model.size = 0;
    p_params->model.model_id[0] = '\0';
    p_params->model.url[0] = '\0';
    p_params->model.version[0] = '\0';
    p_params->model.checksum[0] = '\0';
    p_params->model.p_info_all = NULL;
    p_params->shutter = TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_CONSTANTLY;
    p_params->condition_num = 0;
    p_params->conditions = NULL;
    p_params->conditions_combo = TF_MODULE_AI_CAMERA_CONDITIONS_COMBO_AND;
    p_params->silent_period.time_is_valid = false;
    p_params->silent_period.silence_duration =  CONFIG_TF_MODULE_AI_CAMERA_SILENCE_DURATION_DEFAULT;
    p_params->output_type = TF_MODULE_AI_CAMERA_OUTPUT_TYPE_SMALL_IMG_AND_LARGE_IMG;

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
        p_params->mode = TF_MODULE_AI_CAMERA_MODES_SAMPLE;
    }

    cJSON *model_type_json = cJSON_GetObjectItem(p_json, "model_type");
    if (model_type_json != NULL && cJSON_IsNumber(model_type_json))
    {
        p_params->model.model_type = model_type_json->valueint;
    }
    else
    {
        p_params->model.model_type = TF_MODULE_AI_CAMERA_MODEL_TYPE_CLOUD;
    }

    // only inference mode and cloud model need parse model info
    cJSON *model_json = cJSON_GetObjectItem(p_json, "model");
    if ( (p_params->mode == TF_MODULE_AI_CAMERA_MODES_INFERENCE) &&  \
         (p_params->model.model_type == TF_MODULE_AI_CAMERA_MODEL_TYPE_CLOUD) && model_json != NULL)
    {
        cJSON *model_id_json = cJSON_GetObjectItem(model_json, "model_id");
        cJSON *version_json = cJSON_GetObjectItem(model_json, "version");
        cJSON *checksum_json = cJSON_GetObjectItem(model_json, "checksum"); 
        if (model_id_json && cJSON_IsString(model_id_json) &&
            checksum_json && cJSON_IsString(checksum_json) &&
            version_json && cJSON_IsString(version_json) )
        {

            strncpy(p_params->model.model_id, model_id_json->valuestring, sizeof(p_params->model.model_id) - 1);
            strncpy(p_params->model.version, version_json->valuestring, sizeof(p_params->model.version) - 1);
            strncpy(p_params->model.checksum, checksum_json->valuestring, sizeof(p_params->model.checksum) - 1);
        }

        cJSON *arguments_json = cJSON_GetObjectItem(model_json, "arguments");
        if (arguments_json != NULL) {
            cJSON *url_json = cJSON_GetObjectItem(arguments_json, "url");
            cJSON *size_json = cJSON_GetObjectItem(arguments_json, "size");
            if (url_json && cJSON_IsString(url_json) &&
                size_json && cJSON_IsNumber(size_json) )
            {
                strncpy(p_params->model.url, url_json->valuestring, sizeof(p_params->model.url) - 1);
                p_params->model.size = size_json->valueint;
            }
        }

        p_params->model.p_info_all = cJSON_PrintUnformatted(model_json);
        
    } else {
        p_params->model.p_info_all = NULL;
    }

    cJSON *conditions_json = cJSON_GetObjectItem(p_json, "conditions");
    if (conditions_json != NULL && cJSON_IsArray(conditions_json))
    {
        p_params->condition_num = cJSON_GetArraySize(conditions_json);
        
        if( p_params->condition_num > 0 ) {
            p_params->conditions = (struct tf_module_ai_camera_condition *)tf_malloc(p_params->condition_num * sizeof(struct tf_module_ai_camera_condition));
        } else {
            p_params->conditions = NULL;
        }

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
    } else {
        p_params->conditions = NULL;
        p_params->condition_num = 0;
    }

    cJSON *conditions_combo_json = cJSON_GetObjectItem(p_json, "conditions_combo");
    if (conditions_combo_json != NULL && cJSON_IsNumber(conditions_combo_json))
    {
        p_params->conditions_combo = conditions_combo_json->valueint;
    } 

    cJSON *silent_period_json = cJSON_GetObjectItem(p_json, "silent_period");
    if (silent_period_json != NULL)
    {
        cJSON *time_period_json = cJSON_GetObjectItem(silent_period_json, "time_period");
        if ( time_period_json != NULL)
        {
            p_params->silent_period.time_is_valid = true;

            cJSON *repeat_json = cJSON_GetObjectItem(time_period_json, "repeat");
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

            cJSON *time_start_json = cJSON_GetObjectItem(time_period_json, "time_start");
            cJSON *time_end_json = cJSON_GetObjectItem(time_period_json, "time_end");
            if (time_start_json && cJSON_IsString(time_start_json) && time_end_json && cJSON_IsString(time_end_json)) {
                char *time_start_str = time_start_json->valuestring;
                char *time_end_str = time_end_json->valuestring;
                if (sscanf(time_start_str, "%" SCNu8 ":%" SCNu8 ":%" SCNu8,  \
                            &p_params->silent_period.start.hour, &p_params->silent_period.start.minute, &p_params->silent_period.start.second) != 3) {
                    p_params->silent_period.start.hour = 0;
                    p_params->silent_period.start.minute = 0;
                    p_params->silent_period.start.second = 0;
                }

                if (sscanf(time_end_str, "%" SCNu8 ":%" SCNu8 ":%" SCNu8, 
                            &p_params->silent_period.end.hour, &p_params->silent_period.end.minute, &p_params->silent_period.end.second) != 3) {
                    p_params->silent_period.end.hour = 0;
                    p_params->silent_period.end.minute = 0;
                    p_params->silent_period.end.second = 0;
                }
            }

        } else {
            p_params->silent_period.time_is_valid = false;
        }

        cJSON *silence_duration_json = cJSON_GetObjectItem(silent_period_json, "silence_duration");
        if (silence_duration_json && cJSON_IsNumber(silence_duration_json))
        {
            p_params->silent_period.silence_duration = silence_duration_json->valueint;
        } else {
            p_params->silent_period.silence_duration = CONFIG_TF_MODULE_AI_CAMERA_SILENCE_DURATION_DEFAULT;
        }

    } else {
        p_params->silent_period.time_is_valid = false;
        p_params->silent_period.silence_duration = CONFIG_TF_MODULE_AI_CAMERA_SILENCE_DURATION_DEFAULT;
    }

    cJSON *shutter_json = cJSON_GetObjectItem(p_json, "shutter");
    if (shutter_json != NULL && cJSON_IsNumber(shutter_json))
    {
        p_params->shutter = shutter_json->valueint;
    }

    cJSON *output_type_json = cJSON_GetObjectItem(p_json, "output_type");
    if (output_type_json != NULL && cJSON_IsNumber(output_type_json))
    {
        p_params->output_type = output_type_json->valueint;
    } else {
        p_params->output_type = TF_MODULE_AI_CAMERA_OUTPUT_TYPE_SMALL_IMG_AND_LARGE_IMG;
    }
    return 0;
}


static void ai_camera_task(void *p_arg)
{
    esp_err_t ret = ESP_OK;
    int err_flag = 0;
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)p_arg;
    struct tf_module_ai_camera_params *p_params = &p_module_ins->params;
    sscma_client_model_t *model_info;
    EventBits_t bits;
    bool run_flag = false;
    ESP_LOGI(TAG, "Task start");
    while(1) {
        
        err_flag = 0;
        bits = xEventGroupWaitBits(p_module_ins->event_group, \
                EVENT_STATRT | EVENT_STOP | \
                EVENT_SIMPLE_640_480 | EVENT_PRVIEW_416_416, pdTRUE, pdFALSE, portMAX_DELAY);

        if( ( bits & EVENT_SIMPLE_640_480 ) != 0  && run_flag ) {
            ESP_LOGI(TAG, "EVENT_SIMPLE_640_480");
            sscma_client_break(p_module_ins->sscma_client_handle);
            sscma_client_set_sensor(p_module_ins->sscma_client_handle, 1,  \
                                    TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_640_480, true);
            if (sscma_client_sample(p_module_ins->sscma_client_handle, 1) != ESP_OK)
            {
                ESP_LOGE(TAG, "Sample %d failed\n", TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_640_480);
                xEventGroupSetBits(p_module_ins->event_group, EVENT_PRVIEW_416_416);
            }
            //TODO  need to set a timeout?
        }
        if( ( bits & EVENT_PRVIEW_416_416 ) != 0 && run_flag ) {
            ESP_LOGI(TAG, "EVENT_PRVIEW_416_416");
            sscma_client_break(p_module_ins->sscma_client_handle);
            sscma_client_set_sensor(p_module_ins->sscma_client_handle, 1,  \
                                    TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_416_416, true);
            if( p_params->mode == TF_MODULE_AI_CAMERA_MODES_INFERENCE ) {
                if (sscma_client_invoke(p_module_ins->sscma_client_handle, -1, false, true) != ESP_OK) {
                    ESP_LOGE(TAG, "Invoke %d failed\n", TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_416_416);
                    err_flag |= TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_INVOKE; 
                }
            } else {
                if (sscma_client_sample(p_module_ins->sscma_client_handle, -1) != ESP_OK) {
                    ESP_LOGE(TAG, "Sample %d failed\n", TF_MODULE_AI_CAMERA_SENSOR_RESOLUTION_416_416);
                    err_flag |= TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_SIMPLE; 
                }
            }
        }

        if( ( bits & EVENT_STATRT ) != 0 ) {
            bool is_use_model = true;
            bool is_need_update = false;

            ESP_LOGI(TAG, "EVENT_STATRT");

            sscma_client_break(p_module_ins->sscma_client_handle);

            // reset catch information
            __data_lock(p_module_ins); 
            p_module_ins->shutter_trigger_flag = 0;
            p_module_ins->last_output_time = 0;
            p_module_ins->condition_trigger_buf_idx = 0;
            memset(p_module_ins->condition_trigger_buf, false, sizeof(p_module_ins->condition_trigger_buf));
            memset(p_module_ins->classes_num_cache, 0, sizeof(p_module_ins->classes_num_cache));
            memset(p_module_ins->classes, NULL, sizeof(p_module_ins->classes));
            __data_unlock(p_module_ins);

            if( p_params->mode == TF_MODULE_AI_CAMERA_MODES_INFERENCE ) {
                is_use_model = true;
            } else {
                is_use_model = false;
            }

            if( is_use_model ) {
                if( p_params->model.model_type == TF_MODULE_AI_CAMERA_MODEL_TYPE_CLOUD ) {
                    ESP_LOGI(TAG, "Use cloud model");
                    if(sscma_client_set_model(p_module_ins->sscma_client_handle, 4) != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to set model:%d\n", 4);
                        // err_flag |= TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_MODEL; //TODO 
                    }
                    
                    if(sscma_client_get_model(p_module_ins->sscma_client_handle, &model_info, false) != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to get model info\n");
                        is_need_update = true;
                    } else {
                        if( model_info->uuid && model_info->ver &&  \
                            strcmp(model_info->uuid, p_params->model.model_id) == 0  && 
                            strcmp(model_info->ver, p_params->model.version) == 0 ) {
                            is_need_update = false;
                        } else {
                            is_need_update = true;
                        }
                    }

                    if(is_need_update ) {
                        
                        if( model_info ) {
                            ESP_LOGI(TAG, "Model updating... %s,%s --> %s,%s", \
                                (model_info->uuid != NULL) ? model_info->uuid : "NULL", (model_info->ver != NULL) ? model_info->ver : "NULL", p_params->model.model_id, p_params->model.version);
                        } else {
                            ESP_LOGI(TAG, "Model updating... NULL --> %s,%s",  p_params->model.model_id, p_params->model.version);
                        }

                        ESP_LOGI(TAG, "Url: %s", p_params->model.url);
                        ESP_LOGI(TAG, "Size:%d", p_params->model.size);
                        ESP_LOGI(TAG, "MD5:%s", p_params->model.checksum);
                        ret = app_ota_ai_model_download(p_params->model.url, p_params->model.size);
                        if( ret != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to download model:%d\n", ret);
                            err_flag |= TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_MODEL_OTA;
                        } else {
                            //TODO Himax will restart.
                            ESP_LOGI(TAG, "Download model success");
                            if(sscma_client_set_model(p_module_ins->sscma_client_handle, 4) != ESP_OK) {
                                ESP_LOGE(TAG, "Failed to set model:%d\n", 4);
                                err_flag |= TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_MODEL; //TODO 
                            } else {
                                // set model info
                                if (sscma_client_set_model_info(p_module_ins->sscma_client_handle, (const char *)p_params->model.p_info_all) != ESP_OK)
                                {
                                    ESP_LOGE(TAG, "Failed to set model info\n");
                                    err_flag |= TF_MODULE_AI_CAMERA_CODE_ERR_SSCMA_MODEL;
                                }
                            }

                        }
                    } else {
                        ESP_LOGI(TAG, "Model does not need to be updated");
                    }

                } else {
                    ESP_LOGI(TAG, "Use local model: %d", p_params->model.model_type);
                    if(sscma_client_set_model(p_module_ins->sscma_client_handle, p_params->model.model_type) != ESP_OK) {
                        ESP_LOGI(TAG, "Failed to set model:%d\n", p_params->model.model_type);
                    }
                }

                if( sscma_client_get_model(p_module_ins->sscma_client_handle, &model_info, false) == ESP_OK) {
                    ESP_LOGI(TAG, "UUID: %s", model_info->uuid ? model_info->uuid : "N/A");
                    ESP_LOGI(TAG, "Name: %s", model_info->name ? model_info->name : "N/A");
                    ESP_LOGI(TAG, "Version: %s", model_info->ver ? model_info->ver : "N/A");
                    ESP_LOGI(TAG, "Classes:");
                    if (model_info->classes[0] != NULL)
                    {   
                        int i = 0;
                        for (i = 0; model_info->classes[i] != NULL; i++)
                        {
                            ESP_LOGI(TAG, "  - %s", model_info->classes[i]);
                        }

                        if( i >= CONFIG_TF_MODULE_AI_CAMERA_MODEL_CLASSES_MAX_NUM) {
                            i = CONFIG_TF_MODULE_AI_CAMERA_MODEL_CLASSES_MAX_NUM -1;
                            ESP_LOGE(TAG, "Classes num more than %d\n", CONFIG_TF_MODULE_AI_CAMERA_MODEL_CLASSES_MAX_NUM);
                        }

                        //update classes
                        memccpy(p_module_ins->classes, model_info->classes, 0, i * sizeof(char*));
                    } else {
                        ESP_LOGI(TAG, "  N/A");
                    }
                }
            } else {
                ESP_LOGI(TAG, "Do not use model");
            }

            // trigger only once
            if(p_params->shutter == TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_ONCE) {
                p_module_ins->shutter_trigger_flag =  0x01 << TF_MODULE_AI_CAMERA_SHUTTER_TRIGGER_ONCE;
            }

            if( err_flag == 0 ) {
                // start preview
                xEventGroupSetBits(p_module_ins->event_group, EVENT_PRVIEW_416_416);
                run_flag = true;
            }
        }

        if( ( bits & EVENT_STOP ) != 0 ) {
            sscma_client_break(p_module_ins->sscma_client_handle);
            ESP_LOGI(TAG, "EVENT_STOP");
            run_flag = false;
        }
        
        if( err_flag != 0 ) {
            tf_module_status_set(TF_MODULE_AI_CAMERA_NAME, err_flag);
        }
    }
}
/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)p_module;
    xEventGroupSetBits(p_module_ins->event_group, EVENT_STATRT);
    return 0;
}

static int __stop(void *p_module)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)p_module;
    
    xEventGroupSetBits(p_module_ins->event_group, EVENT_STOP);

    __data_lock(p_module_ins);
    if( p_module_ins->p_output_evt_id ) {
        tf_free(p_module_ins->p_output_evt_id); 
    }
    if( p_module_ins->params.conditions ) {
        tf_free(p_module_ins->params.conditions);
    }
    if( p_module_ins->params.model.p_info_all ) {
        free(p_module_ins->params.model.p_info_all);
    }

    p_module_ins->p_output_evt_id = NULL;
    p_module_ins->output_evt_num = 0;
    p_module_ins->params.conditions  = NULL;
    p_module_ins->params.condition_num = 0;
    __data_unlock(p_module_ins);

    esp_err_t ret = tf_event_handler_unregister(p_module_ins->input_evt_id, __event_handler);

    
    return ret;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)p_module;
    __data_lock(p_module_ins);
    __parmas_default(&p_module_ins->params);
    __params_parse(&p_module_ins->params, p_json);
    __parmas_printf(&p_module_ins->params);
    __data_unlock(p_module_ins);
    return 0;
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
    __data_lock(p_module_ins);
    if (output_index == 0 && num > 0)
    {
        p_module_ins->p_output_evt_id = (int *)tf_malloc(sizeof(int) * num);
        if (p_module_ins->p_output_evt_id )
        {
            memcpy(p_module_ins->p_output_evt_id, p_evt_id, sizeof(int) * num);
            p_module_ins->output_evt_num = num;
        } else {
            ESP_LOGE(TAG, "Failed to malloc p_output_evt_id");
            p_module_ins->output_evt_num = 0;
        }
    }
    else
    {
        ESP_LOGW(TAG, "Only support output port 0, ignore %d", output_index);
    }
    __data_unlock(p_module_ins);
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
        tf_free(p_module_ins);
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
    .msgs_pub_set = __msgs_pub_set
};

const static struct tf_module_mgmt __g_module_mgmt = {  
    .tf_module_instance = __module_instance,
    .tf_module_destroy = __module_destroy,
};

/*************************************************************************
 * API
 ************************************************************************/

tf_module_t *tf_module_ai_camera_init(tf_module_ai_camera_t *p_module_ins)
{
    esp_err_t ret = ESP_OK;
    if (NULL == p_module_ins)
    {
        return NULL;
    }
#if CONFIG_ENABLE_TF_MODULE_AI_CAMERA_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    p_module_ins->module_serv.p_module = p_module_ins;
    p_module_ins->module_serv.ops = &__g_module_ops;

    // params default
    __parmas_default(&p_module_ins->params);

    p_module_ins->p_output_evt_id = NULL;
    p_module_ins->output_evt_num = 0;
    p_module_ins->shutter_trigger_flag = 0;

    p_module_ins->condition_trigger_buf_idx = 0;
    memset(p_module_ins->condition_trigger_buf, false, sizeof(p_module_ins->condition_trigger_buf));

    p_module_ins->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_module_ins->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != p_module_ins->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed to create event_group");

    p_module_ins->p_task_stack_buf = (StackType_t *)tf_malloc(TF_MODULE_AI_CAMERA_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(p_module_ins->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    // task TCB must be allocated from internal memory 
    p_module_ins->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(p_module_ins->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_module_ins->task_handle = xTaskCreateStatic(ai_camera_task,
                                                "ai_camera_task",
                                                TF_MODULE_AI_CAMERA_TASK_STACK_SIZE,
                                                (void *)p_module_ins,
                                                TF_MODULE_AI_CAMERA_TASK_PRIO,
                                                p_module_ins->p_task_stack_buf,
                                                p_module_ins->p_task_buf);
    ESP_GOTO_ON_FALSE(p_module_ins->task_handle, ESP_FAIL, err, TAG, "Failed to create task");

    p_module_ins->sscma_client_handle = bsp_sscma_client_init();
    ESP_GOTO_ON_FALSE(p_module_ins->sscma_client_handle, ESP_FAIL, err, TAG, "Failed to bsp sscma");

    const sscma_client_callback_t callback = {
        .on_event = sscma_on_event,
        .on_log = NULL,
    };

    if (sscma_client_register_callback(p_module_ins->sscma_client_handle, \
                                       &callback, p_module_ins) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register sscma event callback");
        goto err;
    }

    sscma_client_init(p_module_ins->sscma_client_handle);
    sscma_client_break(p_module_ins->sscma_client_handle);

    if (sscma_client_get_info(p_module_ins->sscma_client_handle, &p_module_ins->himax_info, true) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get info");
    } else {
        ESP_LOGI(TAG,"ID: %s", (p_module_ins->himax_info->id != NULL) ? p_module_ins->himax_info->id : "NULL");
        ESP_LOGI(TAG,"Name: %s", (p_module_ins->himax_info->name != NULL) ? p_module_ins->himax_info->name : "NULL");
        ESP_LOGI(TAG,"Hardware Version: %s", (p_module_ins->himax_info->hw_ver != NULL) ? p_module_ins->himax_info->hw_ver : "NULL");
        ESP_LOGI(TAG,"Software Version: %s", (p_module_ins->himax_info->sw_ver != NULL) ? p_module_ins->himax_info->sw_ver : "NULL");
        ESP_LOGI(TAG,"Firmware Version: %s", (p_module_ins->himax_info->fw_ver != NULL) ? p_module_ins->himax_info->fw_ver : "NULL");
    }

    // TODO event register
    return &p_module_ins->module_serv;

err:
    if(p_module_ins->task_handle ) {
        vTaskDelete(p_module_ins->task_handle);
        p_module_ins->task_handle = NULL;
    }

    if( p_module_ins->p_task_stack_buf ) {
        tf_free(p_module_ins->p_task_stack_buf);
        p_module_ins->p_task_stack_buf = NULL;
    }

    if( p_module_ins->p_task_buf ) {
        free(p_module_ins->p_task_buf);
        p_module_ins->p_task_buf = NULL;
    }
    if (p_module_ins->sem_handle) {
        vSemaphoreDelete(p_module_ins->sem_handle);
        p_module_ins->sem_handle = NULL;
    }

    if (p_module_ins->event_group) {
        vEventGroupDelete(p_module_ins->event_group);
        p_module_ins->event_group = NULL;
    }

    return NULL;
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

char *tf_module_ai_camera_himax_version_get(void)
{
    if (g_handle == NULL)
    {
        return NULL;
    }
    tf_module_ai_camera_t *p_module_ins = (tf_module_ai_camera_t *)g_handle->p_module;
    
    // It is only modified during initialization and no protection is required.
    if( p_module_ins->himax_info &&  p_module_ins->himax_info->sw_ver) {
        return p_module_ins->himax_info->sw_ver;
    }
    return NULL;
}