/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "sensecap-watcher.h"

// #include "lv_demos.h"
#include "ui/neutral_face.c"


#include <stdio.h>
#include <inttypes.h>
#include <sdkconfig.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"
#include "string.h"

#define BUFFER_SIZE      (1024)
#define SAMPLE_RATE      (16000) // For recording
#define SAMPLE_CHANNELS  (1)
#define DEFAULT_VOLUME   (100)
#define RECORDING_LENGTH (300)
typedef struct __attribute__((packed))
{
    uint8_t ignore_0[22];
    uint16_t num_channels;
    uint32_t sample_rate;
    uint8_t ignore_1[6];
    uint16_t bits_per_sample;
    uint8_t ignore_2[4];
    uint32_t data_size;
    uint8_t data[];
} dumb_wav_header_t;

/* Globals */
static QueueHandle_t audio_button_q = NULL;

int detect_flag = 0;
static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;
static volatile int task_flag = 0;

static const char *TAG = "LVGL_DEMO";

static lv_disp_t *lvgl_disp = NULL;
static esp_io_expander_handle_t io_expander = NULL;

static void btn_handler(void *button_handle, void *usr_data)
{
    int button_pressed = (int)usr_data;
    xQueueSend(audio_button_q, &button_pressed, 0);
}

static void audio_task(void *arg)
{
    bsp_codec_init();
    bsp_codec_volume_set(DEFAULT_VOLUME, NULL);

    const char *play_filename = "/sdcard/Neutral.wav";

    int16_t *wav_bytes = heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DEFAULT);
    assert(wav_bytes != NULL);

    FILE *play_file = fopen(play_filename, "rb");
    if (!play_file) {
        ESP_LOGE(TAG, "Failed to open %s", play_filename);
        vTaskDelete(NULL);
        return;
    }

    dumb_wav_header_t wav_header;
    if (fread((void *)&wav_header, 1, sizeof(wav_header), play_file) != sizeof(wav_header)) {
        ESP_LOGE(TAG, "Error reading WAV header");
        fclose(play_file);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Playing %s", play_filename);
    bsp_codec_set_fs(wav_header.sample_rate, wav_header.bits_per_sample, wav_header.num_channels);

    size_t bytes_sent = 0;
    while (bytes_sent < wav_header.data_size) {
        size_t bytes_read = fread(wav_bytes, 1, BUFFER_SIZE, play_file);
        if (bytes_read <= 0) break;
        size_t bytes_written = 0;
        ESP_ERROR_CHECK(bsp_i2s_write(wav_bytes, bytes_read, &bytes_written, portMAX_DELAY));
        bytes_sent += bytes_written;
    }

    bsp_codec_dev_stop();
    fclose(play_file);
    free(wav_bytes);
    ESP_LOGI(TAG, "Playback finished.");
    vTaskDelete(NULL); // Exit the task after playback
}

static void audio_init(void)
{
    bsp_sdcard_init_default();  // Mount SD card

    BaseType_t ret = xTaskCreate(audio_task, "audio_task", 4096, NULL, 6, NULL);
    assert(ret == pdPASS);
}


void app_main(void)
{
    io_expander = bsp_io_expander_init();
    assert(io_expander != NULL);
    lvgl_disp = bsp_lvgl_init();
    assert(lvgl_disp != NULL);
    audio_init();
    
    
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(0))
    {
        // lv_demo_widgets(); /* A widgets example */
        lv_obj_t *img = lv_img_create(lv_scr_act());
        lv_img_set_src(img, &neutral_face);
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
        // lv_demo_music(); /* A modern, smartphone-like music player demo. */
        // lv_demo_stress();       /* A stress test for LVGL. */
        // lv_demo_benchmark();    /* A demo to measure the performance of LVGL or to compare different settings. */

        // Release the mutex
        lvgl_port_unlock();
    }

    while (1)
    {
        printf("free_heap_size = %ld\n", esp_get_free_heap_size());
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
