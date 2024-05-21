#include "view_alarm.h"
#include "ui/ui.h"
#include "esp_timer.h"

lv_obj_t *ui_alarm_text;
lv_obj_t *ui_alarm_indicator;

static char alarm_str[128];

static void periodic_timer_callback(void* arg);

static const esp_timer_create_args_t periodic_timer_args = {
            .callback = &periodic_timer_callback,
            /* name is optional, but may help identify the timer when debugging */
            .name = "periodic"
    };
static esp_timer_handle_t periodic_timer;

static void periodic_timer_callback(void* arg);
{
    //
}

static void create_alarm_timer()
{
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
	ESP_ERROR_CHECK(esp_timer_start_once(periodic_timer, 100000));
}

static void delete_alarm_timer()
{
    ESP_ERROR_CHECK(esp_timer_delete(periodic_timer));
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