#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_event.h"
#include "event_loops.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "data_defs.h"
#include "app_device_info.h"
#include "storage.h"
#include "sensecap-watcher.h"
#include "app_rgb.h"
#include "app_audio.h"
#include "audio_player.h"
#include "nvs_flash.h"

#include "mqtt_client.h"

#define SN_TAG                    "SN_TAG"
#define APP_DEVICE_INFO_MAX_STACK 4096
#define SN_STORAGE_SK               "sn"
#define BRIGHTNESS_STORAGE_KEY    "brightness"
#define SOUND_STORAGE_KEY         "sound"
#define RGB_SWITCH_STORAGE_KEY    "rgbswitch"
#define CLOUD_SERVICE_STORAGE_KEY "cloudserviceswitch"
#define AI_SERVICE_STORAGE_KEY    "aiservice"
#define RESET_FACTORY_SK          "resetfactory"

uint8_t SN[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x69 };
uint8_t EUI[] = { 0x1C, 0xF7, 0xF1, 0xC8, 0x62, 0x20, 0x00, 0x09, 0x7A, 0x18, 0x7A, 0xA8, 0xEE, 0x8B, 0x97, 0xFF };
char software_version[] = "1.0.0";
char himax_software_version[] = "1.0.0";
int server_code = 1;
int create_batch = 1000205;

int brightness = 100;
int brightness_past = 100;

int sound_value = 50;
int sound_value_past = 50;

int rgb_switch = 1;
int rgb_switch_past = 1;

int cloud_service_switch = 0;
int cloud_service_switch_past = 0;

int reset_factory_switch = 0;
int reset_factory_switch_past = 0;

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
    app_device_info_task_stack = (StackType_t *)heap_caps_malloc(4096 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
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

void init_sn_from_nvs(){
    size_t size =sizeof(SN);
    esp_err_t ret = storage_read(SN_STORAGE_SK, &SN, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI("NVS", "SN value loaded from NVS: %s", SN);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "No SN value found in NVS. Using default: %s", SN);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading SN from NVS: %s", esp_err_to_name(ret));
    }
}
void init_eui_from_nvs(){
    size_t size =sizeof(EUI);
    esp_err_t ret = storage_read(SN_STORAGE_SK, &EUI, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI("NVS", "EUI value loaded from NVS: %s", EUI);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "No EUI value found in NVS. Using default: %s", EUI);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading SN from NVS: %s", esp_err_to_name(ret));
    }
}

void init_ai_service_param_from_nvs()
{
    size_t size = sizeof(ai_service);
    esp_err_t ret = storage_read(AI_SERVICE_STORAGE_KEY, &ai_service, &size);
    if (ret == ESP_OK)
    {
        ai_service_past = ai_service;
        ESP_LOGI("NVS", "ai_service value loaded from NVS: %d", ai_service);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "No ai_service value found in NVS. Using default: %d", ai_service);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading ai_service from NVS: %s", esp_err_to_name(ret));
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
        ret = bsp_lcd_brightness_set(brightness);
        if (ret != ESP_OK)
        {
            ESP_LOGE("BRIGHTNESS_TAG", "LCD brightness set err:%d", ret);
        }
    }
    else
    {
        ESP_LOGE("NVS", "Error reading brightness from NVS: %s", esp_err_to_name(ret));
    }
}

void init_soud_from_nvs()
{
    size_t size = sizeof(sound_value);
    esp_err_t ret = storage_read(SOUND_STORAGE_KEY, &sound_value, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI("NVS", "Sound value loaded from NVS: %d", sound_value);
        ret = bsp_codec_volume_set(sound_value, NULL);
        if (ret != ESP_OK)
        {
            ESP_LOGE("SOUND_TAG", "sound value set err:%d", ret);
        }
        sound_value_past = sound_value;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "No sound value found in NVS. Using default: %d", sound_value);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading sound value from NVS: %s", esp_err_to_name(ret));
    }
}

void init_cloud_service_switch_from_nvs()
{
    size_t size = sizeof(cloud_service_switch);
    esp_err_t ret = storage_read(CLOUD_SERVICE_STORAGE_KEY, &cloud_service_switch, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI("NVS", "cloud_service_switch value loaded from NVS: %d", cloud_service_switch);
        cloud_service_switch_past = cloud_service_switch;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "No rgb_switch value found in NVS. Using default: %d", cloud_service_switch);
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
        ESP_LOGI("NVS", "reset_factory_switch value loaded from NVS: %d", reset_factory_switch);
        reset_factory_switch_past = reset_factory_switch;
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGI("NVS", "No reset_factory_switch value found in NVS. Using default: %d", reset_factory_switch);
    }
    else
    {
        ESP_LOGE("NVS", "Error reading reset_factory_switch from NVS: %s", esp_err_to_name(ret));
    }
}

