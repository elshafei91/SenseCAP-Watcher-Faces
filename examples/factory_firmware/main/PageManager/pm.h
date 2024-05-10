#ifndef PM_H
#define PM_H

#include "lvgl.h"

extern lv_group_t *g_main;
extern lv_indev_t *cur_drv;

extern lv_obj_t *ui_Page_main_group[4];
extern lv_obj_t *ui_Page_template_group[7];
extern lv_obj_t *ui_Page_set_group[12];

extern lv_obj_t *group_view[2];
extern lv_obj_t *group_ha[1];
// extern lv_obj_t *group_dev[5];

typedef struct
{
  lv_obj_t *g_prepage;
  lv_obj_t *g_curpage;
} lv_pm_page_record;

typedef enum
{
  PM_ADD_OBJS_TO_GROUP, // 0: change screen, and add all the objs to group
  PM_NO_OPERATION,      // 1: only change screen without operation
  PM_CLEAR_GROUP        // 2: change screen, and clear all the objs of group
} pm_operation_t;

extern lv_pm_page_record g_page_record;

void lv_pm_init(void);
void lv_pm_open_page(lv_group_t *group, lv_obj_t *page_obj, uint8_t len, pm_operation_t operation,
                     lv_obj_t **target, lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void));
void lv_pm_obj_group(lv_group_t *group, lv_obj_t *page_obj[], uint8_t len);

#endif