#ifndef VIEW_IMAGE_PREVIEW_H
#define VIEW_IMAGE_PREVIEW_H

#include "event_loops.h"
#include "data_defs.h"
#include "lvgl.h"
#include "tf_module_ai_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

int view_image_preview_init(lv_obj_t *ui_screen);

int view_image_preview_flush(struct tf_module_ai_camera_preview_info *p_info);


#ifdef __cplusplus
}
#endif

#endif
