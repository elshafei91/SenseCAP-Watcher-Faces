#include "tf_module_util.h"
#include "tf_module_data_type.h"
#include "tf_util.h"
#include "tf_module_ai_camera.h"
const char * tf_module_data_type_to_str(uint8_t type)
{
    switch (type)
    {
    case TF_DATA_TYPE_BUFFER: return "TF_DATA_TYPE_BUFFER"; break;
    
    default:
        break;
    }
    return "UNKNOWN";
}

void tf_module_data_free(void *event_data)
{
    uint8_t type = ((uint8_t *)event_data)[0];

    switch (type)
    {
    case TF_DATA_TYPE_BUFFER:{
        tf_buffer_t * p_str = (tf_buffer_t *)event_data;
        if( p_str->p_data != NULL ) {
            tf_free(p_str->p_data);
            p_str->p_data = NULL;
        }
        break;
    }
    case TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE:{
        tf_data_dualimage_with_inference_t * p_data = (tf_data_dualimage_with_inference_t *)event_data;
        if( p_data->img_small.p_buf != NULL ) {
            tf_free( p_data->img_small.p_buf );
            p_data->img_small.p_buf = NULL;
        }
        if( p_data->img_large.p_buf != NULL ) {
            tf_free( p_data->img_large.p_buf );
            p_data->img_large.p_buf = NULL;
        }
        if( p_data->inference.p_data != NULL ) {
            tf_free( p_data->inference.p_data);
            p_data->inference.p_data = NULL;
        }
        break;
    }
    default:
        break;
    }

}

void tf_module_data_type_err_handle(void *event_data)
{
    tf_module_data_free(event_data);
}

