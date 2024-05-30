#include "app_png.h"
#include "esp_log.h"
#include "data_defs.h"
#include "event_loops.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

// // define global image data store and count variable
lv_img_dsc_t *g_detect_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_speak_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_listen_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_load_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_sleep_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_smile_img_dsc[MAX_IMAGES];

int g_detect_image_count = 0;
int g_speak_image_count = 0;
int g_listen_image_count = 0;
int g_load_image_count = 0;
int g_sleep_image_count = 0;
int g_smile_image_count = 0;

void create_img_dsc(lv_img_dsc_t **img_dsc, void *data, size_t size) {
    *img_dsc = (lv_img_dsc_t *)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM);

    if (*img_dsc == NULL) {
        ESP_LOGE("Image DSC", "Failed to allocate memory for image descriptor");
        return;
    }

    (*img_dsc)->header.always_zero = 0;
    (*img_dsc)->header.w = 412;
    (*img_dsc)->header.h = 412;
    (*img_dsc)->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    (*img_dsc)->data_size = size;
    (*img_dsc)->data = data;
}

// Function to read and store PNG files into PSRAM
void* read_png_to_psram(const char *path, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        ESP_LOGE("SPIFFS", "Failed to open file: %s", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *png_buffer = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if (!png_buffer) {
        ESP_LOGE("PSRAM", "Failed to allocate PSRAM for image buffer");
        fclose(file);
        return NULL;
    }

    fread(png_buffer, 1, file_size, file);
    fclose(file);

    *out_size = file_size;
    return png_buffer;
}

// Helper function to check if the file name matches the specified prefix and is a PNG file
static int is_png_file_for_expression(const char* filename, const char* prefix) {
    const char *suffix = ".png";
    size_t len = strlen(filename);
    size_t suffix_len = strlen(suffix);
    if (len > suffix_len && strcmp(filename + len - suffix_len, suffix) == 0) {
        if (strncmp(filename, prefix, strlen(prefix)) == 0) {
            return 1;
        }
    }
    return 0;
}

// Function to read and store selected PNG files based on prefix
void read_and_store_selected_pngs(const char *file_prefix, lv_img_dsc_t **img_dsc_array, int *image_count) {
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("/spiffs")) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (is_png_file_for_expression(ent->d_name, file_prefix)) {
                if (*image_count >= MAX_IMAGES) {
                    ESP_LOGW("PNG Load", "Maximum image storage reached, cannot load more images");
                    break;
                }
                
                size_t size;
                char filepath[256];
                sprintf(filepath, "/spiffs/%s", ent->d_name);
                void *data = read_png_to_psram(filepath, &size);
                if (data) {
                    ESP_LOGI("PNG Load", "Loaded %s into PSRAM", ent->d_name);
                    
                    create_img_dsc(&img_dsc_array[*image_count], data, size);
                    (*image_count)++;
                    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_PNG_LOADING, NULL, NULL, portMAX_DELAY);
                }
            }
        }
        closedir(dir);
    } else {
        ESP_LOGE("SPIFFS", "Failed to open directory /spiffs");
    }
}
