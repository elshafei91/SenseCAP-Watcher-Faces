SET(SOURCES screens/ui_screen_startup.c
    screens/ui_screen_preview.c
    screens/ui_screen_setting.c
    screens/ui_screen_shutdown.c
    screens/ui_screen_shutdown_confirm.c
    screens/ui_screen_shutdown_wait.c
    screens/ui_screen_ha_data.c
    screens/ui_screen_ha_ctrl.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    images/ui_img_seeed_logos_png.c
    images/ui_img_setting_png.c
    images/ui_img_shutdown_png.c
    images/ui_img_cancel_png.c
    images/ui_img_confirm_red_png.c
    images/ui_img_battery_1_png.c
    images/ui_img_battery_2_png.c
    images/ui_img_battery_3_png.c
    images/ui_img_battery_4_png.c
    images/ui_img_battery_5_png.c
    images/ui_img_wifi_0_png.c
    images/ui_img_wifi_1_png.c
    images/ui_img_wifi_2_png.c
    images/ui_img_wifi_3_png.c
    images/ui_img_wifi_4_png.c
    images/ui_img_wifi_nonnet_png.c
    images/ui_img_wifi_disconnect_png.c
    fonts/ui_font_Font1.c)

add_library(ui ${SOURCES})
