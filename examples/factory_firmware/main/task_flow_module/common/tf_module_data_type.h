
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
};

typedef struct {
    uint8_t  type;
    uint8_t *p_data;
    uint32_t len;
} tf_buffer_t;

#ifdef __cplusplus
}
#endif
