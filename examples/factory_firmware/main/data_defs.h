#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************************************
 * View Data Defines
*************************************************/

enum start_screen{
    SCREEN_SENSECAP_LOGO,     //startup screen
    SCREEN_CLOUDTASK_PREVIEW,
    SCREEN_HOME,
    SCREEN_WIFI_CONFIG,
};


#define WIFI_SCAN_LIST_SIZE  15


struct view_data_wifi_st
{
    bool   is_connected;
    bool   is_connecting;
    bool   past_connected;
    bool   is_network;  //is connect network
    char   ssid[32];
    int8_t rssi;
    wifi_auth_mode_t authmode;
};


struct view_data_wifi_config
{
    char    ssid[32];
    char    password[64];
    bool    have_password;
};

struct view_data_wifi_item
{
    char   ssid[32];
    bool   auth_mode;
    int8_t rssi;
};

struct view_data_wifi_list
{
    bool  is_connect;
    struct view_data_wifi_item  connect;
    uint16_t cnt;
    struct view_data_wifi_item aps[WIFI_SCAN_LIST_SIZE];
};

struct view_data_wifi_connet_ret_msg 
{
    uint8_t ret; //0:successfull , 1: failure
    char    msg[64];
};

struct view_data_display
{
    int   brightness; //0~100
    bool  sleep_mode_en;       //Turn Off Screen
    int   sleep_mode_time_min;  
};

struct view_data_time_cfg
{
    bool    time_format_24;

    bool    auto_update; //time auto update
    time_t  time;       // use when auto_update is true
    bool    set_time; 

    bool    auto_update_zone;  // use when  auto_update  is true
    int8_t  zone;       // use when auto_update_zone is true
    
    bool    daylight;   // use when auto_update is true  
}__attribute__((packed));


struct view_data_record
{
    uint8_t *p_buf; //record data
    uint32_t len;
};

enum audio_data_type{
    AUDIO_DATA_TYPE_FILE,
    AUDIO_DATA_TYPE_MP3_BUF,
};

struct view_data_audio_play_data
{
    enum audio_data_type type;
    char file_name[64];
    uint8_t *p_buf;
    uint32_t len;
};

struct view_data_mqtt_connect_info
{
    SemaphoreHandle_t mutex;
    char serverUrl[128];
    char token[171];
    int mqttPort;
    int mqttsPort;
    int expiresIn;
};

struct view_data_deviceinfo
{
    char eui[17];
    char key[33];
};

struct view_data_device_status
{
    char *fw_version;
    char *hw_version;
    uint8_t battery_per;
};

struct view_data_setting_volbri
{
    int32_t vs_value;		//volume value
    int32_t bs_value;		//brightness value
};

struct view_data_setting_switch
{
    bool ble_sw;
    bool rgb_sw;
    bool wake_word_sw;
};

#define MAX_PNG_FILES 6

struct view_data_emoticon_display
{
    char file_names[MAX_PNG_FILES][256];
    uint8_t file_count;
};// struct view_data_emoticon_display


//OTA
struct view_data_ota_status
{
    int     status;       //0:succeed, 1:downloading, 2:fail
    int     percentage;   //percentage progress, this is for download, not flash
    int     err_code;     //enum esp_err_t, refer to app_ota.h for detailed error code define
};


/**
 * To better understand the event name, every event name need a suffix "_CHANGED".
 * Mostly, when a data struct changes, there will be an event indicating that some data CHANGED,
 * the UI should render again if it's sensitive to that data.
*/
enum {

    VIEW_EVENT_SCREEN_START = 0,  // uint8_t, enum start_screen, which screen when start

    VIEW_EVENT_TIME,      // bool time_format_24
    VIEW_EVENT_TIME_ZONE,   // int8_t zone
    VIEW_EVENT_BATTERY_ST,// battery changed event

    VIEW_EVENT_WIFI_ST,   // view_data_wifi_st changed event
    VIEW_EVENT_CITY,      // char city[32], max display 24 char
                            //device_info            
    VIEW_EVENT_SN_CODE,
    VIEW_EVENT_BLE_STATUS,
    VIEW_EVENT_SOFTWARE_VERSION_CODE,
    VIEW_EVENT_HIMAX_SOFTWARE_VERSION_CODE,
    VIEW_EVENT_BRIGHTNESS,
    
