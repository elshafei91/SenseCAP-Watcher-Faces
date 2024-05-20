#include "tf.h"
#include <string.h>
#include <stdlib.h>
#include "tf_parse.h"
#include "tf_util.h"
#include "esp_log.h"
#include "esp_check.h"

ESP_EVENT_DEFINE_BASE(TF_EVENT_BASE);

static const char *TAG = "tf.engine";

static tf_engine_t *gp_engine = NULL;

// enum tf_engine_st {

// };

void __modules_item_print(tf_module_item_t *p_head, int num)
{
    printf("modules list: ");
    for(int i = 0; i < num; i++) {
        printf("[%s-%d] ", p_head[i].p_name, p_head[i].id);
    }
    printf("\n");
}

int __modules_index_compare(const void *a, const void *b)
{
    return ((tf_module_item_t *)b)->index - ((tf_module_item_t *)a)->index;
}
static int __modules_init(tf_engine_t *p_engine, tf_module_item_t *p_head, int num)
{
    qsort(p_head, num, sizeof(tf_module_item_t), __modules_index_compare);

    __modules_item_print(p_head, num);

    for(int i = 0; i < num; i++) {
        if (!SLIST_EMPTY(&(p_engine->module_nodes)))
        {
            tf_module_node_t *it = NULL;
            SLIST_FOREACH(it, &(p_engine->module_nodes), next)
            {
                if (strcmp(it->p_name, p_head[i].p_name) == 0)
                {
                    p_head[i].mgmt_handle = it->mgmt_handle;
                    break;
                }
            }
        }
    }
    return 0;
}
static int __modules_instance(tf_module_item_t *p_head, int num)
{
    for(int i = 0; i < num; i++) {
        p_head[i].handle = p_head[i].mgmt_handle->tf_module_instance();
        if(p_head[i].handle == NULL) {
            ESP_LOGE(TAG, "module %s instance failed", p_head[i].p_name);
            return ESP_FAIL;
        }
    }
    return 0;
}
static int __modules_destroy(tf_module_item_t *p_head, int num)
{
    for(int i = 0; i < num; i++) {
        if(p_head[i].handle != NULL) {
            p_head[i].mgmt_handle->tf_module_destroy(p_head[i].handle);
        }
    }
    return 0;
}
static int __modules_start(tf_module_item_t *p_head, int num)
{
    for(int i = 0; i < num; i++) {
        tf_module_start(p_head[i].handle);
    }
    return 0;
}
static int __modules_stop(tf_module_item_t *p_head, int num)
{
    //TODO  stop order
    for(int i = 0; i < num; i++) {
        tf_module_stop(p_head[i].handle);
    }
    return 0;
}
static int __modules_cfg(tf_module_item_t *p_head, int num)
{
    for(int i = 0; i < num; i++) {
        tf_module_cfg(p_head[i].handle, p_head[i].p_params);
    }
    return 0;
}

static int __modules_msgs_sub_set(tf_module_item_t *p_head, int num)
{
    for(int i = 0; i < num; i++) {
        tf_module_msgs_sub_set(p_head[i].handle, p_head[i].id);
    }
    return 0;
}
static int __modules_msgs_pub_set(tf_module_item_t *p_head, int num)
{
    for(int i = 0; i < num; i++) {
        for(int j = 0; j < p_head[i].output_port_num; j++) {
            tf_module_msgs_pub_set(p_head[i].handle, j,  \
                                   p_head[i].p_wires->p_evt_id, p_head[i].p_wires->num);
        }
    }
    return 0;
}

