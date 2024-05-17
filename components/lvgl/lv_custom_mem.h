#include <stddef.h>   // for size_t
#include "esp_heap_caps.h"
#include "esp_log.h"


#define TAG "LVGL_MEM_ALLOC"


#define malloc(size) ({ \
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM); \
    if (ptr) { \
        ESP_LOGI(TAG, "Allocated %d bytes in PSRAM at address %p", size, ptr); \
    } else { \
        ESP_LOGI(TAG, "Failed to allocate %d bytes in PSRAM", size); \
    } \
    ptr; \
})

#define free(ptr) ({ \
    ESP_LOGI(TAG, "Freeing memory at address %p", ptr); \
    free(ptr); \
})

#define realloc(ptr, size) ({ \
    void *new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM); \
    if (new_ptr) { \
        ESP_LOGI(TAG, "Reallocated memory to %d bytes in PSRAM at address %p", size, new_ptr); \
    } else { \
        ESP_LOGI(TAG, "Failed to reallocate memory to %d bytes in PSRAM", size); \
    } \
    new_ptr; \
})
