
#pragma once
#include "tf_module.h"
#include "tf_module_util.h"
#include "tf_module_data_type.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct tf_module_timer
    {
        int *p_output_evt_id;
        int output_evt_num;
        esp_timer_handle_t  timer_handle;
        tf_module_serv_t serv;
    } tf_module_timer_t;

    tf_handle_t tf_module_timer_init(tf_module_timer_t *p_module);

    const char *tf_module_timer_name_get(void);
    const char *tf_module_timer_desc_get(void);
    tf_instance_handle_t tf_module_timer_instance_handle_get(void);

    // TEST
    tf_handle_t tf_module_timer_instance(void);
    void tf_module_timer_instance_free(tf_handle_t handle);

#ifdef __cplusplus
}
#endif
