#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_event.h"
#include "event_loops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "data_defs.h"
#include "app_device_info.h"
#include "storage.h"
#include "sensecap-watcher.h"

#define SN_TAG                    "SN_TAG"
#define APP_DEVICE_INFO_MAX_STACK 4096
#define BRIGHTNESS_STORAGE_KEY    "brightness"
#define RGB_SWITCH_STORAGE_KEY    "rgbswitch"

uint8_t SN[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x11 };
uint8_t EUI[] = { 0x2C, 0xF7, 0xF1, 0xC2, 0x44, 0x81, 0x00, 0x47, 0xB0, 0x47, 0xD1, 0xD5, 0x8B, 0xC7, 0xF8, 0xFB };
char software_version[] = "1.0.0";
char himax_software_version[] = "1.0.0";
int server_code = 1;
int create_batch = 1000205;

int brightness = 100;
int brightness_past = 100;

int rgb_switch = 0;
int rgb_switch_past = 0;

SemaphoreHandle_t MUTEX_SN = NULL;
SemaphoreHandle_t MUTEX_software_version;
SemaphoreHandle_t MUTEX_himax_software_version;
SemaphoreHandle_t MUTEX_brightness;
SemaphoreHandle_t MUTEX_rgb_switch;

static StackType_t *app_device_info_task_stack = NULL;
static StaticTask_t app_device_info_task_buffer;

void app_device_info_task(void *pvParameter);

/*----------------------------------------------------tool function---------------------------------------------*/
void byteArrayToHexString(const uint8_t *byteArray, size_t byteArraySize, char *hexString)
{
    for (size_t i = 0; i < byteArraySize; ++i)
    {
        sprintf(&hexString[2 * i], "%02X", byteArray[i]);
    }
}
/*-------------------------------------------------------------------------------------------------------------*/

/*----------------------------------------------------------init function--------------------------------------*/
void app_device_info_init()
{
    app_device_info_task_stack = (StackType_t *)pvPortMalloc(APP_DEVICE_INFO_MAX_STACK * sizeof(StackType_t));
    if (app_device_info_task_stack == NULL)
    {
        ESP_LOGE(SN_TAG, "Failed to allocate memory for task stack");
        return;
    }

    TaskHandle_t task_handle = xTaskCreateStatic(&app_device_info_task, "app_device_info_task", APP_DEVICE_INFO_MAX_STACK, NULL, 5, app_device_info_task_stack, &app_device_info_task_buffer);
    if (task_handle == NULL)
    {
        ESP_LOGE(SN_TAG, "Failed to create task");
    }
}

void init_rgb_switch_from_nvs()
{
    size_t size = sizeof(rgb_switch);
    esp_err_t ret = storage_read(RGB_SWITCH_STORAGE_KEY, &rgb_switch, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI("NVS", "rgb_switch value loaded from NVS: %d", rgb_switch);
        rgb_switch_past = rgb_switch;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "No rgb_switch value found in NVS. Using default: %d", rgb_switch);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading rgb_switch from NVS: %s", esp_err_to_name(ret));
    }
}

void init_brightness_from_nvs()
{
    size_t size = sizeof(brightness);
    esp_err_t ret = storage_read(BRIGHTNESS_STORAGE_KEY, &brightness, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI("NVS", "Brightness value loaded from NVS: %d", brightness);
        ret = bsp_lcd_brightness_set(brightness);
        if (ret != ESP_OK)
        {
            ESP_LOGE("BRIGHTNESS_TAG", "LCD brightness set err:%d", ret);
        }
        brightness_past = brightness;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "No brightness value found in NVS. Using default: %d", brightness);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading brightness from NVS: %s", esp_err_to_name(ret));
    }
}
/*----------------------------------------------------------------------------------------------------------------------*/

uint8_t *get_sn(int caller)
{
    uint8_t *result = NULL;
    if (xSemaphoreTake(MUTEX_SN, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(SN_TAG, "get_sn: MUTEX_SN take failed");
        return NULL;
    }
    else
    {
        switch (caller)
        {
            case BLE_CALLER:
                ESP_LOGI(SN_TAG, "BLE get sn");
                result = SN;
                break;
            case UI_CALLER:
                ESP_LOGI(SN_TAG, "UI get sn");
                char storage_space_2[10];
                char storage_space_3[20];
                char storage_space_4[19];
                snprintf(storage_space_2, sizeof(storage_space_2), "%d", server_code);
                snprintf(storage_space_3, sizeof(storage_space_3), "%d", create_batch);
                char hexString1[33] = { 0 };
                char hexString4[19] = { 0 };
                byteArrayToHexString(EUI, sizeof(EUI), hexString1);
                byteArrayToHexString(SN, sizeof(SN), hexString4);
                hexString1[32] = '\0';
                hexString4[18] = '\0';
                char final_string[150];
                snprintf(final_string, sizeof(final_string), "w1:%s:%s:%s:%s", hexString1, storage_space_2, storage_space_3, hexString4);
                printf("SN: %s\n", final_string);
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, final_string, sizeof(final_string), portMAX_DELAY);
                break;
        }
        xSemaphoreGive(MUTEX_SN);
        return result;
    }
}

uint8_t *get_eui()
{
    return EUI;
}

