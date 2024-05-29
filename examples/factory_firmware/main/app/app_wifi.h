#ifndef APP_WIFI_H
#define APP_WIFI_H

#ifdef __cplusplus
extern "C" {
#endif

// #define  PING_TEST_IP "192.168.100.1"
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "data_defs.h"
 #define  PING_TEST_IP "223.5.5.5"

int app_wifi_init(void);


//wifi config_sys layer Data structure
typedef struct {
    char ssid[32];
    char password[64];
    char* security;
    int caller;
} wifi_config;
typedef struct
{
    char *ssid;
    char *rssi;
    char *encryption;
} WiFiEntry;

typedef struct
{
    WiFiEntry *entries;
    int size;
    int capacity;
} WiFiStack;

extern WiFiStack wifiStack_scanned;
extern WiFiStack wifiStack_connected;
extern int wifi_connect_failed_reason;
extern TaskHandle_t xTask_wifi_config_entry;  

//wifi config_sys layer API


int set_wifi_config(wifi_config* config);
void wifi_scan(void);
//wifi config_sys layer init
void app_wifi_config_layer_init();
void current_wifi_get(wifi_ap_record_t *p_st);

#ifdef __cplusplus
}
#endif

#endif