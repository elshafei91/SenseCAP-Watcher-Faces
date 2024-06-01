#include "view.h"
#include "view_image_preview.h"
#include "view_alarm.h"
#include "sensecap-watcher.h"

#include "util.h"
#include "ui/ui_helpers.h"
#include <time.h>
#include "app_device_info.h"
#include "app_png.h"
#include "ui_manager/pm.h"
#include "ui_manager/animation.h"

#define PNG_IMG_NUMS 32

static const char *TAG = "view";

char sn_data[66];
uint8_t wifi_page_id;
lv_obj_t * view_show_img;
uint8_t shutdown_state = 0;
static int PNG_LOADING_COUNT = 0;
static uint8_t battery_per = 0;
static bool battery_timer_toggle = 0;
static int battery_flash_count = 0;
extern uint8_t task_down;
extern uint8_t swipe_id; // 0 for shutdown, 1 for factoryreset
extern int first_use;

extern lv_img_dsc_t *g_detect_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_speak_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_listen_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_load_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_sleep_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_smile_img_dsc[MAX_IMAGES];
extern lv_img_dsc_t *g_detected_img_dsc[MAX_IMAGES];

extern int g_detect_image_count;
extern int g_speak_image_count;
extern int g_listen_image_count;
extern int g_load_image_count;
extern int g_sleep_image_count;
extern int g_smile_image_count;
extern int g_detected_image_count;

static void update_ai_ota_progress(int percentage)
{
    lv_arc_set_value(ui_waitarc, percentage);
    char percentage_str[4];
    sprintf(percentage_str, "%d", percentage);
    lv_label_set_text(ui_otatper, percentage_str);
    ESP_LOGI(TAG, "OTA progress updated: %d%%", percentage);
}

static void update_ota_progress(int percentage)
{
    lv_arc_set_value(ui_otaarc, percentage);
}

