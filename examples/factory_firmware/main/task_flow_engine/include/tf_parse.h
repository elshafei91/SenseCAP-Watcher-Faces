
#pragma once
#include "tf_module.h"

#ifdef __cplusplus
extern "C"
{
#endif
struct tf_module_wires
{
    int *p_evt_id;
    int num;
};
typedef struct tf_module_item
{
    int id;
    int index;
    const char *p_name;
    cJSON *p_params;
    struct tf_module_wires *p_wires;
    int output_port_num;
    tf_module_t *handle;
    tf_module_mgmt_t *mgmt_handle;
} tf_module_item_t;

int tf_parse_json(const char *p_str,
                    cJSON **pp_json_root,
                    tf_module_item_t **pp_head);

void tf_parse_free(cJSON *p_json_root, tf_module_item_t *p_head, int num);

#ifdef __cplusplus
}
#endif
