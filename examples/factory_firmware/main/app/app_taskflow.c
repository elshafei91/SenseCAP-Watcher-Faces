#include "app_taskflow.h"
#include "esp_err.h"
#include "tf.h"
#include "tf_module_timer.h"
#include "tf_module_debug.h"
void app_taskflow_init(void)
{
    ESP_ERROR_CHECK(tf_engine_init());
    ESP_ERROR_CHECK(tf_module_timer_register());
    ESP_ERROR_CHECK(tf_module_debug_register());
}