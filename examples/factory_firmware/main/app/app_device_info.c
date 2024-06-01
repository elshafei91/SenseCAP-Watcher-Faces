#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"

#include "sensecap-watcher.h"

#include "app_device_info.h"
#include "event_loops.h"
#include "data_defs.h"
#include "storage.h"
#include "app_rgb.h"
#include "app_audio.h"
#include "audio_player.h"
#include "app_sensecraft.h"
#include "tf_module_ai_camera.h"
#include "factory_info.h"

#define APP_DEVICE_INFO_MAX_STACK 4096
#define BRIGHTNESS_STORAGE_KEY    "brightness"
#define SOUND_STORAGE_KEY         "sound"
#define RGB_SWITCH_STORAGE_KEY    "rgbswitch"
#define CLOUD_SERVICE_STORAGE_KEY "cssk"
#define AI_SERVICE_STORAGE_KEY    "aiservice"
#define RESET_FACTORY_SK          "resetfactory"
#define TIME_AUTOMATIC_SK         "time_auto"
static const char *TAG = "deviceinfo";

uint8_t SN[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
uint8_t EUI[] = { 0x2C, 0xF7, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

int server_code = 1;
int create_batch = 1000205;

int brightness = 100;
int brightness_past = 100;

int sound_value = 50;
int sound_value_past = 50;

int rgb_switch = 1;
int rgb_switch_past = 1;

int cloud_service_switch = 1;
int cloud_service_switch_past = 1;

int reset_factory_switch = 0;
int reset_factory_switch_past = 0;

int time_automatic = 0;
int time_automatic_past = 0;

// ai service ip for mqtt
ai_service_pack ai_service;
ai_service_pack ai_service_past;

SemaphoreHandle_t MUTEX_SN = NULL;
SemaphoreHandle_t MUTEX_software_version;
SemaphoreHandle_t MUTEX_himax_software_version;
SemaphoreHandle_t MUTEX_brightness;
SemaphoreHandle_t MUTEX_rgb_switch;
SemaphoreHandle_t MUTEX_sound;
SemaphoreHandle_t MUTEX_ai_service;
SemaphoreHandle_t MUTEX_cloud_service_switch;
SemaphoreHandle_t MUTEX_reset_factory;
SemaphoreHandle_t MUTEX_time_automatic;
SemaphoreHandle_t MUTEX_sdcard_flash_status;

static StackType_t *app_device_info_task_stack = NULL;
static StaticTask_t app_device_info_task_buffer;

static struct view_data_device_status g_device_status;
static struct view_data_sdcard_flash_status g_sdcard_flash_status;
static volatile atomic_bool g_mqttconn = ATOMIC_VAR_INIT(false);

void app_device_info_task(void *pvParameter);

/*----------------------------------------------------tool function---------------------------------------------*/
void byteArrayToHexString(const uint8_t *byteArray, size_t byteArraySize, char *hexString)
{
    for (size_t i = 0; i < byteArraySize; ++i)
    {
        sprintf(&hexString[2 * i], "%02X", byteArray[i]);
    }
}

void string_to_byte_array(const char *str, uint8_t *byte_array, size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        sscanf(str + 2 * i, "%2hhx", &byte_array[i]);
    }
}

/*----------------------------------------------------------init function--------------------------------------*/

void init_sn_from_nvs()
{
    const char *sn_str = factory_info_sn_get();
    string_to_byte_array(sn_str, SN, 9);
}

uint8_t eui[8]={0};
void init_eui_from_nvs()
{
    const char *eui_str = factory_info_eui_get();
    const char *code_str = factory_info_code_get();
    if (eui_str == NULL || code_str == NULL)
    {
        ESP_LOGE(TAG, "Failed to get factory information of eui and code \n");
        return;
    }

    uint8_t code[8];
    string_to_byte_array(eui_str, eui, 8);
    string_to_byte_array(code_str, code, 8);
    memcpy(EUI, eui, 8);
    memcpy(EUI + 8, code, 8);
}

void init_batchid_from_nvs()
{
    const char *batchid = factory_info_batchid_get();
    create_batch = atoi(batchid);
    return;
}

void init_server_code_from_nvs()
{
    uint8_t platform = factory_info_platform_get();
    server_code = (int)platform;
    return;
}

void init_ai_service_param_from_nvs()
{
    size_t size = sizeof(ai_service);
    esp_err_t ret = storage_read(AI_SERVICE_STORAGE_KEY, &ai_service, &size);
    if (ret == ESP_OK)
    {
        ai_service_past = ai_service;
        ESP_LOGI(TAG, "ai_service value loaded from NVS: %d", ai_service);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "No ai_service value found in NVS. Using default: %d", ai_service);
    }
    else
    {
        ESP_LOGE(TAG, "Error reading ai_service from NVS: %s", esp_err_to_name(ret));
    }
}

