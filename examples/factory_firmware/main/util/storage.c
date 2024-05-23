#include "storage.h"
#include "esp_log.h" 
#include "nvs_flash.h"

#define STORAGE_NAMESPACE "FACTORYCFG"

static const char *TAG = "storage";

esp_err_t storage_init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t storage_write(const char *key, const void *value, size_t length)
{
    nvs_handle_t my_handle;
    esp_err_t ret = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(my_handle, key, value, length);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) setting blob in NVS!", esp_err_to_name(ret));
        nvs_close(my_handle);
        return ret;
    }

    ret = nvs_commit(my_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) committing NVS!", esp_err_to_name(ret));
    }

    nvs_close(my_handle);
    return ret;
}

esp_err_t storage_read(const char *key, void *value, size_t length)
{
    nvs_handle_t my_handle;
    esp_err_t ret = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(ret));
        return ret;
    }

    size_t required_size = length;
    ret = nvs_get_blob(my_handle, key, value, &required_size);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG, "Error (%s) reading blob from NVS!", esp_err_to_name(ret));
    }

    nvs_close(my_handle);
    return ret;
}

