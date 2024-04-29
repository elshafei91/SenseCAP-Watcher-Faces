
#pragma once
#include <stdint.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C"
{
#endif

    struct tf_module_drv_funcs
    {
        int (*pfn_start)(void *p_module);
        int (*pfn_stop)(void *p_module);
        int (*pfn_cfg)(void *p_module, cJSON *p_json);
        int (*pfn_msgs_sub_set)(void *p_module, int evt_id);
        int (*pfn_msgs_pub_set)(void *p_module, int output_index, int *p_evt_id, int num);
        int (*pfn_delete)(void *p_module);
    };

    typedef struct tf_module_serv
    {
        struct tf_module_drv_funcs *p_funcs;
        void *p_module;
    } tf_module_serv_t;

    typedef tf_module_serv_t *tf_handle_t;

    typedef tf_handle_t (*tf_instance_handle_t)(void);

    static inline int tf_module_start(tf_handle_t handle)
    {
        return handle->p_funcs->pfn_start(handle->p_module);
    }

    static inline int tf_module_stop(tf_handle_t handle)
    {
        return handle->p_funcs->pfn_stop(handle->p_module);
    }

    static inline int tf_module_cfg(tf_handle_t handle, cJSON *p_json)
    {
        return handle->p_funcs->pfn_cfg(handle->p_module, p_json);
    }

    static inline int tf_module_msg_sub_set(tf_handle_t handle, int evt_id)
    {
        return handle->p_funcs->pfn_msgs_sub_set(handle->p_module, evt_id);
    }

    static inline int tf_module_msg_pub_set(tf_handle_t handle, int output_index, int *p_evt_id, int num)
    {
        return handle->p_funcs->pfn_msgs_pub_set(handle->p_module, output_index, p_evt_id, num);
    }

#ifdef __cplusplus
}
#endif
