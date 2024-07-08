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
#include "esp_err.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_bt.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "sensecap-watcher.h"

#include "app_device_info.h"
#include "event_loops.h"
#include "data_defs.h"
#include "storage.h"
#include "app_rgb.h"
#include "app_audio.h"
#include "audio_player.h"
#include "app_sensecraft.h"
#include "app_ble.h"
#include "factory_info.h"
#include "tf_module_ai_camera.h"
#include "util.h"


#define APP_DEVICE_INFO_MAX_STACK 4096
#define BRIGHTNESS_STORAGE_KEY    "brightness"
#define SOUND_STORAGE_KEY         "sound"
#define RGB_SWITCH_STORAGE_KEY    "rgbswitch"
#define CLOUD_SERVICE_STORAGE_KEY "cssk"
#define LOCAL_SERVICE_STORAGE_KEY "localservice"
#define USAGE_GUIDE_SK            "usage_guide"
#define BLE_STORAGE_KEY           "ble_switch"

#define EVENT_BIT(T)              (BIT0 << T)
#define EVENT_DEVICECFG_CHANGE    BIT0
#define EVENT_TIMER_500MS         BIT1
#define EVENT_TIMER_1S            BIT2
#define EVENT_TIMER_30S           BIT3

#define GET_DEVCFG_PTR(T)               ((devicecfg_t *)&g_devicecfgs[T])
#define DEVCFG_DEFAULT_BRIGHTNESS       100
#define DEVCFG_DEFAULT_RGB_SWITCH       1
#define DEVCFG_DEFAULT_SOUND            80
#define DEVCFG_DEFAULT_BLE_SWITCH       1
#define DEVCFG_DEFAULT_CLOUD_SVC_SWITCH 1
#define DEVCFG_DEFAULT_LOCAL_SVC_SWITCH 0
#define DEVCFG_DEFAULT_USAGE_GUIDE_FLAG 0

typedef enum {
    DEVCFG_TYPE_BRIGHTNESS = 0,
    DEVCFG_TYPE_RGB_SWITCH,
    DEVCFG_TYPE_SOUND,
    DEVCFG_TYPE_BLE_SWITCH,
    DEVCFG_TYPE_CLOUD_SVC_SWITCH,
    DEVCFG_TYPE_LOCAL_SVC,
    DEVCFG_TYPE_USAGE_GUIDE_FLAG,
    DEVCFG_TYPE_FACTORY_RESET_FLAG,
    DEVCFG_TYPE_MAX,
} devicecfg_type_t;

typedef struct {
    devicecfg_type_t  type;
    SemaphoreHandle_t mutex;
    union {
        int value;
        uint32_t uint_value;
        char *str_value;
    } current, last;
    esp_timer_handle_t timer_handle;
} devicecfg_t;


static const char *TAG = "deviceinfo";

static uint8_t SN[9] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
static uint8_t EUI[8] = { 0 };
static uint8_t DEVCODE[8] = { 0 };
static uint8_t QRCODE[67] = { 0 };

static int server_code = 1;
static int create_batch = 1000205;

static StackType_t *app_device_info_task_stack = NULL;
static StaticTask_t app_device_info_task_buffer;

static struct view_data_device_status g_device_status;
static struct view_data_sdcard_flash_status g_sdcard_flash_status;
static volatile atomic_bool g_mqttconn = ATOMIC_VAR_INIT(false);
static volatile atomic_bool g_timeout_firstreport = ATOMIC_VAR_INIT(false);
static volatile atomic_bool g_will_reset_factory = ATOMIC_VAR_INIT(false);
static SemaphoreHandle_t    g_mtx_sdcard_flash_status;
static EventGroupHandle_t   g_eg_task_wakeup;
static EventGroupHandle_t   g_eg_devicecfg_change;

static esp_timer_handle_t g_timer_firstreport;
static esp_timer_handle_t g_timer_every_500ms;
static esp_timer_handle_t g_timer_every_1s;
static esp_timer_handle_t g_timer_every_30s;

static devicecfg_t *g_devicecfgs;
static sscma_client_info_t *g_himax_info;


void init_sn_from_nvs()
{
    const char *sn_str = factory_info_sn_get();
    if (sn_str == NULL)
    {
        ESP_LOGE(TAG, "Failed to get factory information of SN \n");
        return;
    }
    string_to_byte_array(sn_str, SN, 9);
}

void init_eui_from_nvs()
{
    const char *eui_str = factory_info_eui_get();
    const char *code_str = factory_info_code_get();
    if (eui_str == NULL || code_str == NULL)
    {
        ESP_LOGE(TAG, "Failed to get factory information of EUI and code \n");
        return;
    }

    uint8_t code[8];
    string_to_byte_array(eui_str, EUI, 8);
    string_to_byte_array(code_str, DEVCODE, 8);
}

