#include "view_alarm.h"
#include "ui/ui.h"
#include "esp_timer.h"
#include "data_defs.h"

lv_obj_t *ui_alarm_text;
lv_obj_t *ui_alarm_indicator;

static int16_t indicator_value = 0;

static void alarm_timer_callback(void *arg);
static const esp_timer_create_args_t alarm_timer_args = { .callback = &alarm_timer_callback, .name = "alarm_on" };
static esp_timer_handle_t alarm_timer;

lv_anim_t a;


static void alarm_timer_callback(void *arg)
{
    view_alarm_off();
}

static void set_angle(void * obj, int32_t v)
{
    lv_arc_set_value(obj, v);
}

static void alarm_timer_start(int s)
{
    if (esp_timer_is_active(alarm_timer))
    {
        esp_timer_stop(alarm_timer);
    }
    ESP_ERROR_CHECK(esp_timer_start_once(alarm_timer, (uint64_t)s * 1000000));
}

int view_alarm_init(lv_obj_t *ui_screen)
{
    ui_alarm_indicator = lv_arc_create(ui_screen);
    lv_obj_set_width(ui_alarm_indicator, 412);
    lv_obj_set_height(ui_alarm_indicator, 412);
    lv_obj_set_align(ui_alarm_indicator, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_alarm_indicator, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_value(ui_alarm_indicator, 0);
    lv_arc_set_rotation(ui_alarm_indicator, 270);
    lv_arc_set_bg_angles(ui_alarm_indicator, 0, 360);

    lv_obj_set_style_arc_color(ui_alarm_indicator, lv_color_hex(0x4040FF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(ui_alarm_indicator, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_arc_width(ui_alarm_indicator, 40, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_img_src(ui_alarm_indicator, &ui_img_gradient_png, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(ui_alarm_indicator, lv_color_hex(0xFFFFFF), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_alarm_indicator, 0, LV_PART_KNOB | LV_STATE_DEFAULT);


    ESP_ERROR_CHECK(esp_timer_create(&alarm_timer_args, &alarm_timer));

    return 0;
}

void view_alarm_on(void)
{
    if(lv_scr_act() != ui_Page_ViewLive)return;
    int alarm_time = 5;
    indicator_value = 0;
    lv_arc_set_value(ui_alarm_indicator, indicator_value);
    alarm_timer_start(alarm_time);
    lv_obj_clear_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
    
    lv_anim_init(&a);
    lv_anim_set_var(&a, ui_alarm_indicator);
    lv_anim_set_exec_cb(&a, set_angle);
    lv_anim_set_time(&a, alarm_time * 1000);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_start(&a);
}

void view_alarm_off(void)
{
    lv_obj_add_flag(ui_alarm_indicator, LV_OBJ_FLAG_HIDDEN);
    // lv_timer_del(alarm_update_timer);
    // ESP_ERROR_CHECK(esp_timer_stop(alarm_timer));
}