void init_rgb_switch_from_nvs()
{
    size_t size = sizeof(rgb_switch);
    esp_err_t ret = storage_read(RGB_SWITCH_STORAGE_KEY, &rgb_switch, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "rgb_switch value loaded from NVS: %d", rgb_switch);
        rgb_switch_past = rgb_switch;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "No rgb_switch value found in NVS. Using default: %d", rgb_switch);
    }
    else
    {
        ESP_LOGE(TAG, "Error reading rgb_switch from NVS: %s", esp_err_to_name(ret));
    }
}

void init_brightness_from_nvs()
{
    size_t size = sizeof(brightness);
    esp_err_t ret = storage_read(BRIGHTNESS_STORAGE_KEY, &brightness, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Brightness value loaded from NVS: %d", brightness);
        ret = bsp_lcd_brightness_set(brightness);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "LCD brightness set err:%d", ret);
        }
        brightness_past = brightness;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No brightness value found in NVS. Using default: %d", brightness);
        ret = bsp_lcd_brightness_set(brightness);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "LCD brightness set err:%d", ret);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error reading brightness from NVS: %s", esp_err_to_name(ret));
    }
}

void init_soud_from_nvs()
{
    size_t size = sizeof(sound_value);
    esp_err_t ret = storage_read(SOUND_STORAGE_KEY, &sound_value, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Sound value loaded from NVS: %d", sound_value);
        ret = bsp_codec_volume_set(sound_value, NULL);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "sound value set err:%d", ret);
        }
        sound_value_past = sound_value;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "No sound value found in NVS. Using default: %d", sound_value);
    }
    else
    {
        ESP_LOGE(TAG, "Error reading sound value from NVS: %s", esp_err_to_name(ret));
    }
}

void init_cloud_service_switch_from_nvs()
{
    size_t size = sizeof(cloud_service_switch);
    esp_err_t ret = storage_read(CLOUD_SERVICE_STORAGE_KEY, &cloud_service_switch, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "cloud_service_switch value loaded from NVS: %d", cloud_service_switch);
        cloud_service_switch_past = cloud_service_switch;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No cloud_service_switch value found in NVS. Using default: %d", cloud_service_switch);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading rgb_switch from NVS: %s", esp_err_to_name(ret));
    }
}

void init_reset_factory_switch_from_nvs()
{
    size_t size = sizeof(reset_factory_switch);
    esp_err_t ret = storage_read(RESET_FACTORY_SK, &reset_factory_switch, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "reset_factory_switch value loaded from NVS: %d", reset_factory_switch);
        reset_factory_switch_past = reset_factory_switch;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No reset_factory_switch value found in NVS. Using default: %d", reset_factory_switch);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading reset_factory_switch from NVS: %s", esp_err_to_name(ret));
    }
}

void init_time_automatic_switch_from_nvs()
{
    size_t size = sizeof(time_automatic);
    esp_err_t ret = storage_read(TIME_AUTOMATIC_SK, &time_automatic, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "time_automatic value loaded from NVS: %d", time_automatic);
        time_automatic_past = time_automatic;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI(TAG, "No time_automatic value found in NVS. Using default: %d", time_automatic);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading time_automatic from NVS: %s", esp_err_to_name(ret));
    }
}
/*----------------------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------GET FACTORY cfg----------------------------------------------------------*/

uint8_t *get_sn(int caller)
{
    uint8_t *result = NULL;
    if (xSemaphoreTake(MUTEX_SN, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "get_sn: MUTEX_SN take failed");
        return NULL;
    }
    else
    {
        switch (caller)
        {
            case BLE_CALLER:
                ESP_LOGI(TAG, "BLE get sn");
                result = SN;
                break;
            case UI_CALLER:
                ESP_LOGI(TAG, "UI get sn");
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
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, final_string, sizeof(final_string), portMAX_DELAY);
                break;
        }
        xSemaphoreGive(MUTEX_SN);
        return result;
    }
}

