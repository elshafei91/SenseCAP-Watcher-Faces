#ifndef PM_ANIMA_H
#define PM_ANIMA_H

#include "pm.h"

void main_scroll_cb(lv_event_t* e);
void menu_scroll_cb(lv_event_t* e);
void set_scroll_cb(lv_event_t *e);

void sidelines_Animation( lv_obj_t *TargetObject, int delay);
void secondline_Animation( lv_obj_t *TargetObject, int delay);
void shorttoptobottom_Animation( lv_obj_t *TargetObject, int delay);
void shortbottomtotop_Animation( lv_obj_t *TargetObject, int delay);

void scroll_anim_enable();

#endif
