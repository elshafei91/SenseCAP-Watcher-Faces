#include "tf_util.h"
#include "esp_heap_caps.h"
void *tf_malloc(size_t sz)
{
    return heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void tf_free(void *ptr)
{
    free(ptr);
}