    VIEW_EVENT_WIFI_LIST,       //view_data_wifi_list_t
    VIEW_EVENT_WIFI_LIST_REQ,   // NULL
    VIEW_EVENT_WIFI_CONNECT,    // struct view_data_wifi_config
    VIEW_EVENT_WIFI_CONNECT_RET,   // struct view_data_wifi_connet_ret_msg
    VIEW_EVENT_WIFI_CFG_DELETE,

    VIEW_EVENT_TIME_CFG_UPDATE,  //  struct view_data_time_cfg
    VIEW_EVENT_TIME_CFG_APPLY,   //  struct view_data_time_cfg

    VIEW_EVENT_DISPLAY_CFG,         // struct view_data_display
    VIEW_EVENT_BRIGHTNESS_UPDATE,   // uint8_t brightness
    VIEW_EVENT_DISPLAY_CFG_APPLY,   // struct view_data_display. will save


    VIEW_EVENT_SHUTDOWN,      //NULL
    VIEW_EVENT_FACTORY_RESET, //NULL
    VIEW_EVENT_SCREEN_CTRL,   // bool  0:disable , 1:enable

    VIEW_EVENT_AUDIO_WAKE, //NULL
    VIEW_EVENT_AUDIO_VAD_TIMEOUT,   //struct view_data_record
    VIEW_EVENT_AUDIO_PALY, //struct view_data_audio_play_data

    VIEW_EVENT_MQTT_CONNECT_INFO,  // struct view_data_mqtt_connect_info

    VIEW_EVENT_ALARM_ON,  // struct tf_module_local_alarm_info
    VIEW_EVENT_ALARM_OFF, //NULL

    VIEW_EVENT_OTA_STATUS,  //struct view_data_ota_status, this is the merged status reporting, e.g. both himax and esp32 ota

    VIEW_EVENT_AI_CAMERA_PREVIEW, // struct tf_module_ai_camera_preview_info (tf_module_ai_camera.h), There can only be one listener
    VIEW_EVENT_AI_CAMERA_SAMPLE,  // NULL
   
    VIEW_EVENT_TASK_FLOW_EXIST, //uint32_t, 1 or 0, tell UI if there's already a tasklist running
    VIEW_EVENT_TASK_FLOW_STOP, //NULL
    VIEW_EVENT_TASK_FLOW_START_BY_LOCAL, //uint32_t, 0: GESTURE, 1: PET, 2: HUMAN
    VIEW_EVENT_ALL,
};
//config caller
enum {
    UI_CALLER,
    AT_CMD_CALLER,
    BLE_CALLER
};
/************************************************
 * Control Data Defines
*************************************************/

// struct ctrl_data_mqtt_tasklist_cjson
// {
//     SemaphoreHandle_t mutex;
//     cJSON *tasklist_cjson;
// };

/**
 * Control Events are used for control logic within the app backend scope.
 * Typically there are two types of control events:
 * - events for notifying a state/data change, e.g. time has been synced
 * - events for start a action/request, e.g. start requesting some resource through HTTP
*/
enum {
    CTRL_EVENT_SNTP_TIME_SYNCED = 0,        //time is synced with sntp server
    CTRL_EVENT_MQTT_CONNECTED,
    CTRL_EVENT_MQTT_OTA_JSON,               //received ota json from MQTT
 
    CTRL_EVENT_TASK_FLOW_START_BY_MQTT, // char * , taskflow json, There can only be one listener
    CTRL_EVENT_TASK_FLOW_START_BY_BLE,  // char * , taskflow json, There can only be one listener
    CTRL_EVENT_TASK_FLOW_START_BY_SR,   // char * , taskflow json, There can only be one listener

    CTRL_EVENT_OTA_AI_MODEL,  //struct view_data_ota_status
    CTRL_EVENT_OTA_ESP32_FW,  //struct view_data_ota_status
    CTRL_EVENT_OTA_HIMAX_FW,  //struct view_data_ota_status

    CTRL_EVENT_ALL,
};


#ifdef __cplusplus
}
#endif
