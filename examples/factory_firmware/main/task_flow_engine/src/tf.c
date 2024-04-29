#include "tf.h"
#include <string.h>
#include "tf_parse.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"

ESP_EVENT_DEFINE_BASE(TF_EVENT_BASE);

static const char *TAG = "tf.engine";

static tf_engine_t *gp_engine = NULL;

static int __modules_sort(tf_module_item_t *p_head, int num)
{
    return 0;
}
static int __modules_instance(tf_module_item_t *p_head, int num)
{
    return 0;  
}   

static int __modules_start(tf_module_item_t *p_head, int num)
{
    return 0;  
}
static int __modules_stop(tf_module_item_t *p_head, int num)
{
    return 0;  
}
static int __modules_cfg(tf_module_item_t *p_head, int num)
{
    return 0;  
}

static int __modules_msgs_sub_set(tf_module_item_t *p_head, int num)
{
    return 0;  
}
static int __modules_msgs_pub_set(tf_module_item_t *p_head, int num)
{
    return 0;   
}
static int __modules_delete(tf_module_item_t *p_head, int num)
{
    return 0;  
}   

static void __tf_engine_task(void *p_arg)
{
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

esp_err_t tf_engine_init(void)
{
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    esp_err_t ret = ESP_OK;
    gp_engine = (tf_engine_t *)tf_malloc(sizeof(tf_engine_t));
    ESP_GOTO_ON_FALSE(gp_engine, ESP_ERR_NO_MEM, err, TAG, "no mem for tf engine");

    esp_event_loop_args_t event_task_args = {
        .queue_size = 10,
        .task_name = "tf_event_task",
        .task_priority = 6,
        .task_stack_size = 1024 * 3,
        .task_core_id = 0};
    ret = esp_event_loop_create(&event_task_args, &gp_engine->event_handle);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "event_loop_create failed");

    SLIST_INIT(&(gp_engine->module_nodes));

    // TODO create task and queue

err:
    if (gp_engine)
    {
        free(gp_engine);
        gp_engine = NULL;
    }

    return ret;
}

esp_err_t tf_engine_run(void)
{
    //TODO
    return ESP_OK;
}

esp_err_t tf_engine_flow_set(const char *p_str)
{
    //TODO send queue
    return ESP_OK;
}

esp_err_t tf_module_register(const char *p_name,
                             const char *p_desc,
                             tf_handle_t handle,
                             tf_instance_handle_t instance_handler)
{
    assert(gp_engine);
    esp_err_t ret = ESP_OK;

    if (p_name == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!SLIST_EMPTY(&(gp_engine->module_nodes)))
    {
        tf_module_node_t *it = NULL;
        SLIST_FOREACH(it, &(gp_engine->module_nodes), next)
        {
            if (strcmp(it->p_name, p_name) == 0)
            {
                ESP_LOGW(TAG, "module %s already exist", p_name);
                return ESP_ERR_INVALID_STATE;
            }
        }
    }

    tf_module_node_t *p_node = (tf_module_node_t *)malloc(sizeof(tf_module_node_t));
    if (p_node == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    p_node->p_name = p_name;
    p_node->p_desc = p_desc;
    p_node->instance_handle = instance_handler;
    p_node->handle = handle;
    SLIST_INSERT_HEAD(&(gp_engine->module_nodes), p_node, next);

    return ESP_OK;
}

esp_err_t tf_modules_report(void)
{
    return ESP_OK;
}

esp_err_t tf_event_post(int32_t event_id,
                        const void *event_data,
                        size_t event_data_size,
                        TickType_t ticks_to_wait)
{
    assert(gp_engine);
    return esp_event_post_to(gp_engine->event_handle, TF_EVENT_BASE, event_id, event_data, event_data_size, ticks_to_wait);
}

esp_err_t tf_event_handler_register(int32_t event_id,
                                    esp_event_handler_t event_handler,
                                    void *event_handler_arg)
{
    assert(gp_engine);
    return esp_event_handler_register_with(gp_engine->event_handle, TF_EVENT_BASE, event_id, event_handler, event_handler_arg);
}

esp_err_t tf_event_handler_unregister(int32_t event_id,
                                      esp_event_handler_t event_handler)
{
    assert(gp_engine);
    return esp_event_handler_unregister_with(gp_engine->event_handle, TF_EVENT_BASE, event_id, event_handler);
}