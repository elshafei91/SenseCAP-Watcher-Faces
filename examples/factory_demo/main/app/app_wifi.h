#ifndef APP_WIFI_H
#define APP_WIFI_H

#include "config.h"
#include "view_data.h"
#include "ctrl_data.h"

#ifdef __cplusplus
extern "C" {
#endif

// #define  PING_TEST_IP "192.168.100.1"
 #define  PING_TEST_IP "223.5.5.5"

int app_wifi_init(void);

#ifdef __cplusplus
}
#endif

#endif
