#ifndef APP_RGB_H
#define APP_RGB_H

#ifdef __cplusplus
extern "C" {
#endif

int app_rgb_init(void);
esp_err_t set_rgb(int caller_type, int service);

#ifdef __cplusplus
}
#endif

#endif
