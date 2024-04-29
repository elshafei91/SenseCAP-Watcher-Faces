#include "tf_module_debug.h"
#include <string.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"

#define MODULE_DECLARE(p_module_instance, p_module) tf_module_debug_t *p_module_instance = (tf_module_debug_t *)p_module

static const char *TAG = "tfm.debug";


static void __event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *p_event_data)
{
    MODULE_DECLARE(p_module_instance, handler_args);
    
    tf_buffer_t * p_buf = (tf_buffer_t *)p_event_data;
    if( p_buf->type == TF_DATA_TYPE_BUFFER ) {
        if( p_buf->p_data != NULL ) {
            printf("%s", p_buf->p_data); //use
            free(p_buf->p_data); // todo need fresh before free?
            p_buf->p_data = NULL;
        } else {
            ESP_LOGW(TAG, "string data is NULL");
        }
    } else {
        ESP_LOGW(TAG, "must be %s", tf_module_data_type_to_str(p_buf->type));
        tf_module_data_type_err_handle(p_event_data);
    }
}

/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    MODULE_DECLARE(p_module_instance, p_module);
    return 0;
}
static int __stop(void *p_module)
{
    MODULE_DECLARE(p_module_instance, p_module);
    esp_err_t ret = tf_event_handler_unregister(p_module_instance->evt_id, __event_handler);
    return 0;
}
static int __cfg(void *p_module, cJSON *p_json)
{
    MODULE_DECLARE(p_module_instance, p_module);

    return 0;
}
static int __msgs_sub_set(void *p_module, int evt_id)
{
    MODULE_DECLARE(p_module_instance, p_module);
    esp_err_t ret;
    p_module_instance->evt_id = evt_id;
    ret = tf_event_handler_register(evt_id, __event_handler, p_module_instance);
    return ret;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    MODULE_DECLARE(p_module_instance, p_module);
    if (num)
    {
        ESP_LOGW(TAG, "none output");
    } 
    return 0;
}
static int __delete(void *p_module)
{
    MODULE_DECLARE(p_module_instance, p_module);
    tf_free(p_module_instance);
    return 0;
}

struct tf_module_drv_funcs __g_funcs = {
    .pfn_start = __start,
    .pfn_stop = __stop,
    .pfn_cfg = __cfg,
    .pfn_msgs_sub_set = __msgs_sub_set,
    .pfn_delete = __delete,
};

/*************************************************************************
 * API
 ************************************************************************/

tf_handle_t tf_module_debug_init(tf_module_debug_t *p_module)
{
    if (NULL == p_module)
    {
        return NULL;
    }
    p_module->serv.p_module = p_module;
    p_module->serv.p_funcs = &__g_funcs;
    return &p_module->serv;
}

const char *tf_module_debug_name_get(void)
{
    return "debug";
}
const char *tf_module_debug_desc_get(void)
{
    return "debug module";
}
tf_handle_t tf_module_debug_instance(void)
{
    tf_module_debug_t *p_module = (tf_module_debug_t *) tf_malloc(sizeof(tf_module_debug_t));
    if (p_module == NULL)
    {
        return NULL;
    }
    memset(p_module, 0, sizeof(tf_module_debug_t));
    return tf_module_debug_init(p_module);
}

void tf_module_debug_instance_free(tf_handle_t handle)
{
    if (handle)
    {
        tf_module_debug_t *p_module = CONTAINER_OF(handle, tf_module_debug_t, serv);
        tf_free(p_module);
    }
}

tf_instance_handle_t tf_module_debug_instance_handle_get(void)
{
    return (tf_instance_handle_t)tf_module_debug_instance;
}
