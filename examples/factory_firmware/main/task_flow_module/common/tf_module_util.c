#include "tf_module_util.h"
#include "tf_module_data_type.h"
#include "tf_util.h"

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

void tf_module_data_type_err_handle(void *event_data)
{
    uint8_t type = ((uint8_t *)event_data)[0];

    switch (type)
    {
    case TF_DATA_TYPE_BUFFER:
        tf_buffer_t * p_str = (tf_buffer_t *)event_data;
        if( p_str->p_data != NULL ) {
            tf_free(p_str->p_data);
            p_str->p_data = NULL;
        }
        break;
    
    default:
        break;
    }
}

