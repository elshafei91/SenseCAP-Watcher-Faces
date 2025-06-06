// This file was generated by SquareLine Studio
// SquareLine Studio version: SquareLine Studio 1.4.2
// LVGL version: 8.3.6
// Project name: SenseCAP-Watcher

#include "../ui.h"

void ui_Page_Battery_screen_init(void)
{
    ui_Page_Battery = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Page_Battery, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Page_Battery, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Page_Battery, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_batteryimg = lv_img_create(ui_Page_Battery);
    lv_img_set_src(ui_batteryimg, &ui_img_battery_warn_png);
    lv_obj_set_width(ui_batteryimg, LV_SIZE_CONTENT);   /// 102
    lv_obj_set_height(ui_batteryimg, LV_SIZE_CONTENT);    /// 187
    lv_obj_set_align(ui_batteryimg, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_batteryimg, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_batteryimg, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    lv_obj_add_event_cb(ui_Page_Battery, ui_event_Page_Battery, LV_EVENT_ALL, NULL);

}
