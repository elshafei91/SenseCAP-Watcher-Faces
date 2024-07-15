#ifndef VIEW_H
#define VIEW_H

#include "data_defs.h"
#include "lvgl.h"
#include "ui/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

void view_ble_switch_timer_start();
void view_sleep_timer_start();

int view_init(void);

void view_render_black(void);

#ifdef __cplusplus
}
#endif

#endif
