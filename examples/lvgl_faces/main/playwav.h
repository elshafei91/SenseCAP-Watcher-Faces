#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "sensecap-watcher.h"

// WAV header structure
#pragma pack(push, 1)
typedef struct WAVHeader {
    char riff_id[4];
    uint32_t riff_size;
    char wave_id[4];

    char fmt_id[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;

    char data_id[4];
    uint32_t data_size;
} WAVHeader;
#pragma pack(pop)

// Functions
void dump_wav_header(const WAVHeader *header);
int validate_wav_header(const WAVHeader *header);
void play_audio_task(void *param);

#ifdef __cplusplus
}
#endif
