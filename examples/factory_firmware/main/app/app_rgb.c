#include <time.h>

#include "esp_timer.h"
#include "esp_log.h"


#include "sensecap-watcher.h"
#include "app_rgb.h"
#include "event_loops.h"
#include "data_defs.h"

#define RGB_TAG "RGB_TAG"

// Stack memory allocation for the RGB effect task
static StackType_t *app_rgb_stack = NULL;
static StaticTask_t app_rgb_stack_buffer;

// Structure to hold the RGB status, including color and brightness settings
typedef struct {
    uint8_t r;  // Red component of the RGB color
    uint8_t g;  // Green component of the RGB color
    uint8_t b;  // Blue component of the RGB color
    uint8_t max_brightness_led;  // Maximum brightness level of the LED
    uint8_t min_brightness_led;  // Minimum brightness level of the LED
    int step;  // Step value for changing brightness
    int delay_time;  // Delay time between brightness changes
} rgb_status;

// Instance of the RGB status structure to hold the current RGB settings
static rgb_status rgb_status_instance;

// Structure to hold the caller context, including the caller type, service, and RGB status
typedef struct {
    caller caller_type;  // Type of the caller (e.g., UI_CALLER, AT_CMD_CALLER)
    rgb_service_t service;  // Type of the RGB service (e.g., breath_red, off)
    rgb_status status;  // RGB status associated with the caller
} caller_context_t;

// Stack to hold the contexts of different callers, used for priority management
static caller_context_t caller_stack[MAX_CALLER];

// Top index for the caller stack, used to manage the stack operations
static int top = -1;

// Current active caller, initialized to the lowest priority caller (BLE_CALLER)
static caller current_caller = BLE_CALLER;

