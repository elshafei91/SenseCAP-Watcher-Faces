#include "view_alarm.h"
#include "ui.h"

lv_obj_t *ui_alarm_text;
lv_obj_t *ui_alarm_indicator;

static lv_timer_t * alarm_timer; 
uint8_t             g_alarm_;

static char alarm_str[128];

static void view_alarm_callback(lv_timer_t * timer)
{
    g_alarm_ = 0;
}

static void create_alarm_timer()
{
    if(alarm_timer != NULL)
    {
        lv_timer_del(alarm_timer);
    }
    alarm_timer = lv_timer_create(view_alarm_callback, 5000, NULL);
}


int view_alarm_init(lv_obj_t *ui_screen)
{
    ui_alarm_indicator = lv_arc_create(ui_screen);
    lv_obj_set_width( ui_alarm_indicator, 240);
    lv_obj_set_height( ui_alarm_indicator, 240);
    lv_obj_set_x( ui_alarm_indicator, 0 );
    lv_obj_set_y( ui_alarm_indicator, 0 );
    lv_obj_set_align( ui_alarm_indicator, LV_ALIGN_CENTER );
    lv_obj_clear_flag( ui_alarm_indicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK | LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_SNAPPABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN );    /// Flags
    lv_obj_set_scrollbar_mode(ui_alarm_indicator, LV_SCROLLBAR_MODE_OFF);
    lv_arc_set_value(ui_alarm_indicator, 360);
    lv_arc_set_bg_angles(ui_alarm_indicator,0,360);

    lv_obj_set_style_arc_color(ui_alarm_indicator, lv_palette_main(LV_PALETTE_RED), LV_PART_INDICATOR | LV_STATE_DEFAULT );
    lv_obj_set_style_arc_opa(ui_alarm_indicator, 255, LV_PART_INDICATOR| LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(ui_alarm_indicator, 10, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_remove_style(ui_alarm_indicator, NULL, LV_PART_KNOB);

    return 0;
}

int view_alarm_on(void)
{
    g_alarm_ = 1;
    g_prepage = ui_previewp2;
    lv_obj_clear_flag(ui_previewp2, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_group_remove_all_objs(g_main);
    lv_group_add_obj(g_main, ui_previewp2);
    _ui_screen_change(&ui_preview_detection, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_preview_detection_screen_init);

    return 0;
}

int view_alarm_off(void)
{
    g_alarm_ = 0;
    lv_obj_add_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_predetp3, LV_OBJ_FLAG_HIDDEN);

    return 0;
}