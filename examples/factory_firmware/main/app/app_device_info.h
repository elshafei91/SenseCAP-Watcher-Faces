#ifndef APP_DEVICEINFO_H
#define APP_DEVICEINFO_H
#include "stdint.h"
#include "data_defs.h"

enum { BLE_CONNECTED, BLE_DISCONNECTED, STATUS_WAITTING };

uint8_t *get_sn(int caller);
void app_device_info_init();

char *get_software_version(int caller);
char *get_himax_software_version(int caller);

ai_service_pack *get_ai_service(int caller);

uint8_t *get_cloud_service_switch(int caller);
uint8_t *get_brightness(int caller);
uint8_t *set_brightness(int caller, int value);
uint8_t *get_sound(int caller);
uint8_t *set_sound(int caller, int value);
uint8_t *set_rgb_switch(int caller, int value);
uint8_t *get_rgb_switch(int caller);
uint8_t *get_reset_factory(int caller);
uint8_t *get_sound(int caller);
uint8_t *set_sound(int caller, int value);
uint8_t *get_bt_mac();
uint8_t *get_eui();
uint8_t *get_sn_code();
#endif