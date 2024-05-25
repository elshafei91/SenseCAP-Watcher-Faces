#ifndef APP_RGB_H
#define APP_RGB_H

#ifdef __cplusplus
extern "C" {
#endif

int app_rgb_init(void);
void set_rgb_with_priority(int caller, int service);
void release_rgb(int caller);

#ifdef __cplusplus
}
#endif

#endif