/*----------------------------------------------brightness module------------------------------------------------------*/
uint8_t *get_brightness(int caller)
{
    if (xSemaphoreTake(MUTEX_brightness, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("BRIGHTNESS_TAG", "get_brightness: MUTEX_brightness take failed");
        return NULL;
    }
    uint8_t *result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI("BRIGHTNESS_TAG", "BLE get brightness");
            result = (uint8_t *)&brightness;
            break;
        case UI_CALLER:
            ESP_LOGI("BRIGHTNESS_TAG", "UI get brightness");
            result = (uint8_t *)&brightness;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS, result, sizeof(uint8_t *), portMAX_DELAY);
            break;
    }
    xSemaphoreGive(MUTEX_brightness);
    return brightness;
}

uint8_t *set_brightness(int caller, int value)
{
    ESP_LOGI("BRIGHTNESS_TAG", "set_brightness");
    if (xSemaphoreTake(MUTEX_brightness, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("BRIGHTNESS_TAG", "set_brightness: MUTEX_brightness take failed");
        return NULL;
    }
    brightness_past = brightness;
    brightness = value;

    xSemaphoreGive(MUTEX_brightness);
    return NULL;
}

static int __set_brightness()
{
    if (xSemaphoreTake(MUTEX_brightness, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("BRIGHTNESS_TAG", "set_brightness: MUTEX_brightness take failed");
        return NULL;
    }
    if (brightness_past != brightness)
    {
        esp_err_t ret = storage_write(BRIGHTNESS_STORAGE_KEY, &brightness, sizeof(brightness));
        if (ret != ESP_OK)
        {
            ESP_LOGE("BRIGHTNESS_TAG", "cfg write err:%d", ret);
            return ret;
        }
        ret = bsp_lcd_brightness_set(brightness);
        if (ret != ESP_OK)
        {
            ESP_LOGE("BRIGHTNESS_TAG", "LCD brightness set err:%d", ret);
            return ret;
        }
    }
    xSemaphoreGive(MUTEX_brightness);
    return 0;
}
/*---------------------------------------------------------------------------------------------------------------------*/

/*---------------------------------------------version module--------------------------------------------------------------*/

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
                break;
            case UI_CALLER:
                ESP_LOGI(SN_TAG, "UI get software version");
                char final_string[150];
                snprintf(final_string, sizeof(final_string), "v%s", software_version);
                printf("Software Version: %s\n", final_string);
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SOFTWARE_VERSION_CODE, final_string, strlen(final_string) + 1, portMAX_DELAY);
                result = strdup(software_version);
                break;
        }
        xSemaphoreGive(MUTEX_software_version);
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
                break;
            case UI_CALLER:
                ESP_LOGI(SN_TAG, "UI get himax software version");
                char final_string[150];
                snprintf(final_string, sizeof(final_string), "v%s", himax_software_version);
                printf("Himax Software Version: %s\n", final_string);
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_HIMAX_SOFTWARE_VERSION_CODE, final_string, strlen(final_string) + 1, portMAX_DELAY);
                result = strdup(himax_software_version);
                break;
        }
        xSemaphoreGive(MUTEX_himax_software_version);
    }
    return result;
}

/*---------------------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------rgb switch  module----------------------------------------------------------------*/

uint8_t *get_rgb_switch(int caller)
{
    if (xSemaphoreTake(MUTEX_rgb_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("rgb_switch_TAG", "get_brightness: MUTEX_rgb_switch take failed");
        return NULL;
    }
    uint8_t *result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI("rgb_switch_TAG", "BLE get rgb_switch");
            result = (uint8_t *)&rgb_switch;
            break;
        case UI_CALLER:
            ESP_LOGI("rgb_switch_TAG", "UI get rgb_switch");
            result = (uint8_t *)&rgb_switch;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_RGB_SWITCH, result, sizeof(uint8_t *), portMAX_DELAY);
            break;
    }
    xSemaphoreGive(MUTEX_rgb_switch);
    return rgb_switch;
}

uint8_t *set_rgb_switch(int caller, int value)
{
    ESP_LOGI("rgb_switch_TAG", "set_brightness");
    if (xSemaphoreTake(MUTEX_rgb_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("rgb_switch_TAG", "set_brightness: MUTEX_rgb_switch take failed");
        return NULL;
    }
    rgb_switch_past = rgb_switch;
    rgb_switch = value;

    xSemaphoreGive(MUTEX_rgb_switch);
    return NULL;
}

static int __set_rgb_switch()
{
    if (xSemaphoreTake(MUTEX_rgb_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("rgb_switch_TAG", "set_rgb_switch: MUTEX_rgb_switch take failed");
        return NULL;
    }
    if (rgb_switch_past != rgb_switch)
    {
        esp_err_t ret = storage_write(RGB_SWITCH_STORAGE_KEY, &rgb_switch, sizeof(rgb_switch));
        printf("rgb_switch: %d\n", rgb_switch);
        if (ret != ESP_OK)
        {
            ESP_LOGE("rgb_switch_TAG", "cfg write err:%d", ret);
            return ret;
        }
    }
    xSemaphoreGive(MUTEX_rgb_switch);
    return 0;
}

/*-----------------------------------------------------TASK----------------------------------------------------------*/
void app_device_info_task(void *pvParameter)
{
    MUTEX_brightness = xSemaphoreCreateMutex();
    MUTEX_SN = xSemaphoreCreateMutex();
    MUTEX_software_version = xSemaphoreCreateMutex();
    MUTEX_himax_software_version = xSemaphoreCreateMutex();
    MUTEX_rgb_switch = xSemaphoreCreateMutex();
    init_brightness_from_nvs();
    init_rgb_switch_from_nvs();
    while (1)
    {
        __set_brightness();
        __set_rgb_switch();
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/*------------------------------------------------------------------------------------------------------------------*/
