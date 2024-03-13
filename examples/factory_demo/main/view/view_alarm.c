#include "view_alarm.h"

lv_obj_t *ui_alarm_text;
lv_obj_t *ui_alarm_indicator;

static char alarm_str[128];

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

    ui_alarm_text = lv_label_create(ui_screen);
    lv_label_set_long_mode(ui_alarm_text, LV_LABEL_LONG_SCROLL_CIRCULAR);     /*Circular scroll*/
    lv_obj_set_width(ui_alarm_text, 200);
    // lv_label_set_text(ui_alarm_text, "warning...");
    lv_obj_align(ui_alarm_text, LV_ALIGN_CENTER, 0, 0);
    // lv_obj_set_style_border_color(ui_alarm_text, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_text_color(ui_alarm_text, lv_palette_main(LV_PALETTE_RED), 0);

    return 0;
}

int view_alarm_on(char * str)
{
    lv_snprintf(alarm_str,sizeof(alarm_str)-1, "%s", str);
    lv_label_set_text(ui_alarm_text, alarm_str);
    lv_obj_clear_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_alarm_text, LV_OBJ_FLAG_HIDDEN);

    return 0;
}

int view_alarm_off(void)
{
    lv_obj_add_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_alarm_text, LV_OBJ_FLAG_HIDDEN);

    return 0;
}