#ifndef _STORAGE_H
#define _STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif


#include "nvs.h"
#include "nvs_flash.h"


#define STORAGE_NAMESPACE "storage"

int storage_init(void);

esp_err_t storage_write(char *p_key, void *p_data, size_t len);

//p_len : inout
esp_err_t storage_read(char *p_key, void *p_data, size_t *p_len);
esp_err_t storage_erase();

//eg: file: /spiffs/test.text
esp_err_t storage_file_write(char *file, void *p_data, size_t len);
esp_err_t storage_file_read(char *file, void *p_data, size_t *p_len); //p_len : inout
esp_err_t storage_file_size_get(char *file, size_t *p_len);

#ifdef __cplusplus
}
#endif

#endif
