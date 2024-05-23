#ifndef _STORAGE_H
#define _STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif


#include "nvs.h"
#include "nvs_flash.h"


#define STORAGE_NAMESPACE "storage"

int storage_init(void);

esp_err_t storage_write(const char *key, const void *value, size_t length);


//p_len : inout
esp_err_t storage_read(const char *key, void *value, size_t length);

#ifdef __cplusplus
}
#endif

#endif
