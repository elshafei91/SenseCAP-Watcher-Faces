#ifndef APP_WIFI_H
#define APP_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

// #define  PING_TEST_IP "192.168.100.1"
#include <stdbool.h>

 #define  PING_TEST_IP "223.5.5.5"

int app_wifi_init(void);


//wifi config_sys layer Data structure
typedef struct {
    char* ssid;
    char* password;
    char* security;
    int caller;
} wifi_config;

typedef struct{
    bool is_connected;
    char* ip_address;
    int signal_strength;
}wifi_status;   

//wifi config_sys layer API

void get_wifi_status(int caller);

void set_wifi_config(wifi_config* config);

//wifi config_sys layer init
void app_wifi_config_layer_init();

#ifdef __cplusplus
}
#endif

#endif