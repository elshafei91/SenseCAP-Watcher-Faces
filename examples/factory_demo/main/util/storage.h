#ifndef _STORAGE_H
#define _STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif


#include "nvs.h"
#include "nvs_flash.h"

int storage_init(void);

esp_err_t storage_write(char *p_key, void *p_data, size_t len);


//p_len : inout
esp_err_t storage_read(char *p_key, void *p_data, size_t *p_len);

#ifdef __cplusplus
}
#endif

#endif