uint8_t *get_bt_mac()
{
    const uint8_t *bd_addr = esp_bt_dev_get_address();
    if (bd_addr)
    {
        ESP_LOGI(TAG, "Bluetooth MAC Address: %02X:%02X:%02X:%02X:%02X:%02X", bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to get Bluetooth MAC Address");
    }
    return bd_addr;
}

uint8_t *get_sn_code()
{
    return SN;
}

uint8_t *get_eui()
{
    return eui;
}

/*----------------------------------------------brightness module------------------------------------------------------*/
int get_brightness(int caller)
{
    if (xSemaphoreTake(MUTEX_brightness, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "get_brightness: MUTEX_brightness take failed");
        return NULL;
    }
    uint8_t *result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "BLE get brightness");
            result = (uint8_t *)&brightness;
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get brightness");
            result = (uint8_t *)&brightness;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS, result, sizeof(uint8_t *), portMAX_DELAY);
            break;
    }
    xSemaphoreGive(MUTEX_brightness);
    return brightness;
}

uint8_t *set_brightness(int caller, int value)
{
    ESP_LOGI(TAG, "set_brightness");
    if (xSemaphoreTake(MUTEX_brightness, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_brightness: MUTEX_brightness take failed");
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
        ESP_LOGE(TAG, "set_brightness: MUTEX_brightness take failed");
        return NULL;
    }
    if (brightness_past != brightness)
    {
        esp_err_t ret = storage_write(BRIGHTNESS_STORAGE_KEY, &brightness, sizeof(brightness));
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "set_brightness cfg write err:%d", ret);
            return ret;
        }
        ret = bsp_lcd_brightness_set(brightness);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "LCD brightness set err:%d", ret);
            return ret;
        }
    }
    xSemaphoreGive(MUTEX_brightness);
    return 0;
}

/*---------------------------------------------version module--------------------------------------------------------------*/

char *get_software_version(int caller)
{
    char *result = NULL;
    if (xSemaphoreTake(MUTEX_software_version, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "get_software_version: MUTEX_software_version take failed");
        return NULL;
    }
    else
    {
        switch (caller)
        {
            case AT_CMD_CALLER:
                ESP_LOGI(TAG, "BLE get software version");
                result = g_device_status.fw_version;
                break;
            case UI_CALLER:
                ESP_LOGI(TAG, "UI get software version");
                char final_string[150];
                snprintf(final_string, sizeof(final_string), "v%s", g_device_status.fw_version);
                ESP_LOGD(TAG, "Software Version: %s\n", final_string);
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SOFTWARE_VERSION_CODE, final_string, strlen(final_string) + 1, portMAX_DELAY);
                result = g_device_status.fw_version;
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
        ESP_LOGE(TAG, "get_himax_software_version: MUTEX_himax_software_version take failed");
        return NULL;
    }
    else
    {
        switch (caller)
        {
            case AT_CMD_CALLER:
                ESP_LOGI(TAG, "BLE get himax software version");
                result = g_device_status.himax_fw_version;
                break;
            case UI_CALLER:
                ESP_LOGI(TAG, "UI get himax software version");
                char final_string[150];
                snprintf(final_string, sizeof(final_string), "v%s", g_device_status.himax_fw_version);
                ESP_LOGD(TAG, "Himax Software Version: %s\n", final_string);
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_HIMAX_SOFTWARE_VERSION_CODE, final_string, strlen(final_string) + 1, portMAX_DELAY);
                result = g_device_status.himax_fw_version;
                break;
        }
        xSemaphoreGive(MUTEX_himax_software_version);
    }
    return result;
}

/*--------------------------------------------rgb switch  module----------------------------------------------------------------*/

int *get_rgb_switch(int caller)
{
    if (xSemaphoreTake(MUTEX_rgb_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(xTaskGenericNotifyFromISR, "get_rgb_switch: MUTEX_rgb_switch take failed");
        return NULL;
    }
    int *result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "BLE get rgb_switch");
            result = &rgb_switch;
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get rgb_switch");
            result = &rgb_switch;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_RGB_SWITCH, result, sizeof(int), portMAX_DELAY);
            break;
    }
    xSemaphoreGive(MUTEX_rgb_switch);
    return rgb_switch;
}

