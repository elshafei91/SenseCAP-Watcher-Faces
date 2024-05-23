#include "app_png.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

// define global image data store and count variable
ImageData g_detect_store[MAX_IMAGES];
ImageData g_speak_store[MAX_IMAGES];
ImageData g_listen_store[MAX_IMAGES];
ImageData g_load_store[MAX_IMAGES];
ImageData g_sleep_store[MAX_IMAGES];
ImageData g_smile_store[MAX_IMAGES];

int g_detect_image_count = 0;
int g_speak_image_count = 0;
int g_listen_image_count = 0;
int g_load_image_count = 0;
int g_sleep_image_count = 0;
int g_smile_image_count = 0;

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
void read_and_store_selected_pngs(const char *file_prefix, ImageData *imagedata, int *image_count) {
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
                    
                    imagedata[*image_count].data = data;
                    imagedata[*image_count].size = size;
                    (*image_count)++;
                }
            }
        }
        closedir(dir);
    } else {
        ESP_LOGE("SPIFFS", "Failed to open directory /spiffs");
    }
}
