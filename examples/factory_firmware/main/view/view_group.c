#include "view_group.h"
#include "ui.h"

static lv_group_t *g_knob_op_group = NULL;

extern lv_obj_t *ui_model_name;

static void sceen_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    // if( code == LV_EVENT_FOCUSED) {
    //     lv_scr_load_anim(obj, LV_SCR_LOAD_ANIM_FADE_ON, 0, 0, false);
    // } else if( obj == ui_screen_preview && code == LV_EVENT_CLICKED) {
        
    //     char *p_name = NULL;
    //     g_model_id++;
    //     if( g_model_id >=4) {
    //         g_model_id = 1;
    //     }

    //     switch (g_model_id)
    //     {
    //     case 1: p_name="Person Detection";break;
    //     case 2: p_name="Apple Detection";break;
    //     case 3: p_name="Gesture Detection";break;
    //     default:
    //         break;
    //     }
    //     lv_label_set_text(ui_model_name, p_name);
    //     up_Animation(ui_model_name, 300);

    //     esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_IMAGE_MODEL, &g_model_id, sizeof(g_model_id), portMAX_DELAY);
    // }
}

int view_group_init(void)
{
    // g_main = lv_group_create();
    // cur_drv = NULL;
    // while((cur_drv = lv_indev_get_next(cur_drv))) {
    //     if(cur_drv->driver->type == LV_INDEV_TYPE_ENCODER) {
    //         lv_indev_set_group(cur_drv, g_main);
    //         break; // 假设只有一个编码器，找到后即停止
    //     }
    // }
    return 0;
}


int view_group_screen_init(void)
{
    //test

    // lv_obj_add_flag(ui_screen_preview, LV_OBJ_FLAG_CLICKABLE);
    
    // lv_group_remove_all_objs(g_knob_op_group);

    // lv_obj_t * screens[] = {
    //     ui_screen_preview,
    //     ui_screen_ha_data,
    //     ui_screen_ha_ctrl,
    //     ui_screen_setting,
    //     ui_screen_shutdown,
    // };
    // for (size_t i = 0; i < sizeof(screens)/sizeof(screens[0]); i++)
    // {
    //     printf("add screen %d to group\r\n", i);
    //     lv_group_add_obj(g_knob_op_group, screens[i]);
    //     lv_obj_add_event_cb(screens[i], sceen_event_cb, LV_EVENT_ALL, NULL);
    // }
    return 0;
}