void init_batchid_from_nvs()
{
    const char *batchid = factory_info_batchid_get();
    if (batchid == NULL) {
        ESP_LOGE(TAG, "Failed to get factory information of batchid \n");
        return;
    }
    create_batch = atoi(batchid);
    return;
}

void init_server_code_from_nvs()
{
    uint8_t platform = factory_info_platform_get();
    server_code = (int)platform;
    return;
}

void init_brightness_from_nvs()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_BRIGHTNESS);
    size_t size = sizeof(cfg->current.value);
    esp_err_t ret = storage_read(BRIGHTNESS_STORAGE_KEY, &cfg->current.value, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Brightness value loaded from NVS: %d", cfg->current.value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        cfg->current.value = DEVCFG_DEFAULT_BRIGHTNESS;
        ESP_LOGI(TAG, "No brightness value found in NVS. Using default: %d", DEVCFG_DEFAULT_BRIGHTNESS);
    }
    else
    {
        cfg->current.value = DEVCFG_DEFAULT_BRIGHTNESS;
        ESP_LOGE(TAG, "Error reading brightness from NVS: %s", esp_err_to_name(ret));
    }

    ret = bsp_lcd_brightness_set(cfg->current.value);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "LCD brightness set err:%d", ret);
    }
    else
    {
        cfg->last.value = cfg->current.value;  // no one shall be accessing this during init, need no lock
    }
}

void init_rgb_switch_from_nvs()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_RGB_SWITCH);
    size_t size = sizeof(cfg->current.value);
    esp_err_t ret = storage_read(RGB_SWITCH_STORAGE_KEY, &cfg->current.value, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "rgb_switch value loaded from NVS: %d", cfg->current.value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        cfg->current.value = DEVCFG_DEFAULT_RGB_SWITCH;
        ESP_LOGW(TAG, "No rgb_switch value found in NVS. Using default: %d", DEVCFG_DEFAULT_RGB_SWITCH);
    }
    else
    {
        cfg->current.value = DEVCFG_DEFAULT_RGB_SWITCH;
        ESP_LOGE(TAG, "Error reading rgb_switch from NVS: %s", esp_err_to_name(ret));
    }

    app_rgb_set(UI_CALLER, cfg->current.value == 1 ? RGB_ON : RGB_OFF);
    cfg->last.value = cfg->current.value;
}

void init_sound_from_nvs()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_SOUND);
    size_t size = sizeof(cfg->current.value);
    esp_err_t ret = storage_read(SOUND_STORAGE_KEY, &cfg->current.value, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Sound value loaded from NVS: %d", cfg->current.value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        cfg->current.value = DEVCFG_DEFAULT_SOUND;
        ESP_LOGW(TAG, "No sound value found in NVS. Using default: %d", DEVCFG_DEFAULT_SOUND);
    }
    else
    {
        cfg->current.value = DEVCFG_DEFAULT_SOUND;
        ESP_LOGE(TAG, "Error reading sound value from NVS: %s", esp_err_to_name(ret));
    }

    ret = bsp_codec_volume_set(cfg->current.value, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "sound value set err:%d", ret);
    } else
    {
        cfg->last.value = cfg->current.value; // no one shall be accessing this during init, need no lock
    }
}

void init_ble_switch_from_nvs()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_BLE_SWITCH);
    size_t size = sizeof(cfg->current.value);
    esp_err_t ret = storage_read(BLE_STORAGE_KEY, &cfg->current.value, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "BLE switch loaded from NVS: %d", cfg->current.value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        cfg->current.value = DEVCFG_DEFAULT_BLE_SWITCH;
        ESP_LOGW(TAG, "No ble switch found in NVS. Using default: %d", DEVCFG_DEFAULT_BLE_SWITCH);
    }
    else
    {
        cfg->current.value = DEVCFG_DEFAULT_BLE_SWITCH;
        ESP_LOGE(TAG, "Error reading ble switch from NVS: %s", esp_err_to_name(ret));
    }

    ret = app_ble_adv_switch((cfg->current.value != 0));
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "BLE switch set err:%d", ret);
    }
    else
    {
        cfg->last.value = cfg->current.value; // no one shall be accessing this during init, need no lock
    }
}

void init_cloud_service_switch_from_nvs()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_CLOUD_SVC_SWITCH);
    size_t size = sizeof(cfg->current.value);
    esp_err_t ret = storage_read(CLOUD_SERVICE_STORAGE_KEY, &cfg->current.value, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "cloud_service_switch value loaded from NVS: %d", cfg->current.value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        cfg->current.value = DEVCFG_DEFAULT_CLOUD_SVC_SWITCH;
        ESP_LOGI(TAG, "No cloud_service_switch value found in NVS. Using default: %d", DEVCFG_DEFAULT_CLOUD_SVC_SWITCH);
    }
    else
    {
        cfg->current.value = DEVCFG_DEFAULT_CLOUD_SVC_SWITCH;
        ESP_LOGE("NVS", "Error reading rgb_switch from NVS: %s", esp_err_to_name(ret));
    }
    cfg->last.value = cfg->current.value;
}

