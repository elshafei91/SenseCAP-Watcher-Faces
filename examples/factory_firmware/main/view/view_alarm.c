#include "view_alarm.h"
#include "ui/ui.h"

lv_obj_t *ui_alarm_text;
lv_obj_t *ui_alarm_indicator;

static lv_timer_t *alarm_timer;
uint8_t g_alarm_;
uint8_t g_taskend = 0;

static char alarm_str[128];

static void view_alarm_callback(lv_timer_t *timer)
{
    // g_alarm_ = 0;
}

static void create_alarm_timer()
{
    // if (alarm_timer != NULL)
    // {
    //     lv_timer_del(alarm_timer);
    // }
    // alarm_timer = lv_timer_create(view_alarm_callback, 5000, NULL);
}

int view_alarm_init(lv_obj_t *ui_screen)
{
    ui_alarm_indicator = lv_arc_create(ui_screen);
    lv_obj_set_width(ui_alarm_indicator, 412);
    lv_obj_set_height(ui_alarm_indicator, 412);
    lv_obj_set_align(ui_alarm_indicator, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_clear_flag(ui_alarm_indicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_arc_set_value(ui_alarm_indicator, 100);
    lv_arc_set_bg_angles(ui_alarm_indicator, 0, 360);
    lv_obj_set_style_arc_color(ui_alarm_indicator, lv_color_hex(0x4040FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ui_alarm_indicator, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_width(ui_alarm_indicator, 40, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_img_src(ui_alarm_indicator, &ui_img_gradient_png, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_alarm_indicator, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_alarm_indicator, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    return 0;
}

void view_alarm_on(void)
{
    lv_obj_clear_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);     /// Flags
}

void view_alarm_off(void)
{
    lv_obj_add_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);     /// Flags
}