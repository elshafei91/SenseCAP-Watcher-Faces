#include "app_taskflow.h"
#include "esp_err.h"
#include "tf.h"
#include "tf_module_timer.h"
#include "tf_module_debug.h"
#include "tf_module_ai_camera.h"
#include "tf_module_img_analyzer.h"
#include "tf_module_alarm.h"
#include "tf_module_alarm_trigger.h"
void app_taskflow_init(void)
{
    ESP_ERROR_CHECK(tf_engine_init());
    ESP_ERROR_CHECK(tf_module_timer_register());
    ESP_ERROR_CHECK(tf_module_debug_register());
    ESP_ERROR_CHECK(tf_module_ai_camera_register());
    ESP_ERROR_CHECK(tf_module_img_analyzer_register());
    ESP_ERROR_CHECK(tf_module_alarm_register());
    ESP_ERROR_CHECK(tf_module_alarm_trigger_register());
}