void init_local_service_from_nvs()
{

}

void init_usage_guide_switch_from_nvs()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_USAGE_GUIDE_FLAG);
    size_t size = sizeof(cfg->current.value);
    esp_err_t ret = storage_read(USAGE_GUIDE_SK, &cfg->current.value, &size);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "usage_guide_switch value loaded from NVS: %d", cfg->current.value);
    }
    else if (ret == ESP_ERR_NVS_NOT_FOUND)
    {
        cfg->current.value = DEVCFG_DEFAULT_USAGE_GUIDE_FLAG;
        ESP_LOGI(TAG, "No usage_guide_switch value found in NVS. Using default: %d", DEVCFG_DEFAULT_USAGE_GUIDE_FLAG);
    }
    else
    {
        cfg->current.value = DEVCFG_DEFAULT_USAGE_GUIDE_FLAG;
        ESP_LOGE("NVS", "Error reading usage_guide_switch from NVS: %s", esp_err_to_name(ret));
    }
    cfg->last.value = cfg->current.value;
}

void init_qrcode_content()
{
    char str_platformid[4] = { 0 };
    char str_batchid[20] = { 0 };
    snprintf(str_platformid, sizeof(str_platformid), "%d", server_code);
    snprintf(str_batchid, sizeof(str_batchid), "%d", create_batch);
    char hexStringEUI[19] = { 0 };
    char hexStringCode[19] = { 0 };
    char hexStringSn[19] = { 0 };
    byte_array_to_hex_string(EUI, sizeof(EUI), hexStringEUI);
    byte_array_to_hex_string(DEVCODE, sizeof(DEVCODE), hexStringCode);
    byte_array_to_hex_string(SN, sizeof(SN), hexStringSn);

    snprintf((char *)QRCODE, sizeof(QRCODE), "w1:%s%s:%s:%s:%s", hexStringEUI, hexStringCode, str_platformid, str_batchid, hexStringSn);
    //esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, QRCODE, sizeof(QRCODE), pdMS_TO_TICKS(10000));
}

/*----------------------------------------------------------------------------------------------------------------------*/
/*---------------------------------------------GET FACTORY cfg----------------------------------------------------------*/

uint8_t *get_sn(int caller)
{
    return SN;
}

uint8_t *get_eui()
{
    return EUI;
}

uint8_t *get_qrcode_content()
{
    return QRCODE;
}

uint8_t *get_bt_mac()
{
    const uint8_t *bd_addr = app_ble_get_mac_address();
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

uint8_t *get_wifi_mac()
{
    static uint8_t wifi_mac[6] = { 0 };

    if (wifi_mac[0] == 0 && wifi_mac[1] == 0 && wifi_mac[2] == 0) {
        esp_wifi_get_mac(WIFI_IF_STA, wifi_mac);
        ESP_LOGI(TAG, "WiFi MAC Address: %02X:%02X:%02X:%02X:%02X:%02X", wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4], wifi_mac[5]);
    }
    return wifi_mac;
}

/*---------------------------------------------version module--------------------------------------------------------------*/

char *get_software_version(int caller)
{
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "BLE get software version");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get software version");
            char final_string[150];
            memset(final_string, 0, sizeof(final_string));
            snprintf(final_string, sizeof(final_string), "v%s", g_device_status.fw_version);
            ESP_LOGD(TAG, "Software Version: %s\n", final_string);
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SOFTWARE_VERSION_CODE, 
                              final_string, strlen(final_string) + 1, portMAX_DELAY);
            break;
    }

    return g_device_status.fw_version;
}

char *get_himax_software_version(int caller)
{
    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "BLE get himax software version");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get himax software version");
            char final_string[150];
            memset(final_string, 0, sizeof(final_string));
            snprintf(final_string, sizeof(final_string), "v%s", g_device_status.himax_fw_version);
            ESP_LOGD(TAG, "Himax Software Version: %s\n", final_string);
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_HIMAX_SOFTWARE_VERSION_CODE, 
                              final_string, strlen(final_string) + 1, portMAX_DELAY);
            break;
    }

    return g_device_status.himax_fw_version;
}

/*----------------------------------------------little util func------------------------------------------------------*/
static esp_err_t __safely_set_devicecfg_value(devicecfg_t *cfg, int value)
{
    esp_err_t ret;
    xSemaphoreTake(cfg->mutex, portMAX_DELAY);
    cfg->current.value = value;
    esp_timer_stop(cfg->timer_handle);
    ret = esp_timer_start_once(cfg->timer_handle, 100*1000);
    xSemaphoreGive(cfg->mutex);
    return ret;
}

