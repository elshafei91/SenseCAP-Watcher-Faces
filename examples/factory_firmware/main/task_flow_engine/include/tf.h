
#pragma once
#include "tf_module.h"
#include "tf_parse.h"
#include "sys/queue.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define TF_ENGINE_TASK_STACK_SIZE 1024
#define TF_ENGINE_TASK_PRIO 5
#define TF_ENGINE_QUEUE_SIZE 5

typedef struct
{
    char *p_data;
    size_t len;
} tf_flow_data_t;

typedef struct tf_module_node
{
    const char *p_name;
    const char *p_desc;
    const char *p_version;
    tf_module_mgmt_t *mgmt_handle;
    SLIST_ENTRY(tf_module_node)
    next;
} tf_module_node_t;

typedef SLIST_HEAD(tf_module_nodes, tf_module_node) tf_module_nodes_t;

typedef struct tf_engine
{
    esp_event_loop_handle_t event_handle;
    tf_module_nodes_t module_nodes;
    TaskHandle_t task_handle;
    StaticTask_t task_buf;
    StackType_t *p_task_stack_buf;
    QueueHandle_t queue_handle;
    SemaphoreHandle_t sem_handle;
    cJSON *cur_flow_root;
    tf_module_item_t *p_module_head;
} tf_engine_t;

esp_err_t tf_engine_init(void);

esp_err_t tf_engine_run(void);

esp_err_t tf_engine_flow_set(const char *p_str, size_t len);

esp_err_t tf_module_register(const char *p_name,
                                const char *p_desc,
                                const char *p_version,
                                tf_module_mgmt_t *mgmt_handle);

esp_err_t tf_modules_report(void);

esp_err_t tf_event_post(int32_t event_id,
                        const void *event_data,
                        size_t event_data_size,
                        TickType_t ticks_to_wait);

esp_err_t tf_event_handler_register(int32_t event_id,
                                    esp_event_handler_t event_handler,
                                    void *event_handler_arg);

esp_err_t tf_event_handler_unregister(int32_t event_id,
                                        esp_event_handler_t event_handler);

#ifdef __cplusplus
}
#endif