static void __tf_engine_task(void *p_arg)
{
    tf_engine_t *p_engine = (tf_engine_t *)p_arg;
    tf_flow_data_t  flow;
    int ret =  0;
    ESP_LOGI(TAG, "tf engine task start");

    bool run_flag = false;

    while (1)
    { 
        if(xQueueReceive(p_engine->queue_handle, &flow, ( TickType_t ) 10 ) == pdPASS ) {
            
            if(run_flag) {
                __modules_stop(p_engine->p_module_head, p_engine->module_item_num);
                __modules_destroy(p_engine->p_module_head, p_engine->module_item_num);
                tf_parse_free(p_engine->cur_flow_root, p_engine->p_module_head, p_engine->module_item_num);
                run_flag = false;
            }

            ret = tf_parse_json( flow.p_data, &p_engine->cur_flow_root, &p_engine->p_module_head);
            if( ret  < 0) {
                ESP_LOGE(TAG, "parse json failed");
                continue;
            }
            tf_free(flow.p_data);

            p_engine->module_item_num = ret;

            //TODO handle error
            __modules_init(p_engine, p_engine->p_module_head, p_engine->module_item_num);
            __modules_instance(p_engine->p_module_head, p_engine->module_item_num);
            __modules_cfg(p_engine->p_module_head, p_engine->module_item_num);
            __modules_msgs_sub_set(p_engine->p_module_head, p_engine->module_item_num);
            __modules_msgs_pub_set(p_engine->p_module_head, p_engine->module_item_num);
            __modules_start(p_engine->p_module_head, p_engine->module_item_num);
            run_flag = true;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
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
        .task_core_id = 1
    };
    ret = esp_event_loop_create(&event_task_args, &gp_engine->event_handle);
    ESP_GOTO_ON_ERROR(ret, err, TAG, "event_loop_create failed");

    SLIST_INIT(&(gp_engine->module_nodes));

    gp_engine->p_task_stack_buf = (StackType_t *)tf_malloc(TF_ENGINE_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(gp_engine->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "no mem for tf engine task stack");

    // task TCB must be allocated from internal memory 
    gp_engine->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(gp_engine->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "no mem for task TCB");

    gp_engine->queue_handle = xQueueCreate(TF_ENGINE_QUEUE_SIZE, sizeof(tf_flow_data_t));
    ESP_GOTO_ON_FALSE(gp_engine->queue_handle, ESP_FAIL, err, TAG, "create queue failed");

    gp_engine->task_handle = xTaskCreateStatic(
        __tf_engine_task,
        "tf_engine_task",
        TF_ENGINE_TASK_STACK_SIZE,
        (void *)gp_engine,
        TF_ENGINE_TASK_PRIO,
        gp_engine->p_task_stack_buf,
        gp_engine->p_task_buf);

    ESP_GOTO_ON_FALSE(gp_engine->task_handle, ESP_FAIL, err, TAG, "create task failed");

    return ret;
err:
    if (gp_engine)
    {
        if (gp_engine->p_task_stack_buf)
        {
            tf_free(gp_engine->p_task_stack_buf);
        }

        if (gp_engine->task_handle)
        {
            vTaskDelete(gp_engine->task_handle);
        }

        if (gp_engine->queue_handle)
        {
            vQueueDelete(gp_engine->queue_handle);
        }

        tf_free(gp_engine);
        gp_engine = NULL;
    }

    return ret;
}

esp_err_t tf_engine_run(void)
{
    // TODO
    return ESP_OK;
}

esp_err_t tf_engine_stop(void)
{
    //TODO
    return ESP_OK;
}
esp_err_t tf_engine_flow_set(const char *p_str, size_t len)
{
    assert(gp_engine);
    esp_err_t ret = ESP_OK;
    tf_flow_data_t flow;

    if( p_str == NULL || len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char *p_data = ( char *)tf_malloc(len);
    if( p_data == NULL ) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(p_data, p_str, len);

    flow.p_data = p_data;
    flow.len = len;

    if( xQueueSend(gp_engine->queue_handle, &flow, portMAX_DELAY) != pdTRUE) {
        tf_free(p_data);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t tf_module_register(const char *p_name,
                             const char *p_desc,
                             const char *p_version,
                             tf_module_mgmt_t *mgmt_handle)
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
    p_node->p_version = p_version;
    p_node->mgmt_handle = mgmt_handle;
    SLIST_INSERT_HEAD(&(gp_engine->module_nodes), p_node, next);

    ESP_LOGI(TAG, "module %s register success", p_name);
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