static esp_err_t __safely_set_devicecfg_uint_value(devicecfg_t *cfg, uint32_t value)
{
    esp_err_t ret;
    xSemaphoreTake(cfg->mutex, portMAX_DELAY);
    cfg->current.uint_value = value;
    esp_timer_stop(cfg->timer_handle);
    ret = esp_timer_start_once(cfg->timer_handle, 100*1000);
    xSemaphoreGive(cfg->mutex);
    return ret;
}

static esp_err_t __safely_set_devicecfg_str_value(devicecfg_t *cfg, char *value)
{
    esp_err_t ret;
    xSemaphoreTake(cfg->mutex, portMAX_DELAY);
    cfg->current.str_value = value;
    esp_timer_stop(cfg->timer_handle);
    ret = esp_timer_start_once(cfg->timer_handle, 100*1000);
    xSemaphoreGive(cfg->mutex);
    return ret;
}

/*----------------------------------------------brightness module------------------------------------------------------*/
int get_brightness(int caller)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_BRIGHTNESS);
    int brightness2 = cfg->current.value;  //copy here

    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "BLE get brightness");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get brightness");
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS, &brightness2, sizeof(int), pdMS_TO_TICKS(10000));
            break;
    }

    return brightness2;
}

esp_err_t set_brightness(int caller, int value)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_BRIGHTNESS);
    ESP_LOGI(TAG, "%s: %d", __func__, value);
    return __safely_set_devicecfg_value(cfg, value);
}

static esp_err_t __set_brightness()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_BRIGHTNESS);
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(cfg->mutex, portMAX_DELAY);
    if (cfg->last.value != cfg->current.value)
    {
        ESP_GOTO_ON_ERROR(storage_write(BRIGHTNESS_STORAGE_KEY, &cfg->current.value, sizeof(cfg->current.value)),
                            set_brightness_err, TAG, "%s cfg write err", __func__);
        ESP_GOTO_ON_ERROR(bsp_lcd_brightness_set(cfg->current.value), set_brightness_err, TAG, "bsp_lcd_brightness_set err");
        cfg->last.value = cfg->current.value;
        ESP_LOGD(TAG, "%s done: %d", __func__, cfg->last.value);
    }
set_brightness_err:
    xSemaphoreGive(cfg->mutex);

    return ret;
}

/*--------------------------------------------rgb switch  module----------------------------------------------------------------*/

int get_rgb_switch(int caller)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_RGB_SWITCH);
    int rgb_switch2 = cfg->current.value;

    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "BLE get rgb_switch");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get rgb_switch");
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_RGB_SWITCH, &rgb_switch2, sizeof(int), pdMS_TO_TICKS(10000));
            break;
    }

    return rgb_switch2;
}

esp_err_t set_rgb_switch(int caller, int value)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_RGB_SWITCH);
    ESP_LOGI(TAG, "%s: %d", __func__, value);
    return __safely_set_devicecfg_value(cfg, value);
}

static esp_err_t __set_rgb_switch()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_RGB_SWITCH);
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(cfg->mutex, portMAX_DELAY);
    if (cfg->last.value != cfg->current.value)
    {
        ESP_GOTO_ON_ERROR(storage_write(RGB_SWITCH_STORAGE_KEY, &cfg->current.value, sizeof(cfg->current.value)),
                            set_rgbswitch_err, TAG, "%s cfg write err", __func__);
        app_rgb_set(UI_CALLER, cfg->current.value == 1 ? RGB_ON : RGB_OFF);
        cfg->last.value = cfg->current.value;
        ESP_LOGD(TAG, "%s done: %d", __func__, cfg->last.value);
    }
set_rgbswitch_err:
    xSemaphoreGive(cfg->mutex);

    return ret;
}

/*-----------------------------------------------------sound_Volume---------------------------------------------------*/

int get_sound(int caller)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_SOUND);
    int sound2 = cfg->current.value;

    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "AT_CMD_CALLER get sound");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get sound");
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SOUND, &sound2, sizeof(int), pdMS_TO_TICKS(10000));
            break;
    }

    return sound2;
}

esp_err_t set_sound(int caller, int value)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_SOUND);
    ESP_LOGI(TAG, "%s: %d", __func__, value);
    return __safely_set_devicecfg_value(cfg, value);
}

static esp_err_t __set_sound()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_SOUND);
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(cfg->mutex, portMAX_DELAY);
    if (cfg->last.value != cfg->current.value)
    {
        ESP_GOTO_ON_ERROR(storage_write(SOUND_STORAGE_KEY, &cfg->current.value, sizeof(cfg->current.value)),
                            set_sound_err, TAG, "%s cfg write err", __func__);
        ESP_GOTO_ON_ERROR(bsp_codec_volume_set(cfg->current.value, NULL), set_sound_err, TAG, "bsp_codec_volume_set err");
        cfg->last.value = cfg->current.value;
        ESP_LOGD(TAG, "%s done: %d", __func__, cfg->last.value);
    }
