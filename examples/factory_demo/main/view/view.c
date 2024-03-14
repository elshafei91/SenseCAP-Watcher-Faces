#include "view.h"
#include "view_image_preview.h"
#include "view_alarm.h"
#include "indoor_ai_camera.h"

// #include "app_wifi.h"

// #include "ui.h"
// #include "ui_helpers.h"
// #include "indicator_util.h"

// #include "esp_wifi.h"
// #include <time.h>


static const char *TAG = "view";

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    lvgl_port_lock(0);
    switch (id)
    {
        case VIEW_EVENT_SCREEN_START: {
            uint8_t screen = *( uint8_t *)event_data;
            //todo
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
            // lv_snprintf(buf_h, sizeof(buf_h), "%02d", hour);
            // lv_label_set_text(ui_hour_dis, buf_h);
            // lv_snprintf(buf_m, sizeof(buf_m), "%02d", timeinfo.tm_min);
            // lv_label_set_text(ui_min_dis, buf_m);

            // lv_snprintf(buf, sizeof(buf), "%02d:%02d", hour, timeinfo.tm_min);
            // lv_label_set_text(ui_time2, buf);
            // lv_label_set_text(ui_time3, buf);

            // switch (timeinfo.tm_wday)
            // {
            //     case 0: p_wday_str="Sunday";break;
            //     case 1: p_wday_str="Monday";break;
            //     case 2: p_wday_str="Tuesday";break;
            //     case 3: p_wday_str="Wednesday";break;
            //     case 4: p_wday_str="Thursday";break;
            //     case 5: p_wday_str="Friday";break;
            //     case 6: p_wday_str="Sunday";break;
            //     default: p_wday_str="";break;
            // }
            // char buf1[32];
            // lv_snprintf(buf1, sizeof(buf1), "%s, %02d / %02d / %04d", p_wday_str,  timeinfo.tm_mday, timeinfo.tm_mon+1, timeinfo.tm_year+1900);
            // lv_label_set_text(ui_date, buf1);
            break;
        }

        case VIEW_EVENT_WIFI_ST: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
            struct view_data_wifi_st *p_st = ( struct view_data_wifi_st *)event_data;
            
            // uint8_t *p_src =NULL;
            // //todo is_network
            // if ( p_st->is_connected ) {
            //     switch (wifi_rssi_level_get( p_st->rssi )) {
            //         case 1:
            //             p_src = &ui_img_wifi_1_png;
            //             break;
            //         case 2:
            //             p_src = &ui_img_wifi_2_png;
            //             break;
            //         case 3:
            //             p_src = &ui_img_wifi_3_png;
            //             break;
            //         default:
            //             break;
            //     }
    
            // } else {
            //     p_src = &ui_img_wifi_disconet_png;
            // }

            // lv_img_set_src(ui_wifi_st_1 , (void *)p_src);
            // lv_img_set_src(ui_wifi_st_2 , (void *)p_src);
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
    lvgl_port_lock(0);
    ui_init();
    view_alarm_init(lv_layer_top());
    view_alarm_off();
    view_image_preview_init( ui_screen_preview); 
    lvgl_port_unlock();
    

    int i  = 0;
    for( i = 0; i < VIEW_EVENT_ALL; i++ ) {
        ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, 
                                                                VIEW_EVENT_BASE, i, 
                                                                __view_event_handler, NULL, NULL));
    }
    return 0;
}