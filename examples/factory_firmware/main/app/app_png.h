#ifndef APP_PNG_H
#define APP_PNG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define MAX_IMAGES 10

typedef struct
{
    void *data;
    size_t size;
    uint8_t type_id;
} ImageData;

extern ImageData g_image_store[MAX_IMAGES];
extern int g_image_count;

void *read_png_to_psram(const char *path, size_t *out_size);
void read_and_store_selected_pngs(const char *file_prefix, uint8_t type_id);

#ifdef __cplusplus
}
#endif

#endif // APP_PNG_H
