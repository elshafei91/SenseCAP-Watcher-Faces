#include "tf_module_timer.h"
#include <string.h>
#include <time.h>
#include "tf.h"
#include "tf_util.h"
#include "esp_log.h"


#define MODULE_DECLARE(p_module_instance, p_module) tf_module_timer_t *p_module_instance = (tf_module_timer_t *)p_module

static const char *TAG = "tfm.timer";

static void __timer_callback(void* p_arg)
{
    MODULE_DECLARE(p_module_instance, p_arg);

    char buf[32];
    uint32_t len = 0;

    time_t now = 0;
    time(&now);
    
    len = snprintf(buf, sizeof(buf), "%d", now);

    for(int i = 0; i < p_module_instance->output_evt_num; i++) {
        tf_buffer_t buf_data;
        buf_data.type = TF_DATA_TYPE_BUFFER;
        buf_data.p_data = tf_malloc(len);  //next module use and then free
        buf_data.len = len;
        memcpy(buf_data.p_data, buf, len);
        tf_event_post(p_module_instance->p_output_evt_id[i], &buf_data, sizeof(buf_data),portMAX_DELAY);
    }
}

/*************************************************************************
 * Interface implementation
 ************************************************************************/
static int __start(void *p_module)
{
    MODULE_DECLARE(p_module_instance, p_module);
    esp_err_t ret = ESP_OK;
    const esp_timer_create_args_t timer_args = {
            .callback = &__timer_callback,
            .arg = (void*) p_module_instance,
            .name = "module timer"
    };
    ret = esp_timer_create(&timer_args, &p_module_instance->timer_handle);
    if(ret != ESP_OK) {
        return NULL;
    }
    esp_timer_start_periodic(p_module_instance->timer_handle, 1000000 * 5); //5s TODO from cfg
    return 0;
}
static int __stop(void *p_module)
{
    MODULE_DECLARE(p_module_instance, p_module);
    esp_timer_stop(p_module_instance->timer_handle);
    esp_timer_delete(p_module_instance->timer_handle);
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

    return 0;
}

static int __msgs_pub_set(void *p_module, int output_index, int *p_evt_id, int num)
{
    MODULE_DECLARE(p_module_instance, p_module);
    if( output_index == 0  && num > 0 ) {
        p_module_instance->p_output_evt_id = (int *) tf_malloc(sizeof(int) * num);//todo check
        memcpy(p_module_instance->p_output_evt_id, p_evt_id, sizeof(int) * num);
        p_module_instance->output_evt_num = num;
    } else {
        ESP_LOGW(TAG, "only support output port 0, ignore %d", output_index);
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

tf_handle_t tf_module_timer_init(tf_module_timer_t *p_module)
{
    if (NULL == p_module)
    {
        return NULL;
    }
    p_module->serv.p_module = p_module;
    p_module->serv.p_funcs = &__g_funcs;
    return &p_module->serv;
}

const char *tf_module_timer_name_get(void)
{
    return "timer";
}
const char *tf_module_timer_desc_get(void)
{
    return "timer module";
}
tf_handle_t tf_module_timer_instance(void)
{
    tf_module_timer_t *p_module = (tf_module_timer_t *) tf_malloc(sizeof(tf_module_timer_t));
    if (p_module == NULL)
    {
        return NULL;
    }
    memset(p_module, 0, sizeof(tf_module_timer_t));
    return tf_module_timer_init(p_module);
}

void tf_module_timer_instance_free(tf_handle_t handle)
{
    if (handle)
    {
        tf_module_timer_t *p_module = CONTAINER_OF(handle, tf_module_timer_t, serv);
        free(p_module);
    }
}

tf_instance_handle_t tf_module_timer_instance_handle_get(void)
{
    return (tf_instance_handle_t)tf_module_timer_instance;
}
