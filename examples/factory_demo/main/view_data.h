#ifndef VIEW_DATA_H
#define VIEW_DATA_H

#include "config.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum start_screen{
    SCREEN_SENSECAP_LOGO, //startup screen
    SCREEN_HOME,
    SCREEN_WIFI_CONFIG,
};


#define WIFI_SCAN_LIST_SIZE  15
#define IMAGE_INVOKED_BOXES  10

struct view_data_wifi_st
{
    bool   is_connected;
    bool   is_connecting;
    bool   is_network;  //is connect network
    char   ssid[32];
    int8_t rssi;
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
};

struct view_data_image_invoke
{
    int boxes_cnt;
    struct view_data_boxes boxes[IMAGE_INVOKED_BOXES]; // todo 使用联合体考虑其他推理类型
    struct view_data_image image;
};


enum {
    VIEW_EVENT_SCREEN_START = 0,  // uint8_t, enum start_screen, which screen when start

    VIEW_EVENT_TIME,  //  bool time_format_24
    
    VIEW_EVENT_BATTERY_ST,   

    VIEW_EVENT_WIFI_ST,   //view_data_wifi_st_t
    VIEW_EVENT_CITY,      // char city[32], max display 24 char

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
    VIEW_EVENT_IMAGE_STOP,  //NULL

    
    VIEW_EVENT_ALARM_OFF,  //NULL
    VIEW_EVENT_ALARM_ON,  // char str[128]

    VIEW_EVENT_ALL,
};


#ifdef __cplusplus
}
#endif

#endif
