#include "view.h"
#include "view_image_preview.h"
#include "view_alarm.h"
#include "view_group.h"
#include "view_status_bar.h"
#include "sensecap-watcher.h"

#include "ui.h"
#include "util.h"
#include "ui_helpers.h"
#include <time.h>

#include "pm.h"
#include "animation.h"


static const char *TAG = "view";

char sn_data[17];

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    lvgl_port_lock(0);
    switch (id)
    {
        // case VIEW_EVENT_SCREEN_START: {
        //     uint8_t screen = *( uint8_t *)event_data;
        //     // lv_disp_load_scr( ui_screen_preview);
            
        //     // lv_obj_clear_flag(ui_wifi_status, LV_OBJ_FLAG_HIDDEN);
        //     // lv_obj_clear_flag(ui_battery_status, LV_OBJ_FLAG_HIDDEN);
        // }
        // case VIEW_EVENT_TIME: {
        //     ESP_LOGI(TAG, "event: VIEW_EVENT_TIME");
            // bool time_format_24 = true;
            // if( event_data) {
            //     time_format_24 = *( bool *)event_data;
            // } 
            
            // time_t now = 0;
            // struct tm timeinfo = { 0 };
            // char *p_wday_str;

            // time(&now);
            // localtime_r(&now, &timeinfo);
            // char buf_h[3];
            // char buf_m[3];
            // char buf[6];
            // int hour = timeinfo.tm_hour;

            // if( ! time_format_24 ) {
            //     if( hour>=13 && hour<=23) {
            //         hour = hour-12;
            //     }
            // }
            // char buf1[32];
            // lv_snprintf(buf1, sizeof(buf1), "%02d/%02d/%04d %02d:%02d",timeinfo.tm_mday, timeinfo.tm_mon+1, timeinfo.tm_year+1900, hour, timeinfo.tm_min);
            // lv_label_set_text(ui_maintime, buf1);
        //     break;
        // }

        case VIEW_EVENT_WIFI_ST: {
            ESP_LOGI("view", "event: VIEW_EVENT_WIFI_ST");
            struct view_data_wifi_st *p_st = ( struct view_data_wifi_st *)event_data;
            uint8_t *p_src =NULL;
            if ( p_st->is_network ) {
                switch (wifi_rssi_level_get( p_st->rssi )) {
                    case 1:
                        p_src = &ui_img_wifi_1_png;
                        break;
                    case 2:
                        p_src = &ui_img_wifi_2_png;
                        break;
                    case 3:
                        p_src = &ui_img_wifi_3_png;
                    case 4:
                        p_src = &ui_img_wifi_4_png;
                        break;
                    default:
                        break;
                }
    
            } else if( p_st->is_connected ) {
                p_src = &ui_img_wifi_nonnet_png;
            } else {
                p_src = &ui_img_wifi_disconnect_png;
            }
            lv_img_set_src(ui_mainwifi , (void *)p_src);
            break;
        }
        case VIEW_EVENT_SN_CODE:{
            ESP_LOGI(TAG, "event: VIEW_EVENT_SN_CODE");
            const char** _sn_data = (const char**)event_data;
            strcpy(sn_data, *_sn_data);
            // ESP_LOGI(TAG, "Received SN data: %s", *_sn_data);
            // ESP_LOGI(TAG, "sn_data: %s", sn_data);

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

        default:
            break;
    }
    lvgl_port_unlock();
}

int view_init(void)
{
    lvgl_port_lock(0);
    ui_init();
    lv_pm_init();
    // view_alarm_init(lv_layer_top());
    // view_alarm_off();
    // view_image_preview_init(ui_Page_ViewLive);
    lvgl_port_unlock();
    


    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TIME, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, 
                                                            __view_event_handler, NULL, NULL));   

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, 
                                                            __view_event_handler, NULL, NULL));                                                                                                                 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, 
                                                            __view_event_handler, NULL, NULL)); 

    // ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
    //                                                         VIEW_EVENT_BASE, VIEW_EVENT_ALARM_ON, 
    //                                                         __view_event_handler, NULL, NULL));   

    // ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
    //                                                         VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, 
    //                                                         __view_event_handler, NULL, NULL));  
    
    // ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
    //                                                         VIEW_EVENT_BASE, VIEW_EVENT_TASKLIST_EXIST, 
    //                                                         __view_event_handler, NULL, NULL));  

    return 0;
}