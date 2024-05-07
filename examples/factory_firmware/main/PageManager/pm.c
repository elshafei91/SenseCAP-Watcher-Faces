#include "pm.h"
#include "animation.h"
#include "ui.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "PM_EVENT";
#define PM_PAGE_PRINTER (1)

lv_pm_page_record g_page_record;
lv_group_t *g_main;
lv_indev_t *cur_drv;
lv_obj_t *ui_Page_main_group[4];
lv_obj_t *ui_Page_template_group[7];
lv_obj_t *ui_Page_set_group[12];
lv_obj_t *group_view[2];
// lv_obj_t *group_dev[5];

void lv_pm_init(void)
{
    g_main = lv_group_create();
    cur_drv = NULL;
    while ((cur_drv = lv_indev_get_next(cur_drv)))
    {
        if (cur_drv->driver->type == LV_INDEV_TYPE_ENCODER)
        {
            lv_indev_set_group(cur_drv, g_main);
            break;
        }
    }

    ui_Page_main_group[0] = ui_mainbtn1;
    ui_Page_main_group[1] = ui_mainbtn2;
    ui_Page_main_group[2] = ui_mainbtn3;
    ui_Page_main_group[3] = ui_mainbtn4;

    ui_Page_template_group[0] = ui_custbtn1;
    ui_Page_template_group[1] = ui_custbtn2;
    ui_Page_template_group[2] = ui_custbtn3;
    ui_Page_template_group[3] = ui_menubtn1;
    ui_Page_template_group[4] = ui_menubtn2;
    ui_Page_template_group[5] = ui_menubtn3;
    ui_Page_template_group[6] = ui_menubtn4;

    ui_Page_set_group[0] = ui_setback;
    ui_Page_set_group[1] = ui_setapp;
    ui_Page_set_group[2] = ui_setwifi;
    ui_Page_set_group[3] = ui_setble;
    ui_Page_set_group[4] = ui_setvol;
    ui_Page_set_group[5] = ui_setbri;
    ui_Page_set_group[6] = ui_setrgb;
    ui_Page_set_group[7] = ui_setww;
    ui_Page_set_group[8] = ui_settime;
    ui_Page_set_group[9] = ui_setdev;
    ui_Page_set_group[10] = ui_setdown;
    ui_Page_set_group[11] = ui_setfac;

    group_view[0] = ui_Page_ViewAva;
    group_view[1] = ui_Page_ViewLive;


    // group_dev[0] = ui_dnt2;
    // group_dev[1] = ui_svt2;
    // group_dev[2] = ui_snt2;
    // group_dev[3] = ui_euit2;
    // group_dev[4] = ui_blet2;


#if PM_PAGE_PRINTER
    lv_obj_set_user_data(ui_Page_Vir, "ui_Page_Virtual");
    lv_obj_set_user_data(ui_Page_main, "ui_Page_main");
    lv_obj_set_user_data(ui_Page_Connect, "ui_Page_Connect");
    lv_obj_set_user_data(ui_Page_nwifi, "ui_Page_nwifi");
    lv_obj_set_user_data(ui_Page_CurTask1, "ui_Page_CurTask1");
    lv_obj_set_user_data(ui_Page_CurTask2, "ui_Page_CurTask2");
    lv_obj_set_user_data(ui_Page_CurTask3, "ui_Page_CurTask3");
    lv_obj_set_user_data(ui_Page_ViewAva, "ui_Page_ViewAva");
    lv_obj_set_user_data(ui_Page_ViewLive, "ui_Page_ViewLive");

    lv_obj_set_user_data(ui_Page_LocTask, "ui_Page_LocTask");
    lv_obj_set_user_data(ui_Page_Set, "ui_Page_Set");
    lv_obj_set_user_data(ui_Page_SAbout, "ui_Page_SAbout");
    lv_obj_set_user_data(ui_Page_HA, "ui_Page_HA");
    lv_obj_set_user_data(ui_Page_Swipe, "ui_Page_Swipe");
    lv_obj_set_user_data(ui_Page_STime, "ui_Page_STime");
    lv_obj_set_user_data(ui_Page_brivol, "ui_Page_brivol");
#endif

    scroll_anim_enable();
}

// lv_pm_open_page(g_main, ui_Page_main_group, 4, PM_ADD_OBJS_TO_GROUP, &ui_Page_main, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_main_screen_init);
void lv_pm_open_page(lv_group_t *group, lv_obj_t *page_obj, uint8_t len, pm_operation_t operation, 
                     lv_obj_t **target, lv_scr_load_anim_t fademode, int spd, int delay, void (*target_init)(void))
{
    if (g_page_record.g_curpage != NULL)
    {
        g_page_record.g_prepage = g_page_record.g_curpage;
    }

    g_page_record.g_curpage = *target;
    if (*target == NULL)
        target_init();
    
    switch (operation) {
        case PM_ADD_OBJS_TO_GROUP:
            if ((group != NULL) && (page_obj != NULL))
                lv_pm_obj_group(group, page_obj, len);
            break;
        case PM_NO_OPERATION:
            break;
        case PM_CLEAR_GROUP:
            if (group != NULL)
                lv_group_remove_all_objs(group);
            break;
    }

    if ((group != NULL) && (page_obj != NULL))
        lv_pm_obj_group(group, page_obj, len);

    lv_scr_load_anim(*target, fademode, spd, 50, false);

#if PM_PAGE_PRINTER
    if(g_page_record.g_prepage)
    {
        const char *prepage_name = lv_obj_get_user_data(g_page_record.g_prepage);
        ESP_LOGI(TAG, "The Previous Page : %s", prepage_name);
    }
    if(g_page_record.g_curpage)
    {
        const char *curpage_name = lv_obj_get_user_data(g_page_record.g_curpage);
        ESP_LOGI(TAG, "The Current  Page : %s", curpage_name);
    }
#endif
}


void lv_pm_obj_group(lv_group_t *group, lv_obj_t *page_obj[], uint8_t len)
{
    lv_group_remove_all_objs(group);
    for (uint8_t index = 0; index < len; index++)
    {
        lv_group_add_obj(group, page_obj[index]);
    }
}