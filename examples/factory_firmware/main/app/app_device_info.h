#pragma once
#include <stdint.h>
#include <esp_err.h>
#include "data_defs.h"


#define DEVICEINFO_STORAGE  "deviceinfo"

enum { BLE_CONNECTED, BLE_DISCONNECTED, STATUS_WAITTING };


int deviceinfo_get(struct view_data_deviceinfo *p_info);
int deviceinfo_set(struct view_data_deviceinfo *p_info);

uint8_t *get_sn(int caller);
char *get_software_version(int caller);
char *get_himax_software_version(int caller);

ai_service_pack *get_ai_service(int caller);

int get_cloud_service_switch(int caller);
esp_err_t set_cloud_service_switch(int caller, int value);
uint8_t *get_brightness(int caller);
uint8_t *set_brightness(int caller, int value);
uint8_t *get_sound(int caller);
uint8_t *set_sound(int caller, int value);
uint8_t *set_rgb_switch(int caller, int value);
int *get_rgb_switch(int caller);
int *get_reset_factory(int caller);
uint8_t *set_reset_factory(int caller, int value);
uint8_t *get_bt_mac();
uint8_t *get_eui();
uint8_t *get_sn_code();
/**
 * all the following size unit is KiB.
*/
uint16_t get_spiffs_total_size(int caller);
uint16_t get_spiffs_free_size(int caller);
/**
 * all the following size unit is MiB.
*/
uint16_t get_sdcard_total_size(int caller);
uint16_t get_sdcard_free_size(int caller);

void app_device_info_init();

