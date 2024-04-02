#ifndef APP_TIME_H
#define APP_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

//ntp sync
int app_time_init(void);

// set TZ
int app_time_net_zone_set( char *p);

#ifdef __cplusplus
}
#endif

#endif
