
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
    TF_DATA_TYPE_DUALIMAGE_WITH_INFERENCE,
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
    uint8_t  type;
    struct tf_data_buf data;
} tf_data_buffer_t;

#ifdef __cplusplus
}
#endif