void breath_effect_task(void *arg);
esp_err_t push_caller_context(caller caller_type, rgb_service_t service, rgb_status status) {
    if (top < MAX_CALLER - 1) {
        caller_stack[++top] = (caller_context_t){caller_type, service, status};
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t pop_caller_context(caller_context_t *context) {
    if (top >= 0) {
        *context = caller_stack[top--];
        return ESP_OK;
    }
    return ESP_FAIL;
}

esp_err_t set_rgb(int caller_type, int service) {
    caller rgb_caller = (caller)caller_type;
    rgb_service_t rgb_service = (rgb_service_t)service;

    // If the service is 'off', it indicates the end of the caller's operation
    if (rgb_service == off) {
        if (rgb_caller == UI_CALLER || rgb_caller == AT_CMD_CALLER) {
            // If UI_CALLER or AT_CMD_CALLER is turning off, clear the stack
            top = -1;
            current_caller = BLE_CALLER; // Reset to lowest priority
            rgb_status_instance = (rgb_status){0, 0, 0, 0, 0, 0, 0};
            bsp_rgb_set(0, 0, 0);
        } else {
            // For other callers, pop the previous context
            caller_context_t prev_context;
            if (pop_caller_context(&prev_context) == ESP_OK) {
                current_caller = prev_context.caller_type;
                rgb_status_instance = prev_context.status;

                // Restart the breath effect task with the previous settings
                if (app_rgb_stack != NULL) {
                    vTaskDelete(NULL);
                    TaskHandle_t task_handle = xTaskCreateStatic(&breath_effect_task, "app_rgb_task", 4096, &rgb_status_instance, 5, app_rgb_stack, &app_rgb_stack_buffer);
                    if (task_handle == NULL) {
                        ESP_LOGE(RGB_TAG, "Failed to create task");
                        return ESP_FAIL;
                    }
                }
            }
        }
        return ESP_OK;
    }

    rgb_status new_status;

    switch (rgb_service) {
        case breath_red:
            new_status = (rgb_status){255, 0, 0, 255, 0, 50, 200};
            break;
        case breath_green:
            new_status = (rgb_status){0, 255, 0, 255, 0, 50, 200};
            break;
        case breath_blue:
            new_status = (rgb_status){0, 0, 255, 255, 0, 50, 200};
            break;
        case breath_white:
            new_status = (rgb_status){255, 255, 255, 255, 0, 50, 200};
            break;
        case glint_red:
            new_status = (rgb_status){255, 0, 0, 255, 0, 0, 500};
            break;
        case glint_green:
            new_status = (rgb_status){0, 255, 0, 255, 0, 0, 500};
            break;
        case glint_blue:
            new_status = (rgb_status){0, 0, 255, 255, 0, 0, 500};
            break;
        case glint_white:
            new_status = (rgb_status){255, 255, 255, 255, 0, 0, 500};
            break;
        case flare_red:
            new_status = (rgb_status){255, 0, 0, 255, 255, 0, 0};
            break;
        case flare_green:
            new_status = (rgb_status){0, 255, 0, 255, 255, 0, 0};
            break;
        case flare_blue:
            new_status = (rgb_status){0, 0, 255, 255, 255, 0, 0};
            break;
        case flare_white:
            new_status = (rgb_status){255, 255, 255, 255, 255, 0, 0};
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    if ((current_caller == UI_CALLER || current_caller == AT_CMD_CALLER) && 
        (rgb_caller != UI_CALLER && rgb_caller != AT_CMD_CALLER)) {
        return ESP_FAIL; // UI_CALLER and AT_CMD_CALLER are controlling, ignore others
    }

    if (rgb_caller == UI_CALLER || rgb_caller == AT_CMD_CALLER || (rgb_caller > current_caller)) {
        // Save current status and caller if new caller has higher priority
        push_caller_context(current_caller, rgb_service, rgb_status_instance);
        current_caller = rgb_caller;
        rgb_status_instance = new_status;

        // Restart the breath effect task with the new settings
        if (app_rgb_stack != NULL) {
            vTaskDelete(NULL);
            TaskHandle_t task_handle = xTaskCreateStatic(&breath_effect_task, "app_rgb_task", 4096, &rgb_status_instance, 5, app_rgb_stack, &app_rgb_stack_buffer);
            if (task_handle == NULL) {
                ESP_LOGE(RGB_TAG, "Failed to create task");
                return ESP_FAIL;
            }
        }
    }
    return ESP_OK;
}










void __set_breath_color(rgb_status *status) {
    uint8_t brightness_led = status->min_brightness_led;
    bool increasing = true;

    uint8_t current_r = (status->r * brightness_led) / 255;
    uint8_t current_g = (status->g * brightness_led) / 255;
    uint8_t current_b = (status->b * brightness_led) / 255;

    bsp_rgb_set(current_r, current_g, current_b);

    while (true) {
        if (increasing) {
            brightness_led += status->step;
            if (brightness_led >= status->max_brightness_led) {
                brightness_led = status->max_brightness_led;
                increasing = false;
            }
        } else {
            brightness_led -= status->step;
            if (brightness_led <= status->min_brightness_led) {
                brightness_led = status->min_brightness_led;
                increasing = true;
            }
        }
        current_r = (status->r * brightness_led) / 255;
        current_g = (status->g * brightness_led) / 255;
        current_b = (status->b * brightness_led) / 255;
        bsp_rgb_set(current_r, current_g, current_b);
        vTaskDelay(pdMS_TO_TICKS(status->delay_time));
    }
}



void breath_effect_task(void *arg) {
    rgb_status *status = (rgb_status *)arg;
    __set_breath_color(status);
}



int app_rgb_init(void) {
    rgb_status_instance = (rgb_status){
        .r = 255,
        .g = 255,
        .b = 255,
        .max_brightness_led = 255,
        .min_brightness_led = 0,
        .step = 50,
        .delay_time = 200
    };

    app_rgb_stack = (StackType_t *)heap_caps_malloc(4096 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (app_rgb_stack == NULL) {
        ESP_LOGE(RGB_TAG, "Failed to allocate memory for task stack");
        return -1;
    }
    TaskHandle_t task_handle = xTaskCreateStatic(&breath_effect_task, "app_rgb_task", 4096, &rgb_status_instance, 5, app_rgb_stack, &app_rgb_stack_buffer);
    if (task_handle == NULL) {
        ESP_LOGE(RGB_TAG, "Failed to create task");
        return -1;
    }
    return 0;
}
