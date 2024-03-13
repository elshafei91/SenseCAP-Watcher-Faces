SET(SOURCES screens/ui_screen_startup.c
    screens/ui_screen_preview.c
    screens/ui_screen_setting.c
    screens/ui_screen_shutdown.c
    screens/ui_screen_shutdown_confirm.c
    screens/ui_screen_shutdown_wait.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    images/ui_img_948016297.c
    images/ui_img_ui_img_setting_png.c
    images/ui_img_ui_img_shutdown_png.c
    images/ui_img_ui_img__cancel_png.c
    images/ui_img_ui_img_confirm_red_png.c)

add_library(ui ${SOURCES})