set_sound_err:
    xSemaphoreGive(cfg->mutex);

    return ret;
}

/*----------------------------------------------------ble switch--------------------------------------------------------*/
int get_ble_switch(int caller)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_BLE_SWITCH);
    int sw = cfg->current.value;

    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "BLE get ble switch");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get ble switch");
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_SWITCH, &sw, sizeof(int), pdMS_TO_TICKS(10000));
            break;
    }

    return sw;
}

esp_err_t set_ble_switch(int caller, int value)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_BLE_SWITCH);
    ESP_LOGI(TAG, "%s: %d", __func__, value);
    return __safely_set_devicecfg_value(cfg, value);
}

static esp_err_t __set_ble_switch()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_BLE_SWITCH);
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(cfg->mutex, portMAX_DELAY);
    if (cfg->last.value != cfg->current.value)
    {
        ESP_GOTO_ON_ERROR(storage_write(BLE_STORAGE_KEY, &cfg->current.value, sizeof(cfg->current.value)),
                            set_ble_err, TAG, "%s cfg write err", __func__);
        ESP_GOTO_ON_ERROR(app_ble_adv_switch((cfg->current.value != 0)), set_ble_err, TAG, "app_ble_adv_switch err");
        cfg->last.value = cfg->current.value;
        ESP_LOGD(TAG, "%s done: %d", __func__, cfg->last.value);
    }
set_ble_err:
    xSemaphoreGive(cfg->mutex);

    return ret;
}

/*-----------------------------------------------------Cloud_service_switch------------------------------------------*/
int get_cloud_service_switch(int caller)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_CLOUD_SVC_SWITCH);
    int cloud_service_switch2 = cfg->current.value;

    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "AT_CMD_CALLER get cloud_service_switch");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI get cloud_service_switch");
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_CLOUD_SERVICE_SWITCH, 
                              &cloud_service_switch2, sizeof(int), portMAX_DELAY);   
            break;
    }

    return cloud_service_switch2;
}

esp_err_t set_cloud_service_switch(int caller, int value)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_CLOUD_SVC_SWITCH);
    ESP_LOGI(TAG, "%s: %d", __func__, value);
    return __safely_set_devicecfg_value(cfg, value);
}

static esp_err_t __set_cloud_service_switch()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_CLOUD_SVC_SWITCH);
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(cfg->mutex, portMAX_DELAY);
    if (cfg->last.value != cfg->current.value)
    {
        ESP_GOTO_ON_ERROR(storage_write(CLOUD_SERVICE_STORAGE_KEY, &cfg->current.value, sizeof(cfg->current.value)),
                            set_cloud_svc_err, TAG, "%s cfg write err", __func__);
        cfg->last.value = cfg->current.value;
        ESP_LOGD(TAG, "%s done: %d", __func__, cfg->last.value);
    }
set_cloud_svc_err:
    xSemaphoreGive(cfg->mutex);

    return ret;
}

/*----------------------------------------------------usage guide switch--------------------------------------------------------*/

int get_usage_guide(int caller)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_USAGE_GUIDE_FLAG);
    int usage_guide_switch2 = cfg->current.value;

    switch (caller)
    {
        case AT_CMD_CALLER:
            ESP_LOGI(TAG, "AT_CMD_CALLER  get_usage_guide");
            break;
        case UI_CALLER:
            ESP_LOGI(TAG, "UI  get_usage_guide");
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_USAGE_GUIDE_SWITCH, 
                              &usage_guide_switch2, sizeof(int), portMAX_DELAY);
            break;
    }

    return usage_guide_switch2;
}

esp_err_t set_usage_guide(int caller, int value)
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_USAGE_GUIDE_FLAG);
    ESP_LOGI(TAG, "%s: %d", __func__, value);
    return __safely_set_devicecfg_value(cfg, value);
}

static esp_err_t __set_usage_guide()
{
    devicecfg_t *cfg = GET_DEVCFG_PTR(DEVCFG_TYPE_USAGE_GUIDE_FLAG);
    esp_err_t ret = ESP_OK;
    xSemaphoreTake(cfg->mutex, portMAX_DELAY);
    if (cfg->last.value != cfg->current.value)
    {
        ESP_GOTO_ON_ERROR(storage_write(USAGE_GUIDE_SK, &cfg->current.value, sizeof(cfg->current.value)),
                            set_usage_guide_err, TAG, "%s cfg write err", __func__);
        cfg->last.value = cfg->current.value;
        ESP_LOGD(TAG, "%s done: %d", __func__, cfg->last.value);
    }
set_usage_guide_err:
    xSemaphoreGive(cfg->mutex);

    return ret;
}


/*----------------------------------------------------reset-factory--------------------------------------------------------*/

esp_err_t set_reset_factory()
{
    atomic_store(&g_will_reset_factory, true);
    return ESP_OK;
}

