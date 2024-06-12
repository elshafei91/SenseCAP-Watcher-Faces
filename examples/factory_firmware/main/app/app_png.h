#ifndef APP_PNG_H
#define APP_PNG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "lvgl/lvgl.h"

#define MAX_IMAGES 10

typedef struct
{
    void *data;
    size_t size;
} ImageData;


void *read_png_to_psram(const char *path, size_t *out_size);
void read_and_store_selected_pngs(const char *prefixes[], int prefix_count, lv_img_dsc_t **img_dsc_array, int *image_count);

#ifdef __cplusplus
}
#endif

#endif // APP_PNG_H
