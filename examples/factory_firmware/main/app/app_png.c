#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/param.h>

#include "esp_log.h"
#include "data_defs.h"
#include "event_loops.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_timer.h"
#include "esp_check.h"

#include "storage.h"
#include "app_png.h"
#include "util/util.h"

#define TAG              "HTTP_EMOJI"
#define HTTP_MOUNT_POINT "/sdcard"

#define EMOJI_HTTP_TIMEOUT_MS           30000
#define EMOJI_HTTP_DOWNLOAD_RETRY_TIMES 5
#define EMOJI_HTTP_RX_CHUNK_SIZE        512

// // define global image data store and count variable
lv_img_dsc_t *g_detect_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_speak_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_listen_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_load_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_sleep_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_smile_img_dsc[MAX_IMAGES];
lv_img_dsc_t *g_detected_img_dsc[MAX_IMAGES];

int g_detect_image_count = 0;
int g_speak_image_count = 0;
int g_listen_image_count = 0;
int g_load_image_count = 0;
int g_sleep_image_count = 0;
int g_smile_image_count = 0;
int g_detected_image_count = 0;

void create_img_dsc(lv_img_dsc_t **img_dsc, void *data, size_t size)
{
    *img_dsc = (lv_img_dsc_t *)heap_caps_malloc(sizeof(lv_img_dsc_t), MALLOC_CAP_SPIRAM);

    if (*img_dsc == NULL)
    {
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
void *read_png_to_psram(const char *path, size_t *out_size)
{
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        ESP_LOGE("SPIFFS", "Failed to open file: %s", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *png_buffer = heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
    if (!png_buffer)
    {
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
static int is_png_file_for_expression(const char *filename, const char *prefix)
{
    const char *suffix = ".png";
    size_t len = strlen(filename);
    size_t suffix_len = strlen(suffix);
    if (len > suffix_len && strcmp(filename + len - suffix_len, suffix) == 0)
    {
        if (strncmp(filename, prefix, strlen(prefix)) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Function to read and store selected PNG files based on prefix
void read_and_store_selected_pngs(const char *file_prefix, lv_img_dsc_t **img_dsc_array, int *image_count)
{
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("/spiffs")) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            if (is_png_file_for_expression(ent->d_name, file_prefix))
            {
                if (*image_count >= MAX_IMAGES)
                {
                    ESP_LOGW("PNG Load", "Maximum image storage reached, cannot load more images");
                    break;
                }

                size_t size;
                char filepath[256];
                sprintf(filepath, "/spiffs/%s", ent->d_name);
                void *data = read_png_to_psram(filepath, &size);
                if (data)
                {
                    ESP_LOGI("PNG Load", "Loaded %s into PSRAM", ent->d_name);

                    create_img_dsc(&img_dsc_array[*image_count], data, size);
                    (*image_count)++;
                    esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_PNG_LOADING, NULL, NULL, portMAX_DELAY);
                }
            }
        }
        closedir(dir);
    }
    else
    {
        ESP_LOGE("SPIFFS", "Failed to open directory /spiffs");
    }
}

static char *emoji_name = NULL;
#define MAX_RETRY_COUNT 5
#define MAX_URLS        5

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static FILE *f = NULL;
    static char* file_path = NULL;
    static int64_t file_start_time = 0; 

    switch (evt->event_id)
    {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            file_start_time = esp_timer_get_time(); 
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0)
            {
                if (!f)
                {
                    asprintf(&file_path, "%s/%s.png", HTTP_MOUNT_POINT, emoji_name);
                    f = fopen(file_path, "wb");
                    if (!f)
                    {
                        ESP_LOGE(TAG, "Failed to open file for writing");
                        return ESP_FAIL;
                    }
                }
                fwrite(evt->data, 1, evt->data_len, f);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if (f)
            {
                fclose(f);
                f = NULL;
            }
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

// after use free name
void download_emoji_images(char *base_name, char *urls[], int url_count)
{
    ESP_LOGI(TAG, "Starting emoji HTTP download, base file name = %s ...", base_name);
    esp_err_t ret = ESP_OK;
    esp_http_client_config_t *http_client_config = NULL;
    esp_err_t err;

    int64_t total_start_time = esp_timer_get_time(); 
    int64_t total_data_size = 0; 

    // Allocate memory for http_client_config
    http_client_config = (esp_http_client_config_t *)psram_calloc(1, sizeof(esp_http_client_config_t));
    ESP_GOTO_ON_FALSE(http_client_config != NULL, ESP_ERR_NO_MEM, emoji_download_err, TAG, "sscma ota, mem alloc fail [1]");

    // Initialize http_client_config
    http_client_config->method = HTTP_METHOD_GET;
    http_client_config->timeout_ms = EMOJI_HTTP_TIMEOUT_MS;
    http_client_config->crt_bundle_attach = esp_crt_bundle_attach;
    http_client_config->buffer_size = EMOJI_HTTP_RX_CHUNK_SIZE;
    http_client_config->event_handler = _http_event_handler;

    for (int url_index = 0; url_index < url_count; url_index++)
    {
        // Generate a unique emoji_name for each URL
        if (emoji_name != NULL)
        {
            free(emoji_name); // Free the previous value if it was allocated
        }

        // Generate a new name for each URL with an index appended
        char name_with_index[50];
        snprintf(name_with_index, sizeof(name_with_index), "%s_%d", base_name, url_index);
        emoji_name = strdup(name_with_index); // Allocate and copy the name string

        http_client_config->url = urls[url_index];

        // Retry logic for HTTP client connection
        for (int i = 0; i < MAX_RETRY_COUNT; i++)
        {
            // Check network status before making HTTP request
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to get AP info. Retrying...");
                vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait for a second before retrying
                continue;
            }

            // Initialize http client
            esp_http_client_handle_t client = esp_http_client_init(http_client_config);
            ESP_GOTO_ON_FALSE(client != NULL, ESP_ERR_NO_MEM, emoji_download_err, TAG, "sscma ota, mem alloc fail [2]");

            // Perform HTTP GET request
            err = esp_http_client_perform(client);
            if (err == ESP_OK)
            {
                int64_t download_time = esp_timer_get_time() - total_start_time; 
                int64_t content_length = esp_http_client_get_content_length(client);
                total_data_size += content_length;

                ESP_LOGI(TAG, "EMOJI HTTP GET Status = %d, content_length = %d", esp_http_client_get_status_code(client), content_length);
                ESP_LOGI(TAG, "emoji update, HTTP GET Status = %d, content_length = %" PRId64 ", download time = %lld us", esp_http_client_get_status_code(client), content_length, download_time);
                esp_http_client_cleanup(client);
                break;
            }
            else
            {
                ret = ESP_FAIL; // We sum all these errors as download failure, easier for upper caller
                ESP_LOGE(TAG, "HTTP GET request failed: %s. Retrying...", esp_err_to_name(err));
                esp_http_client_cleanup(client);
                vTaskDelay(1000 / portTICK_PERIOD_MS); // Wait for a second before retrying
            }
        }
    }

emoji_download_err:
    // Free allocated memory for http_client_config
    if (http_client_config)
    {
        free(http_client_config);
    }

    // Free the global emoji_name at the end
    if (emoji_name != NULL)
    {
        free(emoji_name);
        emoji_name = NULL;
    }

    // Calculate total download time and speed
    int64_t total_end_time = esp_timer_get_time();
    int64_t total_time_us = total_end_time - total_start_time;
    double total_time_s = total_time_us / 1000000.0;
    double download_speed = total_data_size / total_time_s;

    ESP_LOGI(TAG, "Total download size: %" PRId64 " bytes", total_data_size);
    ESP_LOGI(TAG, "Total download time: %.2f seconds", total_time_s);
    ESP_LOGI(TAG, "Overall download speed: %.2f bytes/second", download_speed);
}