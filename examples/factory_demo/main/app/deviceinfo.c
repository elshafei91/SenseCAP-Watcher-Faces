#include "deviceinfo.h"
#include "storage.h"

#define DEVICEINFO_STORAGE  "deviceinfo"

int deviceinfo_get(struct view_data_deviceinfo *p_info)
{
    size_t len=sizeof(struct view_data_deviceinfo);
    memset(p_info, 0, len);
    esp_err_t ret = storage_read(DEVICEINFO_STORAGE, (void *)p_info, &len);
    if (ret != ESP_OK) {
        return ret;
	}
    return ESP_OK;
}

int deviceinfo_set(struct view_data_deviceinfo *p_info)
{
    esp_err_t ret = 0;
    ret = storage_write(DEVICEINFO_STORAGE, (void *)p_info, sizeof(struct view_data_deviceinfo));
    if( ret != ESP_OK ) {
        ESP_LOGE("", "cfg write err:%d", ret);
        return ret;
    }
    return ESP_OK;
}

