
#pragma once
#include "tf_module.h"
#include "tf_module_util.h"
#include "tf_module_data_type.h"


#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct tf_module_debug
    {
        int evt_id;
        tf_module_serv_t serv;
    } tf_module_debug_t;

    tf_handle_t tf_module_debug_init(tf_module_debug_t *p_module);

    const char *tf_module_debug_name_get(void);
    const char *tf_module_debug_desc_get(void);
    tf_instance_handle_t tf_module_debug_instance_handle_get(void);

    // TEST
    tf_handle_t tf_module_debug_instance(void);
    void tf_module_debug_instance_free(tf_handle_t handle);

#ifdef __cplusplus
}
#endif