static esp_err_t __check_reset_factory()
{
    if (atomic_load(&g_will_reset_factory))
    {
        ESP_LOGI(TAG, "###########>>> start to reset factory <<<###########");
        storage_erase();
        storage_file_remove("/spiffs/Custom_greeting1.png");
        storage_file_remove("/spiffs/Custom_greeting2.png");
        storage_file_remove("/spiffs/Custom_greeting3.png");

        storage_file_remove("/spiffs/Custom_detecting1.png");
        storage_file_remove("/spiffs/Custom_detecting2.png");
        storage_file_remove("/spiffs/Custom_detecting3.png");
        storage_file_remove("/spiffs/Custom_detecting4.png");
        storage_file_remove("/spiffs/Custom_detecting5.png");

        storage_file_remove("/spiffs/Custom_detected1.png");
        storage_file_remove("/spiffs/Custom_detected2.png");

        storage_file_remove("/spiffs/Custom_speaking1.png");
        storage_file_remove("/spiffs/Custom_speaking2.png");
        storage_file_remove("/spiffs/Custom_speaking3.png");

        storage_file_remove("/spiffs/Custom_listening1.png");
        storage_file_remove("/spiffs/Custom_listening2.png");

        storage_file_remove("/spiffs/Custom_analyzing1.png");
        storage_file_remove("/spiffs/Custom_analyzing2.png");
        storage_file_remove("/spiffs/Custom_analyzing3.png");
        storage_file_remove("/spiffs/Custom_analyzing4.png");
        storage_file_remove("/spiffs/Custom_analyzing5.png");

        storage_file_remove("/spiffs/Custom_standby1.png");
        storage_file_remove("/spiffs/Custom_standby2.png");
        storage_file_remove("/spiffs/Custom_standby3.png");
        storage_file_remove("/spiffs/Custom_standby4.png");


        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_REBOOT, NULL, 0, pdMS_TO_TICKS(10000));
        atomic_store(&g_will_reset_factory, false);
    }
    return ESP_OK;
}

/*------------------------------------------------------sdcard into------------------------------------------------------*/
uint16_t get_spiffs_total_size(int caller)
{
    uint16_t size = 0;
    xSemaphoreTake(g_mtx_sdcard_flash_status, portMAX_DELAY);
    size = g_sdcard_flash_status.spiffs_total_KiB;
    xSemaphoreGive(g_mtx_sdcard_flash_status);

    return size;
}

uint16_t get_spiffs_free_size(int caller)
{
    uint16_t size = 0;
    xSemaphoreTake(g_mtx_sdcard_flash_status, portMAX_DELAY);
    size = g_sdcard_flash_status.spiffs_free_KiB;
    xSemaphoreGive(g_mtx_sdcard_flash_status);

    return size;
}

uint16_t get_sdcard_total_size(int caller)
{
    uint16_t size = 0;
    xSemaphoreTake(g_mtx_sdcard_flash_status, portMAX_DELAY);
    size = g_sdcard_flash_status.sdcard_total_MiB;
    xSemaphoreGive(g_mtx_sdcard_flash_status);

    return size;
}

uint16_t get_sdcard_free_size(int caller)
{
    uint16_t size = 0;
    xSemaphoreTake(g_mtx_sdcard_flash_status, portMAX_DELAY);
    size = g_sdcard_flash_status.sdcard_free_MiB;
    xSemaphoreGive(g_mtx_sdcard_flash_status);

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

    xSemaphoreTake(g_mtx_sdcard_flash_status, portMAX_DELAY);
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
    xSemaphoreGive(g_mtx_sdcard_flash_status);
}

static void __timer_cb_first_report(void *arg)
{
    atomic_store(&g_timeout_firstreport, true);
}

static void __timer_cb_devicecfg_change_debounce(void *arg)
{
    devicecfg_t *cfg = (devicecfg_t *)arg;
    xEventGroupSetBits(g_eg_devicecfg_change, EVENT_BIT(cfg->type));
    xEventGroupSetBits(g_eg_task_wakeup, EVENT_DEVICECFG_CHANGE);
}

static void __timer_cb_every_500ms(void *arg)
{
    xEventGroupSetBits(g_eg_task_wakeup, EVENT_TIMER_500MS);
}

static void __timer_cb_every_1s(void *arg)
{
    xEventGroupSetBits(g_eg_task_wakeup, EVENT_TIMER_1S);
}

static void __timer_cb_every_30s(void *arg)
{
    xEventGroupSetBits(g_eg_task_wakeup, EVENT_TIMER_30S);
}

