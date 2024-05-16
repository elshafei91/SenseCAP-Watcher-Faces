#include "app_taskflow.h"
#include "esp_err.h"
#include "tf.h"
#include "tf_module_timer.h"
#include "tf_module_debug.h"
#include "tf_module_ai_camera.h"
#include "tf_module_img_analyzer.h"
#include "tf_module_local_alarm.h"
#include "tf_module_alarm_trigger.h"

// const char taskflow_test[]="{\"tl\":[{\"id\":123,\"type\":\"ai camera\",\"type_id\":0,\"index\":0,\"params\":{\"model_type\":0,\"model\":{\"model_id\":\"60021\",\"version\":\"1.0.0\",\"arguments\":{\"size\":8199,\"url\":\"https://sensecraft-statics.oss-accelerate.com\",\"icon\":\"https://sensecraft-statics.oss-accelerate.xxx.png\",\"task\":\"detect\",\"createdAt\":\"1695282154\",\"updatedAt\":\"1714960582\"},\"model_name\":\"GeneralObjectDetection\",\"model_format\":\"TensorRT\",\"ai_framwork\":\"2\",\"author\":\"SenseCraftAI\",\"algorithm\":\"ObjectDectect(TensorRT,SMALL,COCO)\",\"classes\":[\"person\"],\"checksum\":\"12345667dgdf\"},\"modes\":0,\"conditions\":[{\"class\":\"person\",\"mode\":1,\"type\":2,\"num\":0}],\"conditions_combo\":0,\"silent_period\":{\"time_period\":{\"repeat\":[1,1,1,1,1,1,1],\"time_start\":\"00:00:00\",\"time_end\":\"23:59:59\"},\"silence_duration\":10},\"resolution\":1,\"shutter\":0},\"wires\":[[456]]},{\"id\":456,\"type\":\"image analyzer\",\"type_id\":0,\"index\":1,\"params\":{\"body\":{\"prompt\":\"What's in this picture?\",\"type\":0,\"audio_txt\":\"hello\"}},\"wires\":[[789]]},{\"id\":789,\"type\":\"local alarm\",\"type_id\":0,\"index\":2,\"params\":{\"sound\":1,\"rgb\":1,\"duration\":10},\"wires\":[]}]}";
void app_taskflow_init(void)
{
    ESP_ERROR_CHECK(tf_engine_init());
    ESP_ERROR_CHECK(tf_module_timer_register());
    ESP_ERROR_CHECK(tf_module_debug_register());
    ESP_ERROR_CHECK(tf_module_ai_camera_register());
    ESP_ERROR_CHECK(tf_module_img_analyzer_register());
    ESP_ERROR_CHECK(tf_module_local_alarm_register());
    ESP_ERROR_CHECK(tf_module_alarm_trigger_register());

    // TEST 
    // ESP_ERROR_CHECK(tf_engine_flow_set(taskflow_test, strlen(taskflow_test)));
}