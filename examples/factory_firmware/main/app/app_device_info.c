
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_event.h"
#include "event_loops.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "data_defs.h"
#include "app_device_info.h"
#include "storage.h"

#include "sensecap-watcher.h"
#define SN_TAG                    "SN_TAG"
#define APP_DEVICE_INFO_MAX_STACK 4096

#define BRIGHTNESS_STORAGE "brightnressvalue"

uint8_t SN[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x18 };
uint8_t EUI[] = { 0x1C, 0xF7, 0xF1, 0xC8, 0x62, 0x10, 0x00, 0x0B, 0xF4, 0x02, 0xCE, 0x6E, 0xB4, 0x2E, 0xE8, 0xD7 };
char software_version[] = "1.0.0";       // Initialize software_version
char himax_software_version[] = "1.0.0"; // Initialize himax_software_version
int server_code = 1;
int create_batch = 1000205;
int brightness = 100;      // Initialize brightness
int brightness_past = 100; // restore past brightness value
SemaphoreHandle_t MUTEX_SN = NULL;
SemaphoreHandle_t MUTEX_software_version;
SemaphoreHandle_t MUTEX_himax_software_version;
SemaphoreHandle_t MUTEX_brightness;

static StackType_t *app_device_info_task_stack =NULL;
static StaticTask_t app_device_info_task_buffer;


void byteArrayToHexString(const uint8_t *byteArray, size_t byteArraySize, char *hexString)
{
    for (size_t i = 0; i < byteArraySize; ++i)
    {
        sprintf(&hexString[2 * i], "%02X", byteArray[i]);
    }
}

uint8_t *get_sn(int caller)
{
    uint8_t *result = NULL;
    if (xSemaphoreTake(MUTEX_SN, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("SN_TAG", "get_sn: MUTEX_SN take failed");
        return NULL;
    }
    else
    {
        switch (caller)
        {
            case BLE_CALLER:
                ESP_LOGI(SN_TAG, "BLE get sn");
                result = SN;
                xSemaphoreGive(MUTEX_SN);
                break;
            case UI_CALLER:
                ESP_LOGI(SN_TAG, "UI get sn");
                char storage_space_2[10];
                char storage_space_3[20];
                char storage_space_4[19]; // 18 chars + null terminator
                snprintf(storage_space_2, sizeof(storage_space_2), "%d", server_code);
                snprintf(storage_space_3, sizeof(storage_space_3), "%d", create_batch);
                char hexString1[33] = { 0 }; // 32 chars + null terminator for data1
                char hexString4[19] = { 0 }; // 18 chars + null terminator for data4
                byteArrayToHexString(EUI, sizeof(EUI), hexString1);
                byteArrayToHexString(SN, sizeof(SN), hexString4);
                hexString1[32] = '\0';
                hexString4[18] = '\0';
                char final_string[150];
                snprintf(final_string, sizeof(final_string), "w1:%s:%s:%s:%s", hexString1, storage_space_2, storage_space_3, hexString4);
                printf("SN: %s\n", final_string);
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, &final_string, sizeof(final_string), portMAX_DELAY);
                xSemaphoreGive(MUTEX_SN);
                break;
        }
        return result;
    }
}

uint8_t *get_eui()
{
    return EUI;
}

uint8_t *get_brightness(int caller)
{
    ESP_LOGI("BRIGHTNESS_TAG", "get_brightness");

    return NULL;
}

uint8_t *set_brightness(int caller, int value)
{
    ESP_LOGI("BRIGHTNESS_TAG", "set_brightness");
    brightness_past = brightness;
    brightness = value;
    return NULL;
}

static int __set_brightness()
{
    if (brightness_past != brightness)
    {
        esp_err_t ret = 0;
        ret = storage_write(BRIGHTNESS_STORAGE, (void *)&brightness, sizeof(int));
        if (ret != ESP_OK)
        {
            ESP_LOGE("", "cfg write err:%d", ret);
            return ret;
        }
        BSP_ERROR_CHECK_RETURN_ERR(bsp_lcd_brightness_set(brightness));
    }
    return 0;
}


char *get_software_version(int caller)
{
    char *result = NULL;
    if (xSemaphoreTake(MUTEX_software_version, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("get_software_version_TAG", "get_software_version: MUTEX_software_version take failed");
        return NULL;
    }
    else
    {
        switch (caller)
        {
            case AT_CMD_CALLER:
                ESP_LOGI(SN_TAG, "BLE get software version");
                result = strdup(software_version);
                xSemaphoreGive(MUTEX_software_version);
                break;
            case UI_CALLER:
                ESP_LOGI(SN_TAG, "UI get software version");
                char final_string[150];
                snprintf(final_string, sizeof(final_string), "v%s", software_version);
                printf("Software Version: %s\n", final_string);
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SOFTWARE_VERSION_CODE, final_string, strlen(final_string) + 1, portMAX_DELAY);
                result = strdup(software_version);
                xSemaphoreGive(MUTEX_software_version);
                break;
        }
    }
    return result;
}

char *get_himax_software_version(int caller)
{
    char *result = NULL;
    if (xSemaphoreTake(MUTEX_himax_software_version, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("get_himax_software_version_TAG", "get_himax_software_version: MUTEX_himax_software_version take failed");
        return NULL;
    }
    else
    {
        switch (caller)
        {
            case AT_CMD_CALLER:
                ESP_LOGI(SN_TAG, "BLE get himax software version");
                result = strdup(himax_software_version);
                xSemaphoreGive(MUTEX_himax_software_version);
                break;
            case UI_CALLER:
                ESP_LOGI(SN_TAG, "UI get himax software version");
                char final_string[150];
                snprintf(final_string, sizeof(final_string), "v%s", himax_software_version);
                printf("Himax Software Version: %s\n", final_string);
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_HIMAX_SOFTWARE_VERSION_CODE, final_string, strlen(final_string) + 1, portMAX_DELAY);
                result = strdup(himax_software_version);
                xSemaphoreGive(MUTEX_himax_software_version);
                break;
        }
    }
    return result;
}

void app_device_info_task(void *pvParameter)
{
    MUTEX_brightness = xSemaphoreCreateBinary();
    MUTEX_SN = xSemaphoreCreateBinary();
    MUTEX_software_version = xSemaphoreCreateBinary();
    MUTEX_himax_software_version = xSemaphoreCreateBinary(); // Initialize MUTEX_himax_software_version
    xSemaphoreGive(MUTEX_software_version);
    xSemaphoreGive(MUTEX_himax_software_version);
    xSemaphoreGive(MUTEX_SN);
    xSemaphoreGive(MUTEX_brightness);
    while (1)
    {
        // efuse or nvs read function
        //__set_brightness();
        // read and init brightness

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void app_device_info_init()
{
    app_device_info_task_stack =(StackType_t *)heap_caps_malloc(4096*sizeof(StackType_t),MALLOC_CAP_SPIRAM);
    TaskHandle_t task_handle =xTaskCreateStatic(&app_device_info_task, "app_device_info_task", 4096, NULL, 5, app_device_info_task_stack, &app_device_info_task_buffer);
}