void __app_device_info_task(void *pvParameter)
{
    uint8_t batnow = 0;
    uint32_t cnt = 0;
    bool firstboot_reported = false, himax_version_got = false;
    static uint8_t last_charge_st = 0x66, last_sdcard_inserted = 0x88, sdcard_debounce = 0x99;
    static uint8_t last_bat_level_report = 255;
    EventBits_t bits, bits_devicecfg, bits_devicecfg_mask = 0;

    init_brightness_from_nvs();
    init_rgb_switch_from_nvs();
    init_sound_from_nvs();
    init_cloud_service_switch_from_nvs();
    init_ble_switch_from_nvs();

    // get spiffs and sdcard status
    __try_check_sdcard_flash();

    g_device_status.battery_per = bsp_battery_get_percent();

    //post battery level to UI at early time
    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BATTERY_ST, 
                        &g_device_status, sizeof(struct view_data_device_status), portMAX_DELAY);

    for (int i = 0; i < DEVCFG_TYPE_MAX; i++) {
        bits_devicecfg_mask |= EVENT_BIT(i);
    }

    esp_timer_start_periodic(g_timer_every_500ms, 500*1000);
    esp_timer_start_periodic(g_timer_every_1s, 1000000);
    esp_timer_start_periodic(g_timer_every_30s, 30*1000000);

    while (1)
    {
        bits = xEventGroupWaitBits(g_eg_task_wakeup,
                                   EVENT_DEVICECFG_CHANGE | EVENT_TIMER_500MS | EVENT_TIMER_1S | EVENT_TIMER_30S,
                                   pdTRUE, pdFALSE, portMAX_DELAY);
        
        if ((bits & EVENT_DEVICECFG_CHANGE) != 0)
        {
            bits_devicecfg = xEventGroupWaitBits(g_eg_devicecfg_change, bits_devicecfg_mask, pdTRUE, pdFALSE, 0);
            if ((bits_devicecfg & EVENT_BIT(DEVCFG_TYPE_BRIGHTNESS)) != 0) {
                __set_brightness();
            }
            else if ((bits_devicecfg & EVENT_BIT(DEVCFG_TYPE_RGB_SWITCH)) != 0) {
                __set_rgb_switch();
            }
            else if ((bits_devicecfg & EVENT_BIT(DEVCFG_TYPE_SOUND)) != 0) {
                __set_sound();
            }
            else if ((bits_devicecfg & EVENT_BIT(DEVCFG_TYPE_BLE_SWITCH)) != 0) {
                __set_ble_switch();
            }
            else if ((bits_devicecfg & EVENT_BIT(DEVCFG_TYPE_CLOUD_SVC_SWITCH)) != 0) {
                __set_cloud_service_switch();
            }
            else if ((bits_devicecfg & EVENT_BIT(DEVCFG_TYPE_LOCAL_SVC)) != 0) { }
            else if ((bits_devicecfg & EVENT_BIT(DEVCFG_TYPE_USAGE_GUIDE_FLAG)) != 0) {
                __set_usage_guide();
            }
            else if ((bits_devicecfg & EVENT_BIT(DEVCFG_TYPE_FACTORY_RESET_FLAG)) != 0) {
                __check_reset_factory();
            }
        }
        else if ((bits & EVENT_TIMER_500MS) != 0)
        {
            if (!himax_version_got && !atomic_load(&g_timeout_firstreport))
            {
                char *himax_version = tf_module_ai_camera_himax_version_get();

                if (himax_version && strlen(himax_version) > 0)
                {
                    g_device_status.himax_fw_version = himax_version;
                    ESP_LOGI(TAG, "Got Himax fw version: %s", g_device_status.himax_fw_version);
                    himax_version_got = true;
                }
                else
                {
                    ESP_LOGW(TAG, "Failed to get himax info, retrying ...");
                }
            }

            if (!firstboot_reported && atomic_load(&g_timeout_firstreport))
            {
                last_bat_level_report = g_device_status.battery_per;
                app_sensecraft_mqtt_report_device_status(&g_device_status);
                firstboot_reported = true;
            }

            uint8_t chg = (uint8_t)(bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0);
            // ESP_LOGD(TAG, "charging: %d", chg);
            if (chg != last_charge_st)
            {
                last_charge_st = chg;
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_CHARGE_ST, &last_charge_st, 1, pdMS_TO_TICKS(10000));
                if (!chg)
                { // measure the battery immediately when unplug the usb-c charger
                    batnow = bsp_battery_get_percent();
                    if (abs(g_device_status.battery_per - batnow) > 1 || batnow == 0)
                    {
                        g_device_status.battery_per = batnow;
                        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BATTERY_ST, &g_device_status, sizeof(struct view_data_device_status), portMAX_DELAY);
                    }
                }
            }
        }
        else if ((bits & EVENT_TIMER_1S) != 0)
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
                        xSemaphoreTake(g_mtx_sdcard_flash_status, portMAX_DELAY);
                        g_sdcard_flash_status.sdcard_total_MiB = g_sdcard_flash_status.sdcard_free_MiB = 0;
                        xSemaphoreGive(g_mtx_sdcard_flash_status);
                    }
                    last_sdcard_inserted = sdcard_inserted;
                }
            }
            sdcard_debounce = sdcard_inserted;
        }
        else if ((bits & EVENT_TIMER_30S) != 0)
        {
            batnow = bsp_battery_get_percent();
            bool is_charging = (bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0);
            if (abs(g_device_status.battery_per - batnow) > 0 || batnow == 0)
            {
                g_device_status.battery_per = batnow;
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BATTERY_ST, &g_device_status, sizeof(struct view_data_device_status), portMAX_DELAY);
            }
            // mqtt pub
            if (firstboot_reported && (abs(last_bat_level_report - batnow) > 10 || batnow == 0 || (batnow == 100 && abs(last_bat_level_report - batnow) > 0)))
            {
                g_device_status.battery_per = batnow;
                app_sensecraft_mqtt_report_device_status(&g_device_status);
                last_bat_level_report = batnow;
            }
            if (batnow == 0 && !is_charging)
            {
                vTaskDelay(pdMS_TO_TICKS(2000)); // for mqtt pub
                ESP_LOGW(TAG, "the battery drop to 0%%, will shutdown to protect the battery and data...");
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BAT_DRAIN_SHUTDOWN, NULL, 0, pdMS_TO_TICKS(10000));
            }
            // TODO: open this after problem fignured
            // bsp_rtc_set_timer(62);  // feed the watchdog, leave 2sec overhead for iteration cost
        }
    } //while (1)
}

