#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_ENABLE_TEST_ENV
#define SENSECAP_URL "http://intranet-sensecap-env-expose-publicdns.seeed.cc"
#else
#define SENSECAP_URL "https://sensecap.seeed.cc"
#endif

#define SENSECAP_PATH_TOKEN_GET "/deviceapi/hardware/iotjoin/requestMqttToken"

int app_sensecap_https_init(void);

#ifdef __cplusplus
}
#endif

