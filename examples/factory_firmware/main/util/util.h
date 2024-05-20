
#ifndef _UTIL_H
#define _UTIL_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

int wifi_rssi_level_get(int rssi);

time_t util_get_timestamp_ms(void);

void *psram_malloc(size_t sz);
void *psram_calloc(size_t n, size_t sz);
void *psram_realloc(void *ptr, size_t new_sz);

#ifdef __cplusplus
}
#endif

#endif
