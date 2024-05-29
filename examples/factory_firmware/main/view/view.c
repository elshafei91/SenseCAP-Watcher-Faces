#include "view.h"
#include "view_image_preview.h"
#include "view_alarm.h"
#include "sensecap-watcher.h"

#include "util.h"
#include "ui/ui_helpers.h"
#include <time.h>
#include "app_device_info.h"
#include "ui_manager/pm.h"
#include "ui_manager/animation.h"

#define PNG_IMG_NUMS 30

static const char *TAG = "view";

char sn_data[66];
uint8_t wifi_page_id;
lv_obj_t * view_show_img;
static int PNG_LOADING_COUNT = 0;
extern uint8_t task_down;


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

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    lvgl_port_lock(0);
    if(base == VIEW_EVENT_BASE){
        switch (id)
        {
            case VIEW_EVENT_SCREEN_START: {
                _ui_screen_change(&ui_Page_Vir, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_Vir_screen_init);

            }
            case VIEW_EVENT_PNG_LOADING:{
                PNG_LOADING_COUNT++;
                int progress_percentage = (PNG_LOADING_COUNT * 100) / PNG_IMG_NUMS;
                if(progress_percentage <= 100){
                    lv_arc_set_value(ui_Arc1, progress_percentage);
                    static char load_per[5];
                    sprintf(load_per, "%d%%", progress_percentage);
                    lv_label_set_text(ui_Label5, load_per);
                }
                break;
            }

            case VIEW_EVENT_BATTERY_ST:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_BATTERY_ST");
                struct view_data_device_status * bat_st = (struct view_data_device_status *)event_data;
                ESP_LOGI(TAG, "battery_voltage: %d", bat_st->battery_per);
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
                ESP_LOGI("view", "event: VIEW_EVENT_WIFI_ST");
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

            // case VIEW_EVENT_AI_CAMERA_PREVIEW:{
            //     struct tf_module_ai_camera_preview_info *p_info = ( struct tf_module_ai_camera_preview_info *)event_data;
                // view_image_preview_flush(p_info);
            //     tf_data_image_free(&p_info->img);
                // tf_data_inference_free(&p_info->inference);
            // }

            case VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK");
                _ui_screen_change(&ui_Page_CurTask3, LV_SCR_LOAD_ANIM_FADE_ON, 100, 0, &ui_Page_CurTask3_screen_init);
                break;
            }

            case VIEW_EVENT_TASK_FLOW_STOP:{
                ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_STOP");
                task_down = 1;
                lv_obj_add_flag(ui_viewlivp, LV_OBJ_FLAG_HIDDEN); /// Flags
                lv_obj_add_flag(ui_viewavap, LV_OBJ_FLAG_HIDDEN); /// Flags
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
                }else{
                    ESP_LOGE(TAG, "OTA download failed, error code: %d", ota_st->err_code);
                }
                break;
            }

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

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, 
                                                            __view_event_handler, NULL, NULL)); 
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_PNG_LOADING, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_TIME, 
                                                            __view_event_handler, NULL, NULL)); 

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_SN_CODE, 
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
                                                            VIEW_EVENT_BASE, VIEW_EVENT_ALARM_ON, 
                                                            __view_event_handler, NULL, NULL));   

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_ALARM_OFF, 
                                                            __view_event_handler, NULL, NULL));  
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(app_event_loop_handle, 
                                                            VIEW_EVENT_BASE, VIEW_EVENT_BATTERY_ST, 
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

    return 0;
}