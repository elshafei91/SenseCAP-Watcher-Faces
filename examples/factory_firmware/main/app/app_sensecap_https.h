#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// #define SENSECAP_URL "https://sensecap.seeed.cc"
#define SENSECAP_URL "http://intranet-sensecap-env-expose-publicdns.seeed.cc"
#define SENSECAP_PATH_TOKEN_GET "/deviceapi/hardware/iotjoin/requestMqttToken"

int app_sensecap_https_init(void);

#ifdef __cplusplus
}
#endif

