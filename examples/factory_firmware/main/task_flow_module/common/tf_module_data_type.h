
#pragma once
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TF_DATA_TYPE_UNKNOWN = 0,
    TF_DATA_TYPE_UINT8,
    TF_DATA_TYPE_UINT16,
    TF_DATA_TYPE_UINT32,
    TF_DATA_TYPE_UINT64,
    TF_DATA_TYPE_INT8,
    TF_DATA_TYPE_INT16,
    TF_DATA_TYPE_INT32,
    TF_DATA_TYPE_INT64,
    TF_DATA_TYPE_FLOAT32,
    TF_DATA_TYPE_FLOAT64,
    TF_DATA_TYPE_BUFFER,
    TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE, // tf_module_ai_camera define
    TF_DATA_TYPE_DUALIMAGE_WITH_AUDIO_TEXT,
};

struct tf_data_buf
{
    uint8_t *p_buf;
    uint32_t len;
};

struct tf_data_image
{
    uint8_t *p_buf;  //base64 data
    uint32_t len;
    time_t   time;
};

typedef struct {
    uint8_t  type; // TF_DATA_TYPE_BUFFER
    struct tf_data_buf data;
} tf_data_buffer_t;

typedef struct tf_data_dualimage_with_audio_text
{
    uint8_t type; // TF_DATA_TYPE_DUALIMAGE_WITH_AUDIO_TEXT
    struct tf_data_image img_small;
    struct tf_data_image img_large;
    struct tf_data_buf   audio;
    struct tf_data_buf   text;
} tf_data_dualimage_with_audio_text_t;


#ifdef __cplusplus
}
#endif