/*----------------------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------GET FACTORY cfg----------------------------------------------------------*/

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

uint8_t *get_bt_mac()
{
    const uint8_t *bd_addr = esp_bt_dev_get_address();
    if (bd_addr)
    {
        ESP_LOGI("BT", "Bluetooth MAC Address: %02X:%02X:%02X:%02X:%02X:%02X", bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
    }
    else
    {
        ESP_LOGE("BT", "Failed to get Bluetooth MAC Address");
    }
    return bd_addr;
}
uint8_t *get_sn_code()
{
    return SN;
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
        if (rgb_switch == 1)
        {
            release_rgb(UI_CALLER);
        }
        else
        {
            set_rgb_with_priority(UI_CALLER, off);
        }
        if (ret != ESP_OK)
        {
            ESP_LOGE("rgb_switch_TAG", "cfg write err:%d", ret);
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
        ESP_LOGE("SOUND_TAG", "get_sound: MUTEX_sound take failed");
        return NULL;
    }
    uint8_t *result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI("SOUND_TAG", "AT_CMD_CALLER get sound");
            result = (uint8_t *)&sound_value;
            break;
        case UI_CALLER:
            ESP_LOGI("SOUND_TAG", "UI get sound");
            result = (uint8_t *)&sound_value;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SOUND, result, sizeof(uint8_t *), portMAX_DELAY);
            break;
    }
    xSemaphoreGive(MUTEX_sound);
    return sound_value;
}

