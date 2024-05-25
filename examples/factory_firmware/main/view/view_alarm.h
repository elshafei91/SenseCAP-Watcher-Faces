#ifndef VIEW_ALARM_H
#define VIEW_ALARM_H

#include "event_loops.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

int view_alarm_init(lv_obj_t *ui_screen);

void view_alarm_on(int alarm_time);

void view_alarm_off(void);


#ifdef __cplusplus
}
#endif

#endif
