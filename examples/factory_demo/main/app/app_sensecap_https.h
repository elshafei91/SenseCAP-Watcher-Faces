#pragma once

#include "config.h"
#include "view_data.h"
#include "ctrl_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSECAP_URL "https://sensecap.seeed.cn"
#define SENSECAP_PATH_TOKEN_GET "/deviceapi/hardware/iotjoin/requestMqttToken"

int app_sensecap_https_init(void);

#ifdef __cplusplus
}
#endif

