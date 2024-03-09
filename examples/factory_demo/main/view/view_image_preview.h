#ifndef VIEW_IMAGE_PREVIEW_H
#define VIEW_IMAGE_PREVIEW_H

#include "config.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

int view_image_preview_init(lv_obj_t *ui_screen);

int view_image_preview_flush(struct view_data_image_invoke *p_invoke);


#ifdef __cplusplus
}
#endif

#endif
