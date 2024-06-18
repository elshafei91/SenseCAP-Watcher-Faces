
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

#define TF_ENGINE_TASK_STACK_SIZE 1024 * 5
#define TF_ENGINE_TASK_PRIO 5
#define TF_ENGINE_QUEUE_SIZE 3

// task flow status
#define TF_STATUS_RUNNING               0
#define TF_STATUS_STARTING              1
#define TF_STATUS_STOP                  2
#define TF_STATUS_STOPING               3
#define TF_STATUS_IDLE                  4

#define TF_STATUS_ERR_GENERAL           100
#define TF_STATUS_ERR_JSON_PARSE        101
#define TF_STATUS_ERR_MODULE_NOT_FOUND  102
#define TF_STATUS_ERR_MODULES_INSTANCE  103
#define TF_STATUS_ERR_MODULES_PARAMS    104
#define TF_STATUS_ERR_MODULES_WIRES     105
#define TF_STATUS_ERR_MODULES_START     106
#define TF_STATUS_ERR_MODULES_INTERNAL  107   // module runtime internal error


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

typedef void (*tf_engine_status_cb_t)(void * p_arg, intmax_t tid, int status, const char *p_err_module);

typedef void (*tf_module_status_cb_t)(void * p_arg, const char *p_name, int status);

typedef struct tf_engine
{
    esp_event_loop_handle_t event_handle;
    tf_module_nodes_t module_nodes;
    TaskHandle_t task_handle;
    StaticTask_t *p_task_buf;
    StackType_t *p_task_stack_buf;
    QueueHandle_t queue_handle;
    SemaphoreHandle_t sem_handle;
    EventGroupHandle_t event_group;
    cJSON *cur_flow_root;
    tf_module_item_t *p_module_head;
    int module_item_num;
    tf_info_t tf_info;
    tf_engine_status_cb_t  status_cb;
    void * p_status_cb_arg;
    tf_module_status_cb_t  module_status_cb;
    void * p_module_status_cb_arg;
    int status;
} tf_engine_t;

esp_err_t tf_engine_init(void);

esp_err_t tf_engine_run(void);

esp_err_t tf_engine_stop(void);

esp_err_t tf_engine_flow_set(const char *p_str, size_t len);

// need free return data
char* tf_engine_flow_get(void);

esp_err_t tf_engine_tid_get(intmax_t *p_tid);

esp_err_t tf_engine_ctd_get(intmax_t *p_ctd);

esp_err_t tf_engine_type_get(int *p_type);

// need free p_info->p_tf_name
esp_err_t tf_engine_info_get(tf_info_t *p_info);

esp_err_t tf_engine_status_get(int *p_status);

esp_err_t tf_engine_status_cb_register(tf_engine_status_cb_t engine_status_cb, void *p_arg);


esp_err_t tf_module_status_set(const char *p_module_name, int status);

esp_err_t tf_module_status_cb_register(tf_module_status_cb_t module_status_cb, void *p_arg);

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
