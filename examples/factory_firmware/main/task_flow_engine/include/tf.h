
#pragma once
#include "tf_module.h"
#include "sys/queue.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct tf_module_node
    {
        const char *p_name;
        const char *p_desc;
        tf_handle_t handle;                   /* module handle */
        tf_instance_handle_t instance_handle; /* module instance handle */
        SLIST_ENTRY(tf_module_list)
        next;
    } tf_module_node_t;

    typedef SLIST_HEAD(tf_module_nodes, tf_module_node) tf_module_nodes_t;

    typedef struct tf_engine
    {
        esp_event_loop_handle_t event_handle;
        tf_module_nodes_t module_nodes;
        TaskHandle_t task_handle;

    } tf_engine_t;

    esp_err_t tf_engine_init(void);

    esp_err_t tf_engine_run(void);

    esp_err_t tf_engine_flow_set(const char *p_str);

    esp_err_t tf_module_register(const char *p_name,
                                 const char *p_desc,
                                 tf_handle_t handle,
                                 tf_instance_handle_t instance_handler);

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
