#include "system_layer.h"

#include "app_wifi.h"
#include "app_device_info.h"

void system_layer_init(){
    app_device_info_init();
    app_wifi_config_layer_init();
}