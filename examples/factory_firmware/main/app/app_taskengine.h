#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "data_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t app_taskengine_init(void);

esp_err_t app_taskengine_register_task_executor(void *something);

intmax_t app_taskengine_get_current_tlid(void);

#ifdef __cplusplus
}
#endif
