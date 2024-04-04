#pragma once

#include "esp_err.h"

#include "data_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICEINFO_STORAGE  "deviceinfo"

int deviceinfo_get(struct view_data_deviceinfo *p_info);

int deviceinfo_set(struct view_data_deviceinfo *p_info);

esp_err_t app_device_status_monitor_init(void);

#ifdef __cplusplus
}
#endif