static void toggle_image_visibility(lv_timer_t *timer)
{
    if(battery_timer_toggle){
        lv_obj_add_flag(ui_Image2, LV_OBJ_FLAG_HIDDEN);
    }else{
        lv_obj_clear_flag(ui_Image2, LV_OBJ_FLAG_HIDDEN);
    }
    battery_timer_toggle = !battery_timer_toggle;

    battery_flash_count++;

    if (battery_flash_count >= 4) {
        lv_timer_del(timer);
        esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SHUTDOWN, NULL, 0, portMAX_DELAY);
    }
}

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    lvgl_port_lock(0);
    if(base == VIEW_EVENT_BASE){
        switch (id)
        {
            case VIEW_EVENT_SCREEN_START: {
                _ui_screen_change(&ui_Page_Vir, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_Vir_screen_init);
                break;
            }

            case VIEW_EVENT_PNG_LOADING:{
                PNG_LOADING_COUNT++;
                int progress_percentage = (PNG_LOADING_COUNT * 100) / PNG_IMG_NUMS;
                if(progress_percentage <= 100){
                    lv_arc_set_value(ui_Arc1, progress_percentage);
                    static char load_per[5];
                    sprintf(load_per, "%d%%", progress_percentage);
                    lv_label_set_text(ui_loadpert, load_per);
                }
                if(progress_percentage>=50)
                {
                    lv_event_send(ui_Page_loading, LV_EVENT_SCREEN_LOADED, NULL);
                }
                break;
            }

            case VIEW_EVENT_FACTORY_RESET_CODE:
            {
                ESP_LOGI(TAG, "event: VIEW_EVENT_FACTORY_RESET_CODE");
                int *reset_st = (int *)event_data;
                first_use = (*reset_st);
                // ESP_LOGI(TAG, "first_use_value : %d", first_use);
                break;
            }

            case VIEW_EVENT_BATTERY_ST:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BATTERY_ST");
                struct view_data_device_status * bat_st = (struct view_data_device_status *)event_data;
                ESP_LOGI(TAG, "battery_percentage: %d", bat_st->battery_per);
                static char load_per[5];
                sprintf(load_per, "%d", bat_st->battery_per);
                lv_label_set_text(ui_btpert, load_per);
                break;
            }

            case VIEW_EVENT_BAT_DRAIN_SHUTDOWN:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BAT_DRAIN_SHUTDOWN");
                lv_timer_t *timer = lv_timer_create(toggle_image_visibility, 500, NULL);
                
                break;
            }

            case VIEW_EVENT_CHARGE_ST:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_CHARGE_ST");
                uint8_t is_charging = *(uint8_t *)event_data;
                ESP_LOGI(TAG, "charging state changed: %d", is_charging);
                lv_obj_add_flag(ui_btpert, LV_OBJ_FLAG_HIDDEN);
                if(is_charging == 0)
                {
                    shutdown_state = 0;
                    if(swipe_id==0)
                    {
                        lv_label_set_text(ui_sptext, "Swipe to reboot");
                    }
                    lv_obj_add_flag(ui_btpert, LV_OBJ_FLAG_HIDDEN);
                    lv_img_set_src(ui_mainb, &ui_img_battery_charging_png);
                }else if(is_charging == 1){
                    shutdown_state = 1;
                    if(swipe_id==0)
                    {
                        lv_label_set_text(ui_sptext, "Swipe to shut down");
                    }
                    lv_obj_clear_flag(ui_btpert, LV_OBJ_FLAG_HIDDEN);
                    lv_img_set_src(ui_mainb, &ui_img_battery_frame_png);
                }
                break;
            } 

            case VIEW_EVENT_TIME: {
                ESP_LOGI(TAG, "event: VIEW_EVENT_TIME");
                bool time_format_24 = true;
                if( event_data) {
                    time_format_24 = *( bool *)event_data;
                } 
                
                time_t now = 0;
                struct tm timeinfo = { 0 };

                time(&now);
                localtime_r(&now, &timeinfo);
                int hour = timeinfo.tm_hour;

                if( ! time_format_24 ) {
                    if( hour>=13 && hour<=23) {
                        hour = hour-12;
                    }
                }
                char buf1[32];
                lv_snprintf(buf1, sizeof(buf1), "%02d:%02d",hour, timeinfo.tm_min);
                lv_label_set_text(ui_maintime, buf1);
                break;
            }

            case VIEW_EVENT_WIFI_ST: {
                ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
                struct view_data_wifi_st *p_st = ( struct view_data_wifi_st *)event_data;
                uint8_t *p_src =NULL;
                if ( p_st->past_connected)
                {
                    wifi_page_id = 1;
                }else{
                    wifi_page_id = 0;
                }
                if ( p_st->is_network ) {
                    switch (wifi_rssi_level_get( p_st->rssi )) {
                        case 1:
                            p_src = &ui_img_wifi_0_png;
                            break;
                        case 2:
                            p_src = &ui_img_wifi_1_png;
                            break;
                        case 3:
                            p_src = &ui_img_wifi_2_png;
                        case 4:
                            p_src = &ui_img_wifi_3_png;
                            break;
                        default:
                            break;
                    }
        
                } else if( p_st->is_connected ) {
                    p_src = &ui_img_no_wifi_png;
                } else {
                    p_src = &ui_img_wifi_abnormal_png;
                }
                lv_img_set_src(ui_mainwifi , (void *)p_src);
                break;
            }

            case VIEW_EVENT_WIFI_CONFIG_SYNC:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CONFIG_SYNC");
                int wifi_config_sync = (int*)event_data;
                ESP_LOGE(TAG,"LOG VIEW_EVENT_WIFI_CONFIG_SYNC  wifi_config_sync is %d",wifi_config_sync);
                if(lv_scr_act() != ui_Page_Wifi)_ui_screen_change(&ui_Page_Wifi, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_Wifi_screen_init);
                // lv_pm_open_page(g_main, NULL, PM_CLEAR_GROUP, &ui_Page_Wifi, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_Wifi_screen_init);
                if(wifi_config_sync == 0)
                {
                    waitForWifi();
                }else if(wifi_config_sync== 1)
                {
                    waitForBinding();
                }else if(wifi_config_sync == 2)
                {
                    waitForAddDev();
                }else if(wifi_config_sync == 3)
                {
                    bindFinish();
                    _ui_screen_change(&ui_Page_Vir, LV_SCR_LOAD_ANIM_FADE_ON, 100, 2000, &ui_Page_Vir_screen_init);
                }else if(wifi_config_sync == 4)
                {
                    wifiConnectFailed();
                }
                break;
            }

            case VIEW_EVENT_SN_CODE:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_SN_CODE");
                const char* _sn_data = (const char*)event_data;
                strncpy(sn_data, _sn_data, 66);
                sn_data[66] = '\0';
                ESP_LOGI(TAG, "Received SN data: %s", _sn_data);
                ESP_LOGI(TAG, "sn_data: %s", sn_data);

                break;
            }

            case VIEW_EVENT_BLE_STATUS:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BLE_STATUS");
                bool ble_connect_status = false;
                ble_connect_status = *(bool *)event_data;
                if(ble_connect_status)
                {
                    lv_obj_set_style_img_recolor(ui_mainble, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                else{
                    lv_obj_set_style_img_recolor(ui_mainble, lv_color_hex(0x171515), LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                break;
            }

            case VIEW_EVENT_BRIGHTNESS:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BRIGHTNESS");
                uint8_t *bri_st = (uint8_t *)event_data;
                int32_t bri_value = (int32_t)(*bri_st);
                lv_slider_set_value(ui_bslider, bri_value, LV_ANIM_OFF);
                                
                break;
            }

            case VIEW_EVENT_RGB_SWITCH:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_RGB_SWITCH");
                int * rgb_st = (int *)event_data;
                if(!(*rgb_st))
                {
                    lv_obj_add_state(ui_setrgbsw, LV_STATE_CHECKED);
                }else{
                    lv_obj_clear_state(ui_setrgbsw, LV_STATE_CHECKED);
                }

                break;
            }

            case VIEW_EVENT_SOUND:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_SOUND");
                uint8_t * vol_st = (uint8_t *)event_data;
                int32_t vol_value = (int32_t)(*vol_st);
                lv_slider_set_value(ui_vslider, (int32_t *)vol_value, LV_ANIM_OFF);
                
                break;
            }

            case VIEW_EVENT_ALARM_ON:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_ALARM_ON");
                struct tf_module_local_alarm_info *alarm_st = (struct tf_module_local_alarm_info *)event_data;
                             
                view_alarm_on(alarm_st);

                break;
            }

            case VIEW_EVENT_ALARM_OFF:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_ALARM_OFF");
                uint8_t * task_st = (uint8_t *)event_data;
                view_alarm_off(task_st);
                break;
            }

            case VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK");
                _ui_screen_change(&ui_Page_CurTask3, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_CurTask3_screen_init);
                break;
            }

            case VIEW_EVENT_TASK_FLOW_STOP:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_STOP");
                task_down = 1;
                lv_obj_add_flag(ui_viewlivp, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui_viewavap, LV_OBJ_FLAG_HIDDEN);
                // event_post_to
                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, &task_down, sizeof(uint8_t), portMAX_DELAY);
                lv_pm_open_page(g_main, &group_page_template, PM_ADD_OBJS_TO_GROUP, &ui_Page_LocTask, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_LocTask_screen_init);
                break;
            }

            case VIEW_EVENT_OTA_STATUS:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_OTA_STATUS");
                _ui_screen_change(&ui_Page_OTA, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_OTA_screen_init);
                struct view_data_ota_status * ota_st = (struct view_data_ota_status *)event_data;
                if(ota_st->status == 0)
                {
                    ESP_LOGI(TAG, "OTA download succeeded");
                }else if (ota_st->status == 1)
                {
                    update_ota_progress(ota_st->percentage);
                    lv_label_set_text(ui_otatext, "Updating\nFirmware");
                }else{
                    ESP_LOGE(TAG, "OTA download failed, error code: %d", ota_st->err_code);
                    lv_label_set_text(ui_otatext, "Update Failed");

                }
                break;
            }

            //Todo
            // case VIEW_EVENT_BAT_DRAIN_SHUTDOWN:{

            //     break;
            // }

            default:
                break;
        }
    }
    else if(base == CTRL_EVENT_BASE){
        switch (id)
        {
            case CTRL_EVENT_OTA_AI_MODEL:{
                ESP_LOGI(TAG, "event: CTRL_EVENT_OTA_AI_MODEL");
                _ui_screen_change(&ui_Page_CurTask2, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_CurTask2_screen_init);
                struct view_data_ota_status * ota_st = (struct view_data_ota_status *)event_data;
                lv_obj_add_flag(ui_otaicon, LV_OBJ_FLAG_HIDDEN);
                if(ota_st->status == 0)
                {
                    ESP_LOGI(TAG, "OTA download succeeded");
                    _ui_screen_change(&ui_Page_ViewAva, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_ViewAva_screen_init);
                }else if (ota_st->status == 1)
                {
                    lv_obj_clear_flag(ui_otaspinner, LV_OBJ_FLAG_HIDDEN);
                    update_ai_ota_progress(ota_st->percentage);
                }else{
                    lv_label_set_text(ui_otatext, "Update Failed");
                    lv_img_set_src(ui_otaicon, &ui_img_error_png);
                    lv_obj_add_flag(ui_otaspinner, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_clear_flag(ui_otaicon, LV_OBJ_FLAG_HIDDEN);
                    ESP_LOGE(TAG, "OTA download failed, error code: %d", ota_st->err_code);
                }
                break;
            }

            default:
                break;
        }
    }
    lvgl_port_unlock();
}


