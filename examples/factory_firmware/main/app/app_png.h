#ifndef APP_PNG_H
#define APP_PNG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "lvgl/lvgl.h"

#define MAX_IMAGES 10


#define MAX_URLS 5
typedef struct
{
    void *data;
    size_t size;
} ImageData;


void *read_png_to_psram(const char *path, size_t *out_size);
void read_and_store_selected_pngs(const char *file_prefix, lv_img_dsc_t **img_dsc_array, int *image_count);



void download_emoji_images(char *name, char *urls[], int url_count);
#ifdef __cplusplus
}
#endif

#endif // APP_PNG_H
