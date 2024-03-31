#include "view.h"
#include "view_image_preview.h"
#include "view_alarm.h"
#include "view_group.h"
#include "view_status_bar.h"
#include "indoor_ai_camera.h"

#include "ui.h"
#include "util.h"
#include <time.h>


static const char *TAG = "view";

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    lvgl_port_lock(0);
    switch (id)
    {
        case VIEW_EVENT_SCREEN_START: {
            uint8_t screen = *( uint8_t *)event_data;
            lv_disp_load_scr( ui_screen_preview);
            
            lv_obj_clear_flag(ui_wifi_status, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_battery_status, LV_OBJ_FLAG_HIDDEN);
        }
        case VIEW_EVENT_TIME: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_TIME");
            bool time_format_24 = true;
            if( event_data) {
                time_format_24 = *( bool *)event_data;
            } 
            
            time_t now = 0;
            struct tm timeinfo = { 0 };
            char *p_wday_str;

            time(&now);
            localtime_r(&now, &timeinfo);
            char buf_h[3];
            char buf_m[3];
            char buf[6];
            int hour = timeinfo.tm_hour;

            if( ! time_format_24 ) {
                if( hour>=13 && hour<=23) {
                    hour = hour-12;
                }
            }
            char buf1[32];
            lv_snprintf(buf1, sizeof(buf1), "%02d/%02d/%04d %02d:%02d",timeinfo.tm_mday, timeinfo.tm_mon+1, timeinfo.tm_year+1900, hour, timeinfo.tm_min);
            lv_label_set_text(ui_time, buf1);
            break;
        }

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
            lv_img_set_src(ui_wifi_status , (void *)p_src);
            break;
        }
        case VIEW_EVENT_ALARM_ON: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_ALARM_ON");
            char *p_data = ( char *)event_data;
            view_alarm_on(p_data);
            
            break;
        }
        case VIEW_EVENT_ALARM_OFF: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_ALARM_OFF");
            view_alarm_off();
            break;
        }
        default:
            break;
    }
    lvgl_port_unlock();
}

int view_init(void)
{
    view_group_init();

    lvgl_port_lock(0);
    ui_init();
    view_alarm_init(lv_layer_top());
    view_alarm_off();
    view_image_preview_init(ui_screen_preview);
    view_status_bar_init(lv_layer_top());
    lvgl_port_unlock();
    

    view_group_screen_init();


    // int i  = 0;
    // for( i = 0; i < VIEW_EVENT_ALL; i++ ) {
    //     ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
    //                                                             VIEW_EVENT_BASE, i, 
    //                                                             __view_event_handler, NULL, NULL));
    // }


    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TIME, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_ALARM_ON, 
                                                            __view_event_handler, NULL, NULL));   

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, 
                                                            __view_event_handler, NULL, NULL));  

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, 
                                                            __view_event_handler, NULL, NULL)); 
    return 0;
}