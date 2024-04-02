#pragma once

#include "data_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICEINFO_STORAGE  "deviceinfo"

int deviceinfo_get(struct view_data_deviceinfo *p_info);

int deviceinfo_set(struct view_data_deviceinfo *p_info);

#ifdef __cplusplus
}
#endif

