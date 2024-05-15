#ifndef VIEW_STATUS_BAR_H
#define VIEW_STATUS_BAR_H

#include "event_loops.h"
#include "ui/ui.h"

#ifdef __cplusplus
extern "C" {
#endif


extern lv_obj_t *ui_wifi_status;
extern lv_obj_t *ui_battery_status;

int view_status_bar_init(lv_obj_t *ui_screen);


#ifdef __cplusplus
}
#endif

#endif