uint8_t *set_sound(int caller, int value)
{
    ESP_LOGI("SOUND_TAG", "set_sound");
    if (xSemaphoreTake(MUTEX_sound, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("SOUND_TAG", "set_sound: MUTEX_sound take failed");
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
        ESP_LOGE("SOUND_TAG", "set_sound: MUTEX_sound take failed");
        return NULL;
    }
    if (sound_value_past != sound_value)
    {
        // FILE *fp = fopen("/spiffs/waitPlease.mp3", "r");
        // if (fp)
        // {
        //     audio_player_play(fp);
        // }
        esp_err_t ret = storage_write(SOUND_STORAGE_KEY, &sound_value, sizeof(sound_value));
        if (ret != ESP_OK)
        {
            ESP_LOGE("BRIGHTNESS_TAG", "cfg write err:%d", ret);
            return ret;
        }
        ret = bsp_codec_volume_set(sound_value, NULL);
        if (ret != ESP_OK)
        {
            ESP_LOGE("SOUND_TAG", "sound set err:%d", ret);
            return ret;
        }
    }
    xSemaphoreGive(MUTEX_sound);
    return 0;
}

/*-----------------------------------------------------Claud_service_switch------------------------------------------*/
uint8_t *get_cloud_service_switch(int caller)
{
    if (xSemaphoreTake(MUTEX_cloud_service_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("Claud_service_switch_TAG", "get_Claud_service_switch: MUTEX_Claud_service_switch take failed");
        return NULL;
    }
    uint8_t *result = NULL;
    result = cloud_service_switch;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI("Claud_service_switch_TAG", "AT_CMD_CALLER get Claud_service_switch");
            break;
        case UI_CALLER:
            ESP_LOGI("Claud_service_switch_TAG", "UI get Claud_service_switch");
            break;
    }
    xSemaphoreGive(MUTEX_cloud_service_switch);
    return result;
}

uint8_t *set_cloud_service_switch(int caller, int value)
{
    ESP_LOGI("Claud_service_switch_TAG", "set_cloud_service_switch");
    if (xSemaphoreTake(MUTEX_cloud_service_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("Claud_service_switch_TAG", "set_cloud_service_switch: MUTEX_cloud_service_switch take failed");
        return NULL;
    }
    cloud_service_switch_past = cloud_service_switch;
    cloud_service_switch = value;

    xSemaphoreGive(MUTEX_cloud_service_switch);
    return NULL;
}

static int __set_cloud_service_switch()
{
    if (xSemaphoreTake(MUTEX_cloud_service_switch, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("Claud_service_switch_TAG", "set_rgb_switch: MUTEX_rgb_switch take failed");
        return NULL;
    }
    if (cloud_service_switch_past != cloud_service_switch)
    {
        esp_err_t ret = storage_write(CLOUD_SERVICE_STORAGE_KEY, &cloud_service_switch, sizeof(cloud_service_switch));
        printf("cloud_service_switch: %d\n", cloud_service_switch);
        if (ret != ESP_OK)
        {
            ESP_LOGE("Claud_service_switch_TAG", "cfg write err:%d", ret);
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
        ESP_LOGE("ai_service_TAG", "get_ai_service: MUTEX_ai_service take failed");
        return NULL;
    }
    ai_service_pack *result = NULL;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI("ai_service_TAG", "BLE get ai_service");
            result = &ai_service;
            break;
        case UI_CALLER:
            ESP_LOGI("ai_service_TAG", "UI get ai_service");
            result = &ai_service;
            // esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_AI_SERVICE, result, sizeof(ai_service_pack), portMAX_DELAY);
            break;
    }
    xSemaphoreGive(MUTEX_ai_service);
    return result;
}

void set_ai_service(int caller, ai_service_pack value)
{
    ESP_LOGI("ai_service_TAG", "set_ai_service");
    if (xSemaphoreTake(MUTEX_ai_service, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("ai_service_TAG", "set_ai_service: MUTEX_ai_service take failed");
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
        ESP_LOGE("ai_service_TAG", "set_ai_service: MUTEX_ai_service take failed");
        return -1;
    }
    if (memcmp(&ai_service_past, &ai_service, sizeof(ai_service_pack)) != 0)
    {
        esp_err_t ret = storage_write(AI_SERVICE_STORAGE_KEY, &ai_service, sizeof(ai_service));
        if (ret != ESP_OK)
        {
            ESP_LOGE("ai_service_TAG", "cfg write err:%d", ret);
            xSemaphoreGive(MUTEX_ai_service);
            return ret;
        }
    }
    xSemaphoreGive(MUTEX_ai_service);
    return 0;
}

/*----------------------------------------------------reset-factory--------------------------------------------------------*/

uint8_t *get_reset_factory(int caller)
{
    if (xSemaphoreTake(MUTEX_reset_factory, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("get_reset_factory_TAG", "get_reset_factory: MUTEX_reset_factory take failed");
        return NULL;
    }
    uint8_t *result = NULL;
    result = reset_factory_switch;
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI("get_reset_factory_TAG", "AT_CMD_CALLER  get_reset_factory_TAG");
            break;
        case UI_CALLER:
            ESP_LOGI("get_reset_factory_TAG", "UI  get_reset_factory_TAG");
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_FACTORY_RESET_CODE, reset_factory_switch, sizeof(reset_factory_switch), portMAX_DELAY);
            break;
    }
    xSemaphoreGive(MUTEX_reset_factory);
    return result;
}

uint8_t *set_reset_factory(int caller, int value)
{
    ESP_LOGI("set_reset_factory_TAG", "set_reset_factory");
    if (xSemaphoreTake(MUTEX_reset_factory, portMAX_DELAY) != pdTRUE)
    {
        ESP_LOGE("set_reset_factory_TAG", "set_reset_factory: MUTEX_reset_factory take failed");
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
        ESP_LOGE("set_reset_factory_TAG", "set_rgb_switch: MUTEX_reset_factory take failed");
        return NULL;
    }
    if (reset_factory_switch_past != reset_factory_switch)
    {
        esp_err_t ret = storage_write(RESET_FACTORY_SK, &reset_factory_switch, sizeof(reset_factory_switch));
        nvs_flash_erase();
        printf("set_reset_factory: %d\n", reset_factory_switch);
        if (ret != ESP_OK)
        {
            ESP_LOGE("set_reset_factory_TAG", "cfg write err:%d", ret);
            return ret;
        }
    }
    xSemaphoreGive(MUTEX_reset_factory);
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
    MUTEX_sound = xSemaphoreCreateMutex();
    MUTEX_cloud_service_switch = xSemaphoreCreateMutex();
    MUTEX_ai_service = xSemaphoreCreateMutex();
    MUTEX_reset_factory = xSemaphoreCreateMutex();
    init_ai_service_param_from_nvs();
    init_brightness_from_nvs();
    init_rgb_switch_from_nvs();
    init_soud_from_nvs();
    init_ai_service_param_from_nvs();
    init_reset_factory_switch_from_nvs();
    while (1)
    {
        //__set_cloud_service_switch();
        __set_brightness();
        __set_rgb_switch();
        __set_sound();
        __set_reset_factory();
        //__set_ai_service();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

/*------------------------------------------------------------------------------------------------------------------*/
