#ifndef APP_RGB_H
#define APP_RGB_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    breath_red,
    breath_green,
    breath_blue,
    breath_white,

    glint_red,
    glint_green,
    glint_blue,
    glint_white,

    flare_red,
    flare_green,
    flare_white,
    flare_blue,
    off,
    on
}rgb_service_t;
int app_rgb_init(void);
void set_rgb_with_priority(int caller, int service);
void release_rgb(int caller);

#ifdef __cplusplus
}
#endif

#endif
