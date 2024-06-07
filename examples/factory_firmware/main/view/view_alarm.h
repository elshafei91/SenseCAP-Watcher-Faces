#ifndef VIEW_ALARM_H
#define VIEW_ALARM_H

#include "event_loops.h"
#include "lvgl.h"
#include "view_image_preview.h"

#include "tf_module_local_alarm.h"
#include "tf_module_util.h"

#ifdef __cplusplus
extern "C" {
#endif

int view_alarm_init(lv_obj_t *ui_screen);

int view_alarm_on(struct tf_module_local_alarm_info *alarm_st);

void view_alarm_off(uint8_t task_down);

void view_task_error_init();


#ifdef __cplusplus
}
#endif

#endif
