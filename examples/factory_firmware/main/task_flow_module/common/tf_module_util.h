
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void tf_module_data_free(void *event_data);
void tf_module_data_type_err_handle(void *event_data);
const char * tf_module_data_type_to_str(uint8_t type);



#ifdef __cplusplus
}
#endif
