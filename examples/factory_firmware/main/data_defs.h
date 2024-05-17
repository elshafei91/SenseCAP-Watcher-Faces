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

#include "sscma_client_types.h"

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
#define IMAGE_INVOKED_BOXES  10

struct view_data_wifi_st
{
    bool   is_connected;
    bool   is_connecting;
    bool   is_network;  //is network ping-able to internet?
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

struct view_data_wifi_connect_result_msg 
{
    uint8_t result; //0:successfull , 1: failure
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

struct view_data_boxes
{
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint8_t score;
    uint8_t target;
} ;

struct view_data_image
{
    uint8_t *p_buf; //image data
    uint32_t len;
    time_t   time;
    bool     need_free;
};

struct view_data_image_invoke
{
    int boxes_cnt;
    struct view_data_boxes boxes[IMAGE_INVOKED_BOXES]; // todo 使用联合体考虑其他推理类型
    struct view_data_image image;
};

union sscma_client_inference  {
    sscma_client_box_t   *p_box;
    sscma_client_class_t *p_class;
    sscma_client_point_t *p_point;
};


struct view_data_inference
{
    int      type;      //0 box, 1 class; 2 point
    union sscma_client_inference  data;  // 数组首地址, sscma_client_box_t、sscma_client_class_t、sscma_client_point_t
    uint32_t  cnt;  // 个数
    char     *classes;
    bool      need_free;
};

struct view_data_image_inference
{   
    struct view_data_inference  inference;
    struct view_data_image      image;
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

struct view_data_trigger_cfg
{   
    int detect_class;
    int condition_type; // 0-小于，1-小于等于，2-小于，3-大于等于，4-大于，5-不等于
    int condition_num;
};

struct view_data_task
{
    int task_type; // 0: local example task; 1: cloud task 
    int model_id; // 运行模型ID:1,2,3, 不需要运行模型，model id 为0
    int num; // 运行次数, -1表示无限次数
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
};

extern char sn_data[66];

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
    
    VIEW_EVENT_BATTERY_ST,// battery changed event

    VIEW_EVENT_WIFI_ST,   // view_data_wifi_st changed event
    VIEW_EVENT_CITY,      // char city[32], max display 24 char

    VIEW_EVENT_SN_CODE,     // generate ble pairing data
    VIEW_EVENT_BLE_STATUS,  // bool 0:ble_off; 1:ble_on
    VIEW_EVENT_EMOTICON,    // struct view_data_emoticon_display
    
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


    VIEW_EVENT_IMAGE_240_240,  // struct view_data_image_invoke
    VIEW_EVENT_IMAGE_640_480,  // struct view_data_image
    VIEW_EVENT_IMAGE_240_240_REQ,  //NULL
    VIEW_EVENT_IMAGE_640_480_REQ,  //NULL
    VIEW_EVENT_IMAGE_640_480_SEND,  //NULL
    VIEW_EVENT_IMAGE_STOP,  //NULL
    VIEW_EVENT_IMAGE_MODEL, //int


    VIEW_EVENT_TASK_START, //struct view_data_task
    VIEW_EVENT_TASK_STOP,   //struct view_data_task
    VIEW_EVENT_TRIGGER_CFG, //struct view_data_trigger_cfg

    VIEW_EVENT_IMAGE_240_240_1, //struct view_data_image_inference //todo

    VIEW_EVENT_MQTT_CONNECT_INFO,  // struct view_data_mqtt_connect_info

    VIEW_EVENT_ALARM_ON,  //struct view_data_task //todo
    VIEW_EVENT_ALARM_OFF, //NULL

    VIEW_EVENT_TASKLIST_EXIST,        //uint32_t, 1 or 0, tell UI if there's already a tasklist running
    
    VIEW_EVENT_OTA_AI_MODEL,  //struct view_data_ota_status
    VIEW_EVENT_OTA_ESP32_FW,  //struct view_data_ota_status
    VIEW_EVENT_OTA_HIMAX_FW,  //struct view_data_ota_status

    VIEW_EVENT_ALL,
};


/************************************************
 * Control Data Defines
*************************************************/

struct ctrl_data_mqtt_tasklist_cjson
{
    SemaphoreHandle_t mutex;
    cJSON *tasklist_cjson;
};

// this is temp
struct ctrl_data_taskinfo7
{
    SemaphoreHandle_t mutex;
    cJSON *task7;
    bool no_task7;  //if no task 7, imply local warn
};

/**
 * Control Events are used for control logic within the app backend scope.
 * Typically there are two types of control events:
 * - events for notifying a state/data change, e.g. time has been synced
 * - events for start a action/request, e.g. start requesting some resource through HTTP
*/
enum {
    CTRL_EVENT_SNTP_TIME_SYNCED = 0,        //time is synced with sntp server
    CTRL_EVENT_MQTT_CONNECTED,
    CTRL_EVENT_MQTT_TASKLIST_JSON,          //received tasklist json from MQTT
    CTRL_EVENT_BROADCAST_TASK7,             //broadcast info of task7 to all listeners, this is temp
    CTRL_EVENT_ALL,
};


#ifdef __cplusplus
}
#endif
