#ifndef APP_PNG_H
#define APP_PNG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "lvgl/lvgl.h"
#include "cJSON.h"
#include "esp_err.h"

#define MAX_IMAGES 10

typedef struct
{
    void *data;
    size_t size;
} ImageData;


void read_and_store_selected_pngs(const char *primary_prefix, const char *secondary_prefix, lv_img_dsc_t **img_dsc_array, int *image_count);
void check_and_download_files();

typedef enum {
    DOWNLOAD_SUCCESS = 0,
    DOWNLOAD_ERR_ALLOC = -1,
    DOWNLOAD_ERR_HTTP = -2,
    DOWNLOAD_ERR_TIMEOUT = -3,
    DOWNLOAD_ERR_UNKNOWN = -4
} download_status_t;

typedef struct {
    bool success;
    int error_code;
} download_result_t;

typedef struct {
    download_result_t *results;
    int64_t total_time_us;
    double download_speed;
} download_summary_t;

esp_err_t download_emoji_images(download_summary_t *summary, cJSON *filename, cJSON *url_array, int url_count);

#ifdef __cplusplus
}
#endif

#endif // APP_PNG_H
