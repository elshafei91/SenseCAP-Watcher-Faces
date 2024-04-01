#include "view_status_bar.h"

lv_obj_t *ui_wifi_status;
lv_obj_t *ui_battery_status;
int view_status_bar_init(lv_obj_t *ui_screen)
{
    ui_wifi_status = lv_img_create(ui_screen);
    lv_img_set_src(ui_wifi_status, &ui_img_wifi_4_png);
    lv_obj_set_width( ui_wifi_status, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height( ui_wifi_status, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_x( ui_wifi_status, -10 );
    lv_obj_set_y( ui_wifi_status, -100 );
    lv_obj_set_align( ui_wifi_status, LV_ALIGN_CENTER );
    lv_obj_add_flag( ui_wifi_status, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
    lv_obj_clear_flag( ui_wifi_status, LV_OBJ_FLAG_SCROLLABLE );    /// Flags
    lv_obj_add_flag(ui_wifi_status, LV_OBJ_FLAG_HIDDEN);

    ui_battery_status = lv_img_create(ui_screen);
    lv_img_set_src(ui_battery_status, &ui_img_battery_5_png);
    lv_obj_set_width( ui_battery_status, LV_SIZE_CONTENT);  /// 1
    lv_obj_set_height( ui_battery_status, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_x( ui_battery_status, 10 );
    lv_obj_set_y( ui_battery_status, -100 );
    lv_obj_set_align( ui_battery_status, LV_ALIGN_CENTER );
    lv_obj_add_flag( ui_battery_status, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
    lv_obj_clear_flag( ui_battery_status, LV_OBJ_FLAG_SCROLLABLE );    /// Flags
    lv_obj_add_flag(ui_battery_status, LV_OBJ_FLAG_HIDDEN);

    return 0;
}