int view_init(void)
{
    lvgl_port_lock(0);
    ui_init();
    lv_pm_init();
    view_alarm_init(lv_layer_top());
    view_image_preview_init(ui_Page_ViewLive);
    lvgl_port_unlock();

    BSP_ERROR_CHECK_RETURN_ERR(bsp_lcd_brightness_set(50));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, 
                                                            __view_event_handler, NULL, NULL)); 
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_PNG_LOADING, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_FACTORY_RESET_CODE, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TIME, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS, 
                                                            __view_event_handler, NULL, NULL));  

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_RGB_SWITCH, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SOUND, 
                                                            __view_event_handler, NULL, NULL)); 
                                                            
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SOFTWARE_VERSION_CODE, 
                                                            __view_event_handler, NULL, NULL));   

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_HIMAX_SOFTWARE_VERSION_CODE, 
                                                            __view_event_handler, NULL, NULL)); 
                                                            
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, 
                                                            __view_event_handler, NULL, NULL));                                                                                                                 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, 
                                                            __view_event_handler, NULL, NULL)); 
                                                            
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONFIG_SYNC, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_ALARM_ON, 
                                                            __view_event_handler, NULL, NULL));   

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, 
                                                            __view_event_handler, NULL, NULL));  
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BATTERY_ST, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BAT_DRAIN_SHUTDOWN, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_CHARGE_ST, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            CTRL_EVENT_BASE, CTRL_EVENT_OTA_AI_MODEL, 
                                                            __view_event_handler, NULL, NULL)); 
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, 
                                                            __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TASK_FLOW_STOP, 
                                                            __view_event_handler, NULL, NULL));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_OTA_STATUS, 
                                                            __view_event_handler, NULL, NULL)); 

    read_and_store_selected_pngs("smiling", g_smile_img_dsc, &g_smile_image_count);
    read_and_store_selected_pngs("detecting", g_detect_img_dsc, &g_detect_image_count);
    read_and_store_selected_pngs("detected", g_detected_img_dsc, &g_detected_image_count);
    read_and_store_selected_pngs("speaking", g_speak_img_dsc, &g_speak_image_count);
    read_and_store_selected_pngs("listening", g_listen_img_dsc, &g_listen_image_count);
    read_and_store_selected_pngs("loading", g_load_img_dsc, &g_load_image_count);
    read_and_store_selected_pngs("sleeping", g_sleep_img_dsc, &g_sleep_image_count);

    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, NULL, 0, portMAX_DELAY);
                    

    return 0;
}

void view_render_black(void)
{
    lvgl_port_lock(0);
    view_image_black_flush();
    lvgl_port_unlock();
}
