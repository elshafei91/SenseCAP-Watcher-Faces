#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_err.h"

#include "storage.h"
#include "event_loops.h"

#define STORAGE_NAMESPACE "FACTORYCFG"

ESP_EVENT_DEFINE_BASE(STORAGE_EVENT_BASE);

enum {
    EVENT_STG_WRITE,
    EVENT_STG_READ,
};

typedef struct {
    SemaphoreHandle_t sem;
    char *key;
    void *data;
    size_t len;
    esp_err_t err;
} storage_event_data_t;


static esp_err_t __storage_write(char *p_key, void *p_data, size_t len)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(my_handle,  p_key, p_data, len);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return err;
    }
    err = nvs_commit(my_handle);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return err;
    }
    nvs_close(my_handle);
    return ESP_OK;
}

static esp_err_t __storage_read(char *p_key, void *p_data, size_t *p_len)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_get_blob(my_handle, p_key, p_data, p_len);
    if (err != ESP_OK) {
        nvs_close(my_handle);
        return err;
    }
    nvs_close(my_handle);
    return ESP_OK;
}

static void __storage_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    storage_event_data_t *evtdata = *(storage_event_data_t **)event_data;

    switch (id) {
        case EVENT_STG_WRITE:
            evtdata->err = __storage_write(evtdata->key, evtdata->data, evtdata->len);
            xSemaphoreGive(evtdata->sem);
            break;
        case EVENT_STG_READ:
            evtdata->err = __storage_read(evtdata->key, evtdata->data, &(evtdata->len));
            xSemaphoreGive(evtdata->sem);
            break;
        default:
            break;
    }
}

int storage_init(void)
{
    //ESP_ERROR_CHECK(nvs_flash_erase());
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, STORAGE_EVENT_BASE, ESP_EVENT_ANY_ID,
                                                    __storage_event_handler, NULL));

    return ret;
}

esp_err_t storage_write(char *p_key, void *p_data, size_t len)
{
    storage_event_data_t evtdata = {
        .sem = xSemaphoreCreateBinary(),
        .key = p_key,
        .data = p_data,
        .len = len,
        .err = ESP_OK
    };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_WRITE, 
                      &pevtdata, sizeof(storage_event_data_t *),  portMAX_DELAY);
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    return evtdata.err;
}

esp_err_t storage_read(char *p_key, void *p_data, size_t *p_len)
{
    storage_event_data_t evtdata = {
        .sem = xSemaphoreCreateBinary(),
        .key = p_key,
        .data = p_data,
        .len = *p_len,
        .err = ESP_OK
    };
    storage_event_data_t *pevtdata = &evtdata;

    esp_event_post_to(app_event_loop_handle, STORAGE_EVENT_BASE, EVENT_STG_READ, 
                      &pevtdata, sizeof(storage_event_data_t *),  portMAX_DELAY);
    xSemaphoreTake(evtdata.sem, portMAX_DELAY);
    vSemaphoreDelete(evtdata.sem);

    *p_len = evtdata.len;

    return evtdata.err;
}