uint8_t *set_rgb_switch(int caller, int value)
{
    if (xSemaphoreTake(MUTEX_rgb_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_rgb_switch: MUTEX_rgb_switch take failed");
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
        ESP_LOGE(TAG, "set_rgb_switch: MUTEX_rgb_switch take failed");
        return NULL;
    }
    if (rgb_switch_past != rgb_switch)
    {
        esp_err_t ret = storage_write(RGB_SWITCH_STORAGE_KEY, &rgb_switch, sizeof(rgb_switch));
        ESP_LOGD(TAG, "rgb_switch: %d\n", rgb_switch);
        if (rgb_switch == 1)
        {
            set_rgb_with_priority(AT_CMD_CALLER, on);
        }
        else
        {
            set_rgb_with_priority(AT_CMD_CALLER, off);
        }
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "cfg write err:%d", ret);
            return ret;
        }
        rgb_switch_past = rgb_switch;
    }
    xSemaphoreGive(MUTEX_rgb_switch);
    return 0;
}

/*-----------------------------------------------------sound_Volume---------------------------------------------------*/

uint8_t *get_sound(int caller)
{
    if (xSemaphoreTake(MUTEX_sound, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "get_sound: MUTEX_sound take failed");
        return NULL;
    }
    uint8_t *result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "AT_CMD_CALLER get sound");
            result = (uint8_t *)&sound_value;
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get sound");
            result = (uint8_t *)&sound_value;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SOUND, result, sizeof(uint8_t *), portMAX_DELAY);
            break;
    }
    xSemaphoreGive(MUTEX_sound);
    return sound_value;
}

uint8_t *set_sound(int caller, int value)
{
    if (xSemaphoreTake(MUTEX_sound, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_sound: MUTEX_sound take failed");
        return NULL;
    }
    sound_value_past = sound_value;
    sound_value = value;

    xSemaphoreGive(MUTEX_sound);
    return NULL;
}

static int __set_sound()
{
    if (xSemaphoreTake(MUTEX_sound, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_sound: MUTEX_sound take failed");
        return NULL;
    }
    if (sound_value_past != sound_value)
    {
        esp_err_t ret = storage_write(SOUND_STORAGE_KEY, &sound_value, sizeof(sound_value));
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "sound cfg write err:%d", ret);
            return ret;
        }
        ret = bsp_codec_volume_set(sound_value, NULL);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "sound set err:%d", ret);
            return ret;
        }
    }
    xSemaphoreGive(MUTEX_sound);
    return 0;
}

/*-----------------------------------------------------Claud_service_switch------------------------------------------*/
int get_cloud_service_switch(int caller)
{
    if (xSemaphoreTake(MUTEX_cloud_service_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "get_Claud_service_switch: MUTEX_Claud_service_switch take failed");
        return NULL;
    }
    int result = cloud_service_switch;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "AT_CMD_CALLER get Claud_service_switch");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get Claud_service_switch");
            break;
    }
    xSemaphoreGive(MUTEX_cloud_service_switch);
    return result;
}

esp_err_t set_cloud_service_switch(int caller, int value)
{
    if (xSemaphoreTake(MUTEX_cloud_service_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_cloud_service_switch: MUTEX_cloud_service_switch take failed");
        return ESP_FAIL;
    }
    cloud_service_switch_past = cloud_service_switch;
    cloud_service_switch = value;

    xSemaphoreGive(MUTEX_cloud_service_switch);
    return ESP_OK;
}

static int __set_cloud_service_switch()
{
    if (xSemaphoreTake(MUTEX_cloud_service_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "__set_cloud_service_switch: MUTEX_cloud_service_switch take failed");
        return NULL;
    }
    if (cloud_service_switch_past != cloud_service_switch)
    {
        esp_err_t ret = storage_write(CLOUD_SERVICE_STORAGE_KEY, &cloud_service_switch, sizeof(cloud_service_switch));
        ESP_LOGD(TAG, "cloud_service_switch: %d\n", cloud_service_switch);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "cloud_service_switch cfg write err:%d", ret);
            return ret;
        }
    }
    xSemaphoreGive(MUTEX_cloud_service_switch);
    return 0;
}
/*----------------------------------------------------AI_service_package----------------------------------------------*/
ai_service_pack *get_ai_service(int caller)
{
    if (xSemaphoreTake(MUTEX_ai_service, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "get_ai_service: MUTEX_ai_service take failed");
        return NULL;
    }
    ai_service_pack *result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "BLE get ai_service");
            result = &ai_service;
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get ai_service");
            result = &ai_service;
            break;
    }
    xSemaphoreGive(MUTEX_ai_service);
    return result;
}

