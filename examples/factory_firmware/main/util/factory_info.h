#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct factory_info{
    char *sn;
    char *eui;
    char *code;
    char *device_key;
    char *ai_key; 
    char *device_control_key;
    bool server;
} factory_info_t;

esp_err_t factory_info_init(void);

const factory_info_t *factory_info_get(void);

esp_err_t factory_info_get1(factory_info_t *p_info);

#ifdef __cplusplus
}
#endif

