
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
};

typedef struct {
    uint8_t  type;
    uint8_t *p_data;
    uint32_t len;
} tf_buffer_t;

struct tf_data_image
{
    uint8_t *p_buf;
    uint32_t len;
    time_t   time;
};

typedef struct tf_data_images
{
    uint8_t  type;
    struct tf_data_image img_small;
    struct tf_data_image img_large;
    //todo add inference data ?
} tf_data_images_t;

#ifdef __cplusplus
}
#endif