void set_ai_service(int caller, ai_service_pack value)
{
    if (xSemaphoreTake(MUTEX_ai_service, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_ai_service: MUTEX_ai_service take failed");
        return;
    }
    ai_service_past = ai_service;
    ai_service = value;

    xSemaphoreGive(MUTEX_ai_service);
}

static int __set_ai_service()
{
    if (xSemaphoreTake(MUTEX_ai_service, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_ai_service: MUTEX_ai_service take failed");
        return -1;
    }
    if (memcmp(&ai_service_past, &ai_service, sizeof(ai_service_pack)) != 0)
    {
        esp_err_t ret = storage_write(AI_SERVICE_STORAGE_KEY, &ai_service, sizeof(ai_service));
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "set_ai_service cfg write err:%d", ret);
            xSemaphoreGive(MUTEX_ai_service);
            return ret;
        }
    }
    xSemaphoreGive(MUTEX_ai_service);
    return 0;
}

/*----------------------------------------------------reset-factory--------------------------------------------------------*/

int *get_reset_factory(int caller)
{
    if (xSemaphoreTake(MUTEX_reset_factory, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "get_reset_factory: MUTEX_reset_factory take failed");
        return NULL;
    }
    int *result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "AT_CMD_CALLER  get_reset_factory_TAG");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI  get_reset_factory_TAG");
            result = &reset_factory_switch;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_FACTORY_RESET_CODE, result, sizeof(int), portMAX_DELAY);
            break;
    }
    xSemaphoreGive(MUTEX_reset_factory);
    return result;
}

uint8_t *set_reset_factory(int caller, int value)
{
    if (xSemaphoreTake(MUTEX_reset_factory, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_reset_factory: MUTEX_reset_factory take failed");
        return NULL;
    }

    reset_factory_switch_past = reset_factory_switch;
    reset_factory_switch = value;
    xSemaphoreGive(MUTEX_reset_factory);
    return NULL;
}

uint8_t *__set_reset_factory()
{
    if (xSemaphoreTake(MUTEX_reset_factory, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "reset_factory_switch: MUTEX_reset_factory take failed");
        return NULL;
    }

    if (reset_factory_switch_past != reset_factory_switch)
    {
        ESP_LOGI(TAG, "start to erase nvs storage ...");
        if (reset_factory_switch_past == 1)
        {
            storage_erase();
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_REBOOT, NULL, 0, portMAX_DELAY);
        }
        esp_err_t ret = storage_write(RESET_FACTORY_SK, &reset_factory_switch, sizeof(reset_factory_switch));
        reset_factory_switch_past = reset_factory_switch;
    }
    xSemaphoreGive(MUTEX_reset_factory);
    return 0;
}
/*-----------------------------------------------------time_auto_update-----------------------------------------------*/

int get_time_automatic(int caller)
{
    if (xSemaphoreTake(MUTEX_time_automatic, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_time_automatic: MUTEX_time_automatic take failed");
        return NULL;
    }
    int result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "AT_CMD_CALLER  get_time_automatic");
            result = time_automatic;
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI  get_time_automatic");
            break;
    }
    xSemaphoreGive(MUTEX_time_automatic);
    return result;
}

