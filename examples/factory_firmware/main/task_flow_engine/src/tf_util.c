#include "tf_util.h"
#include "esp_heap_caps.h"
void *tf_malloc(size_t sz)
{
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
}

void tf_free(void *ptr)
{
    free(ptr);
}
