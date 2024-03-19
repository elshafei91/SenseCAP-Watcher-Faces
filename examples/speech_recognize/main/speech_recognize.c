/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <sdkconfig.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "indoor_ai_camera.h"

/* Buffer for reading/writing to I2S driver. Same length as SPIFFS buffer and I2S buffer, for optimal read/write performance.
   Recording audio data path:
   I2S peripheral -> I2S buffer (DMA) -> App buffer (RAM) -> SPIFFS buffer -> External SPI Flash.
   Vice versa for playback. */
#define BUFFER_SIZE     (1024)
#define SAMPLE_RATE     (16000) // For recording
#define DEFAULT_VOLUME  (50)
/* The recording will be RECORDING_LENGTH * BUFFER_SIZE long (in bytes)
   With sampling frequency 16000 Hz and 16bit mono resolution it equals to ~5.12 seconds */
#define RECORDING_LENGTH (160)

/* Globals */
static const char *TAG = "example";
static QueueHandle_t audio_button_q = NULL;

static void btn_handler(void *button_handle, void *usr_data)
{
    int button_pressed = (int)usr_data;
    xQueueSend(audio_button_q, &button_pressed, 0);
}

// Very simple WAV header, ignores most fields
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

static void audio_task(void *arg)
{
    esp_codec_dev_handle_t spk_codec_dev = bsp_audio_codec_speaker_init();
    assert(spk_codec_dev);
    esp_codec_dev_set_out_vol(spk_codec_dev, DEFAULT_VOLUME);
    esp_codec_dev_handle_t mic_codec_dev = bsp_audio_codec_microphone_init();

    /* Pointer to a file that is going to be played */
    const char music_filename[] = "/spiffs/16bit_mono_22_05khz.wav";
    const char recording_filename[] = "/spiffs/recording.wav";
    const char *play_filename = music_filename;

    while (1) {
        int btn_index = 0;
        if (xQueueReceive(audio_button_q, &btn_index, portMAX_DELAY) == pdTRUE) {
            switch (btn_index) {
            case 0: {
                int16_t *wav_bytes = malloc(BUFFER_SIZE);
                assert(wav_bytes != NULL);
                /* Open WAV file */
                ESP_LOGI(TAG, "Playing file %s", play_filename);
                FILE *play_file = fopen(play_filename, "rb");
                if (play_file == NULL) {
                    ESP_LOGW(TAG, "%s file does not exist!", play_filename);
                    break;
                }
                /* Read WAV header file */
                dumb_wav_header_t wav_header;
                if (fread((void *)&wav_header, 1, sizeof(wav_header), play_file) != sizeof(wav_header)) {
                    ESP_LOGW(TAG, "Error in reading file");
                    break;
                }
                ESP_LOGI(TAG, "Number of channels: %" PRIu16 "", wav_header.num_channels);
                ESP_LOGI(TAG, "Bits per sample: %" PRIu16 "", wav_header.bits_per_sample);
                ESP_LOGI(TAG, "Sample rate: %" PRIu32 "", wav_header.sample_rate);
                ESP_LOGI(TAG, "Data size: %" PRIu32 "", wav_header.data_size);

                esp_codec_dev_sample_info_t fs = {
                    .sample_rate = wav_header.sample_rate,
                    .channel = wav_header.num_channels,
                    .bits_per_sample = wav_header.bits_per_sample,
                };
                esp_codec_dev_open(spk_codec_dev, &fs);
                esp_codec_dev_set_out_vol(spk_codec_dev, 100);

                uint32_t bytes_send_to_i2s = 0;
                while (bytes_send_to_i2s < wav_header.data_size) {
                    /* Get data from SPIFFS and send it to codec */
                    size_t bytes_read_from_spiffs = fread(wav_bytes, 1, BUFFER_SIZE, play_file);
                    esp_codec_dev_write(spk_codec_dev, wav_bytes, bytes_read_from_spiffs);
                    bytes_send_to_i2s += bytes_read_from_spiffs;
                }
                fclose(play_file);
                free(wav_bytes);
                esp_codec_dev_close(spk_codec_dev);
                break;
            }
            default:
                ESP_LOGI(TAG, "No function for this button");
                break;
            }
        }
    }
}

void app_main(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = false,
    };
    esp_vfs_spiffs_register(&conf);

    size_t total = 0, used = 0;
    esp_err_t ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
        return;
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    esp_io_expander_handle_t io_exp_handle = bsp_io_expander_init();

    /* Create FreeRTOS tasks and queues */
    audio_button_q = xQueueCreate(10, sizeof(int));
    assert (audio_button_q != NULL);

    BaseType_t ret = xTaskCreate(audio_task, "audio_task", 4096, NULL, 6, NULL);
    assert(ret == pdPASS);

    /* Init audio buttons */
    const static button_config_t btn_config = {
        .type = BUTTON_TYPE_CUSTOM,
        .long_press_time = 1000,
        .short_press_time = 200,
        .custom_button_config = {
            .active_level = 0,
            .button_custom_init = bsp_knob_btn_init,
            .button_custom_deinit = bsp_knob_btn_deinit,
            .button_custom_get_key_value = bsp_knob_btn_get_key_value,
        },
    };
    button_handle_t btns = iot_button_create(&btn_config);

    iot_button_register_cb(btns, BUTTON_PRESS_DOWN, btn_handler, (void *)0);
}
