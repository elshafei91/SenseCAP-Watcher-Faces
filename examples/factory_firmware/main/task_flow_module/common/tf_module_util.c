#include "tf_module_util.h"
#include "tf_module_data_type.h"
#include "tf_util.h"
#include "tf_module_img_analyzer.h"
const char * tf_data_type_to_str(uint8_t type)
{
    switch (type)
    {
    case TF_DATA_TYPE_BUFFER: return "BUFFER"; break;
    case TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE: return "DUALIMAGE_WITH_INFERENCE"; break;
    case TF_DATA_TYPE_DUALIMAGE_WITH_AUDIO_TEXT: return "DUALIMAGE_WITH_AUDIO_TEXT"; break;
    default:
        break;
    }
    return "UNKNOWN";
}

void tf_data_free(void *event_data)
{
    uint8_t type = ((uint8_t *)event_data)[0];

    switch (type)
    {
    case TF_DATA_TYPE_BUFFER:{
        tf_data_buffer_t * p_data = (tf_data_buffer_t *)event_data;
        tf_data_buf_free(&p_data->data);
        break;
    }
    case TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE:{
        tf_data_dualimage_with_inference_t * p_data = (tf_data_dualimage_with_inference_t *)event_data;
        tf_data_image_free(&p_data->img_small);
        tf_data_image_free(&p_data->img_large);
        tf_data_inference_free(&p_data->inference);
        break;
    }
    case TF_DATA_TYPE_DUALIMAGE_WITH_AUDIO_TEXT:{
        tf_data_dualimage_with_audio_text_t * p_data = (tf_data_dualimage_with_audio_text_t *)event_data;
        tf_data_image_free(&p_data->img_small);
        tf_data_image_free(&p_data->img_large);
        tf_data_buf_free(&p_data->audio);
        tf_data_buf_free(&p_data->text);
        break;
    }

    default:
        break;
    }

}

void tf_data_buf_copy(struct tf_data_buf *p_dst, struct tf_data_buf *p_src)
{
    p_dst->len  = p_src->len;
    if( p_src->p_buf != NULL &&  p_src->len > 0) {
        p_dst->p_buf = tf_malloc(p_src->len);
        memcpy(p_dst->p_buf, p_src->p_buf, p_src->len);
    } else {
        p_dst->p_buf = NULL;
        p_dst->len  = 0;
    }
}
void tf_data_buf_free(struct tf_data_buf *p_data)
{
    p_data->len  = 0;
    if( p_data->p_buf != NULL) {
        tf_free(p_data->p_buf);
    }
    p_data->p_buf = NULL;
}

void tf_data_image_copy(struct tf_data_image *p_dst, struct tf_data_image *p_src)
{
    p_dst->len  = p_src->len;
    p_dst->time = p_src->time;
    if( p_src->p_buf != NULL &&  p_src->len > 0) {
        p_dst->p_buf = tf_malloc(p_src->len);
        memcpy(p_dst->p_buf, p_src->p_buf, p_src->len);
    } else {
        p_dst->p_buf = NULL;
        p_dst->len  = 0;
    }
}

void tf_data_image_free(struct tf_data_image *p_data)
{
    p_data->len  = 0;
    p_data->time  = 0;
    if( p_data->p_buf != NULL) {
        tf_free(p_data->p_buf);
    }
    p_data->p_buf = NULL;
}

void tf_data_inference_copy(struct tf_module_ai_camera_inference_info *p_dst, struct tf_module_ai_camera_inference_info *p_src)
{
    p_dst->is_valid = p_src->is_valid;
    p_dst->type     = p_src->type;
    p_dst->cnt      = p_src->cnt;
    if( p_src->p_data != NULL && p_src->cnt > 0) {
        p_dst->p_data = tf_malloc(sizeof(void *) * p_src->cnt);
        memcpy(p_dst->p_data, p_src->p_data, sizeof(void *) * p_src->cnt);
    } else {
        p_dst->p_data = NULL;
        p_dst->cnt    = 0;
    }
    memcpy(p_dst->classes, p_src->classes, sizeof(p_dst->classes));
}

void tf_data_inference_free(struct tf_module_ai_camera_inference_info *p_inference)
{
    if( p_inference->p_data != NULL) {
        tf_free(p_inference->p_data);
    }
    p_inference->p_data   = NULL;
    p_inference->cnt      = 0;
    p_inference->is_valid = false;
    p_inference->type     = AI_CAMERA_INFERENCE_TYPE_UNKNOWN;
}

