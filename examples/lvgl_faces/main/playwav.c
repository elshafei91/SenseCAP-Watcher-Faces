#include "playwav.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WAV_BUFFER_SIZE 512
static const char *TAG = "PLAYWAV";

void dump_wav_header(const WAVHeader *header) {
    ESP_LOGI(TAG, "RIFF ID: %.4s", header->riff_id);
    ESP_LOGI(TAG, "RIFF Size: %" PRIu32, header->riff_size);
    ESP_LOGI(TAG, "WAVE ID: %.4s", header->wave_id);

    ESP_LOGI(TAG, "FMT ID: %.4s", header->fmt_id);
    ESP_LOGI(TAG, "FMT Size: %" PRIu32, header->fmt_size);
    ESP_LOGI(TAG, "Audio Format: %u", header->audio_format);
    ESP_LOGI(TAG, "Channels: %u", header->num_channels);
    ESP_LOGI(TAG, "Sample Rate: %" PRIu32, header->sample_rate);
    ESP_LOGI(TAG, "Byte Rate: %" PRIu32, header->byte_rate);
    ESP_LOGI(TAG, "Block Align: %u", header->block_align);
    ESP_LOGI(TAG, "Bits per Sample: %u", header->bits_per_sample);

    ESP_LOGI(TAG, "Data ID: %.4s", header->data_id);
    ESP_LOGI(TAG, "Data Size: %" PRIu32, header->data_size);
}

int validate_wav_header(const WAVHeader *header)
{
    if (strncmp(header->riff_id, "RIFF", 4) != 0)
        return 0;
    if (strncmp(header->wave_id, "WAVE", 4) != 0)
        return 0;
    if (strncmp(header->fmt_id, "fmt ", 4) != 0)
        return 0;
    if (header->fmt_size != 16)
        return 0;
    if (header->audio_format != 1)
        return 0;
    if (header->num_channels < 1 || header->num_channels > 2)
        return 0;
    if (header->sample_rate < 8000 || header->sample_rate > 192000)
        return 0;
    if (header->bits_per_sample != 8 && header->bits_per_sample != 16 && header->bits_per_sample != 24 && header->bits_per_sample != 32)
        return 0;
    if (strncmp(header->data_id, "data", 4) != 0)
        return 0;

    uint16_t expected_block_align = header->num_channels * (header->bits_per_sample / 8);
    uint32_t expected_byte_rate = header->sample_rate * expected_block_align;

    if (header->block_align != expected_block_align)
        return 0;
    if (header->byte_rate != expected_byte_rate)
        return 0;

    return 1;
}

void play_audio_task(void *param)
{
    const char *filename = (const char *)param;
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s", DRV_BASE_PATH_SD, filename);

    FILE *fp = fopen(filepath, "rb");
    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open WAV file: %s", filepath);
        vTaskDelete(NULL);
        return;
    }

    WAVHeader header;
    size_t read = fread(&header, 1, sizeof(WAVHeader), fp);
    if (read != sizeof(WAVHeader))
    {
        ESP_LOGE(TAG, "Failed to read WAV header.");
        fclose(fp);
        vTaskDelete(NULL);
        return;
    }

    if (!validate_wav_header(&header))
    {
        ESP_LOGE(TAG, "Invalid WAV header.");
        fclose(fp);
        vTaskDelete(NULL);
        return;
    }

    dump_wav_header(&header);
    bsp_codec_set_fs(header.sample_rate, header.bits_per_sample, header.num_channels);

    uint8_t *wav_buffer = malloc(WAV_BUFFER_SIZE);
    if (!wav_buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate audio buffer.");
        fclose(fp);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting playback...");
    size_t bytes_read, bytes_written;
    while ((bytes_read = fread(wav_buffer, 1, WAV_BUFFER_SIZE, fp)) > 0)
    {
        esp_err_t err = bsp_i2s_write(wav_buffer, bytes_read, &bytes_written, 0);
        if (err != ESP_OK || bytes_written != bytes_read)
        {
            ESP_LOGE(TAG, "I2S write error or mismatch.");
            break;
        }
    }

    ESP_LOGI(TAG, "Playback completed.");
    free(wav_buffer);
    fclose(fp);
    vTaskDelete(NULL);
}