uint8_t *set_time_automatic(int caller, int value)
{
    if (xSemaphoreTake(MUTEX_time_automatic, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "set_time_automatic: MUTEX_time_automatic take failed");
        return NULL;
    }

    time_automatic_past = time_automatic;
    time_automatic = value;
    xSemaphoreGive(MUTEX_time_automatic);
    return NULL;
}
uint8_t *__set_time_automatic()
{
    if (xSemaphoreTake(MUTEX_time_automatic, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE(TAG, "time_automatic: MUTEX_time_automatic take failed");
        return NULL;
    }

    if (time_automatic_past != time_automatic)
    {
        time_automatic_past = time_automatic;
    }
    xSemaphoreGive(MUTEX_time_automatic);
    return 0;
}

/*------------------------------------------------------sdcard into------------------------------------------------------*/
uint16_t get_spiffs_total_size(int caller)
{
    uint16_t size = 0;
    xSemaphoreTake(MUTEX_sdcard_flash_status, portMAX_DELAY);
    size = g_sdcard_flash_status.spiffs_total_KiB;
    xSemaphoreGive(MUTEX_sdcard_flash_status);

    return size;
}

uint16_t get_spiffs_free_size(int caller)
{
    uint16_t size = 0;
    xSemaphoreTake(MUTEX_sdcard_flash_status, portMAX_DELAY);
    size = g_sdcard_flash_status.spiffs_free_KiB;
    xSemaphoreGive(MUTEX_sdcard_flash_status);

    return size;
}

uint16_t get_sdcard_total_size(int caller)
{
    uint16_t size = 0;
    xSemaphoreTake(MUTEX_sdcard_flash_status, portMAX_DELAY);
    size = g_sdcard_flash_status.sdcard_total_MiB;
    xSemaphoreGive(MUTEX_sdcard_flash_status);

    return size;
}

uint16_t get_sdcard_free_size(int caller)
{
    uint16_t size = 0;
    xSemaphoreTake(MUTEX_sdcard_flash_status, portMAX_DELAY);
    size = g_sdcard_flash_status.sdcard_free_MiB;
    xSemaphoreGive(MUTEX_sdcard_flash_status);

    return size;
}

/*-----------------------------------------------------TASK----------------------------------------------------------*/
void __try_check_sdcard_flash()
{
    size_t total = 0, used = 0;
    uint64_t sdtotal = 0, sdfree = 0;

    if (g_sdcard_flash_status.spiffs_total_KiB == 0)
    {
        // the partition label is hard coded
        esp_spiffs_info("storage", &total, &used);
    }
    if (g_sdcard_flash_status.sdcard_total_MiB == 0)
    {
        esp_vfs_fat_info(DRV_BASE_PATH_SD, &sdtotal, &sdfree);
    }

    xSemaphoreTake(MUTEX_sdcard_flash_status, portMAX_DELAY);
    if (g_sdcard_flash_status.spiffs_total_KiB == 0 && total > 0)
    {
        g_sdcard_flash_status.spiffs_total_KiB = (uint16_t)(total / 1024);
        g_sdcard_flash_status.spiffs_free_KiB = (uint16_t)((total - used) / 1024);
        ESP_LOGI(TAG, "spiffs total %d KiB, free %d KiB", (int)g_sdcard_flash_status.spiffs_total_KiB, (int)g_sdcard_flash_status.spiffs_free_KiB);
    }
    if (g_sdcard_flash_status.sdcard_total_MiB == 0 && sdtotal > 0)
    {
        g_sdcard_flash_status.sdcard_total_MiB = (uint16_t)(sdtotal / 1024 / 1024);
        g_sdcard_flash_status.sdcard_free_MiB = (uint16_t)(sdfree / 1024 / 1024);
        ESP_LOGI(TAG, "sdcard total %d MiB, free %d MiB", (int)g_sdcard_flash_status.sdcard_total_MiB, (int)g_sdcard_flash_status.sdcard_free_MiB);
    }
    xSemaphoreGive(MUTEX_sdcard_flash_status);
}

void app_device_info_task(void *pvParameter)
{
    uint8_t batnow = 0;
    uint32_t cnt = 0;
    bool firstboot_reported = false;
    static uint8_t last_charge_st = 0x66, last_sdcard_inserted = 0x88, sdcard_debounce = 0x99;

    MUTEX_brightness = xSemaphoreCreateMutex();
    MUTEX_SN = xSemaphoreCreateMutex();
    MUTEX_software_version = xSemaphoreCreateMutex();
    MUTEX_himax_software_version = xSemaphoreCreateMutex();
    MUTEX_rgb_switch = xSemaphoreCreateMutex();
    MUTEX_sound = xSemaphoreCreateMutex();
    MUTEX_cloud_service_switch = xSemaphoreCreateMutex();
    MUTEX_ai_service = xSemaphoreCreateMutex();
    MUTEX_reset_factory = xSemaphoreCreateMutex();
    MUTEX_time_automatic = xSemaphoreCreateMutex();
    MUTEX_sdcard_flash_status = xSemaphoreCreateMutex();

    init_sn_from_nvs();
    init_eui_from_nvs();
    init_ai_service_param_from_nvs();
    init_brightness_from_nvs();
    init_rgb_switch_from_nvs();
    init_soud_from_nvs();
    init_cloud_service_switch_from_nvs();
    init_ai_service_param_from_nvs();
    init_reset_factory_switch_from_nvs();
    init_time_automatic_switch_from_nvs();

    g_device_status.battery_per = bsp_battery_get_percent();
    g_device_status.himax_fw_version = tf_module_ai_camera_himax_version_get();

    // get spiffs and sdcard status
    __try_check_sdcard_flash();

    while (1)
    {
        __set_cloud_service_switch();
        __set_brightness();
        __set_rgb_switch();
        __set_sound();
        __set_reset_factory();
        __set_time_automatic();
        //__set_ai_service();
        vTaskDelay(100 / portTICK_PERIOD_MS);
        cnt++;

        if (!firstboot_reported && atomic_load(&g_mqttconn))
        {
            app_sensecraft_mqtt_report_device_status(&g_device_status);
            firstboot_reported = true;
        }

        if ((cnt % 300) == 0)
        {
            batnow = bsp_battery_get_percent();
            if (abs(g_device_status.battery_per - batnow) > 10 || batnow == 0)
            {
                g_device_status.battery_per = batnow;
                // mqtt pub
                if (atomic_load(&g_mqttconn))
                {
                    app_sensecraft_mqtt_report_device_status(&g_device_status);
                }
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BATTERY_ST, &g_device_status, sizeof(struct view_data_device_status), portMAX_DELAY);
            }
            if (batnow == 0)
            {
                ESP_LOGW(TAG, "the battery drop to 0%%, will shutdown to protect the battery and data...");
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BAT_DRAIN_SHUTDOWN, NULL, 0, portMAX_DELAY);
            }
        }

        if ((cnt % 5) == 0)
        {
            uint8_t chg = (uint8_t)(bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0);
            // ESP_LOGD(TAG, "charging: %d", chg);
            if (chg != last_charge_st)
            {
                last_charge_st = chg;
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_CHARGE_ST, &last_charge_st, 1, portMAX_DELAY);
            }
        }

        if ((cnt % 10) == 0)
        {
            uint8_t sdcard_inserted = (uint8_t)bsp_sdcard_is_inserted();
            if (sdcard_inserted == sdcard_debounce)
            {
                if (sdcard_inserted != last_sdcard_inserted)
                {
                    if (sdcard_inserted)
                    {
                        bsp_sdcard_init_default(); // sdcard might be initialized in board_init(), but it's ok
                        __try_check_sdcard_flash();
                    }
                    else
                    {
                        bsp_sdcard_deinit_default();
                        ESP_LOGW(TAG, "SD card is umounted.");
                        xSemaphoreTake(MUTEX_sdcard_flash_status, portMAX_DELAY);
                        g_sdcard_flash_status.sdcard_total_MiB = g_sdcard_flash_status.sdcard_free_MiB = 0;
                        xSemaphoreGive(MUTEX_sdcard_flash_status);
                    }
                    last_sdcard_inserted = sdcard_inserted;
                }
            }
            sdcard_debounce = sdcard_inserted;
        }
    }
}

