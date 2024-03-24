#include "view_group.h"
#include "ui.h"

static lv_group_t *g_knob_op_group = NULL;

static void sceen_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    if( code == LV_EVENT_FOCUSED) {
        lv_scr_load_anim(obj, LV_SCR_LOAD_ANIM_FADE_ON, 0, 0, false);
    }
}

int view_group_init(void)
{
    g_knob_op_group = lv_group_get_default();
    if(g_knob_op_group == NULL) {
        g_knob_op_group = lv_group_create();
        lv_group_set_default(g_knob_op_group);
    }

    lv_indev_t * cur_drv = NULL;
    for(;;) {
        cur_drv = lv_indev_get_next(cur_drv);
        if(!cur_drv) {
            break;
        }

        if(cur_drv->driver->type == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(cur_drv, g_knob_op_group);
        }
    }
    return 0;
}


int view_group_screen_init(void)
{
    //test
    lv_group_remove_all_objs(g_knob_op_group);

    lv_obj_t * screens[] = {
        ui_screen_preview,
        ui_screen_ha_data,
        ui_screen_ha_ctrl,
        ui_screen_setting,
        ui_screen_shutdown,
    };
    for (size_t i = 0; i < sizeof(screens)/sizeof(screens[0]); i++)
    {
        printf("add screen %d to group\r\n", i);
        lv_group_add_obj(g_knob_op_group, screens[i]);
        lv_obj_add_event_cb(screens[i], sceen_event_cb, LV_EVENT_ALL, NULL);
    }
    return 0;
}