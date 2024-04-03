
#ifndef _UTIL_H
#define _UTIL_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

int wifi_rssi_level_get(int rssi);

time_t util_get_timestamp_ms(void);


#ifdef __cplusplus
}
#endif

#endif