static void __event_loop_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
        case CTRL_EVENT_MQTT_CONNECTED: {
            ESP_LOGI(TAG, "rcv event: CTRL_EVENT_MQTT_CONNECTED");
            atomic_store(&g_mqttconn, true);
            break;
        }
        default:
            break;
    }
}

void app_device_info_init()
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    const esp_app_desc_t *app_desc = esp_app_get_description();

    // if newer hw_version come up in the future, we can tell it from the EUI
    // for this version firmware, we hard code hw_version as 1.0
    g_device_status.hw_version = "1.0";
    g_device_status.fw_version = app_desc->version;
    g_device_status.battery_per = 100;

    memset(&g_sdcard_flash_status, 0, sizeof(struct view_data_sdcard_flash_status));

    app_device_info_task_stack = (StackType_t *)heap_caps_malloc(10 * 1024 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (app_device_info_task_stack == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for task stack");
        return;
    }

    TaskHandle_t task_handle = xTaskCreateStatic(&app_device_info_task, "app_device_info", APP_DEVICE_INFO_MAX_STACK, NULL, 5, app_device_info_task_stack, &app_device_info_task_buffer);
    if (task_handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create task");
    }

    esp_event_handler_register_with(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_CONNECTED, __event_loop_handler, NULL);
}
