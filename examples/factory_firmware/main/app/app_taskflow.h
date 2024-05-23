
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

struct app_taskflow_info {
    bool is_valid;
    int  len;
    // char uuid[37];
};

void app_taskflow_init(void);

#ifdef __cplusplus
}
#endif