static void __event_loop_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
        case CTRL_EVENT_MQTT_CONNECTED: {
            ESP_LOGI(TAG, "rcv event: CTRL_EVENT_MQTT_CONNECTED");
            atomic_store(&g_mqttconn, true);
            //should report device info no matter himax version is ready or not
            esp_timer_start_once(g_timer_firstreport, 2*1000000);
            break;
        }
        default:
            break;
    }
}

void app_device_info_init_early()
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    // init an array of devicecfg_t
    g_devicecfgs = (devicecfg_t *)psram_calloc(DEVCFG_TYPE_MAX, sizeof(devicecfg_t));
    esp_timer_create_args_t timerargs = {
        .callback = __timer_cb_devicecfg_change_debounce
    };
    for (int i = 0; i < DEVCFG_TYPE_MAX; i++) {
        devicecfg_t *cfg = &g_devicecfgs[i];
        cfg->type = i;
        cfg->mutex = xSemaphoreCreateMutex();
        timerargs.arg = (void *)cfg;
        if (esp_timer_create(&timerargs, &cfg->timer_handle) != ESP_OK) {
            ESP_LOGE(TAG, "can not create debounce timer for devicecfg type %d", i);
        }
    }

    // init critical info
    init_sn_from_nvs();
    init_eui_from_nvs();
    init_batchid_from_nvs();
    init_server_code_from_nvs();
    init_qrcode_content();
    init_usage_guide_switch_from_nvs();

    ESP_LOGI(TAG, "device info init early done, qrcode content: %s", (char *)get_qrcode_content());
}

void app_device_info_init()
{
    memset(&g_device_status, 0, sizeof(struct view_data_device_status));
    const esp_app_desc_t *app_desc = esp_app_get_description();
    // if newer hw_version come up in the future, we can tell it from the EUI
    // for this version firmware, we hard code hw_version as 1.0
    g_device_status.hw_version = "1.0";
    g_device_status.fw_version = app_desc->version;
    g_device_status.battery_per = 100;
    g_device_status.himax_fw_version = "0.0";

    memset(&g_sdcard_flash_status, 0, sizeof(struct view_data_sdcard_flash_status));

    // init semaphores and event groups
    g_mtx_sdcard_flash_status = xSemaphoreCreateMutex();
    g_eg_task_wakeup = xEventGroupCreate();
    g_eg_devicecfg_change = xEventGroupCreate();

    // init task
    const int stack_size = 10 * 1024;
    StackType_t *task_stack = (StackType_t *)psram_calloc(1, stack_size * sizeof(StackType_t));
    StaticTask_t *task_tcb = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
    xTaskCreateStaticPinnedToCore(__app_device_info_task, "app_device_info", stack_size, NULL, 5, task_stack, task_tcb, 1);

    esp_event_handler_register_with(app_event_loop_handle, CTRL_EVENT_BASE, CTRL_EVENT_MQTT_CONNECTED, __event_loop_handler, NULL);

    // init a timer for sending deviceinfo finally, even if fail to get himax info
    esp_timer_create_args_t timerargs = {.callback = __timer_cb_first_report};
    esp_timer_create(&timerargs, &g_timer_firstreport);
    // init periodic timers
    timerargs.callback = __timer_cb_every_500ms;
    esp_timer_create(&timerargs, &g_timer_every_500ms);
    timerargs.callback = __timer_cb_every_1s;
    esp_timer_create(&timerargs, &g_timer_every_1s);
    timerargs.callback = __timer_cb_every_30s;
    esp_timer_create(&timerargs, &g_timer_every_30s);
}
