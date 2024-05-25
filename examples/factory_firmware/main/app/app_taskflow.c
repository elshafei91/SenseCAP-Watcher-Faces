#include "app_taskflow.h"
#include "esp_err.h"
#include "esp_check.h"
#include "event_loops.h"
#include "data_defs.h"
#include "storage.h"
#include "util.h"
#include "uuid.h"
#include "app_sensecraft.h"
#include "tf.h"
#include "tf_module_timer.h"
#include "tf_module_debug.h"
#include "tf_module_ai_camera.h"
#include "tf_module_img_analyzer.h"
#include "tf_module_local_alarm.h"
#include "tf_module_alarm_trigger.h"
#include "tf_module_sensecraft_alarm.h"

static const char *TAG = "taskflow";

#define TASK_FLOW_INFO_STORAGE   "taskflow-info"
#define TASK_FLOW_JSON_STORAGE   "taskflow-json"

#define TF_TYPE_LOCAL    0
#define TF_TYPE_MQTT     1
#define TF_TYPE_BLE      2
#define TF_TYPE_SR       3

#define TASK_FLOW_NULL  "{}"

struct app_taskflow *gp_taskflow = NULL;

static bool g_mqtt_connect_flag = false;

const char local_taskflow_gesture[] = \
"{  \ 
	\"tlid\": 1,    \  
	\"ctd\": 1,    \  
	\"tn\": \"Local Gesture Detection\",    \  
	\"type\": 0,    \  
	\"task_flow\": [{    \  
		\"id\": 1,    \  
		\"type\": \"ai camera\",    \  
		\"index\": 0,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"model_type\": 3,    \  
			\"modes\": 0,    \  
			\"conditions\": [{    \  
				\"class\": \"scissors\",    \  
				\"mode\": 1,    \  
				\"type\": 2,    \  
				\"num\": 0    \  
			}],    \  
			\"conditions_combo\": 0,    \  
			\"silent_period\": {    \  
				\"silence_duration\": 5    \  
			},    \  
			\"output_type\": 0,    \  
			\"shutter\": 0    \  
		},    \  
		\"wires\": [    \  
			[2]    \  
		]    \  
	}, {    \  
		\"id\": 2,    \  
		\"type\": \"alarm trigger\",    \  
		\"index\": 1,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"text\": \"scissors detected\",    \  
			\"audio\": \"\"    \  
		},    \  
		\"wires\": [    \  
			[3]    \  
		]    \  
	}, {    \  
		\"id\": 3,    \  
		\"type\": \"local alarm\",    \  
		\"index\": 2,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"sound\": 1,    \  
			\"rgb\": 1,    \
			\"img\": 0,    \  
			\"text\": 0,    \    
			\"duration\": 5    \  
		},    \  
		\"wires\": []    \  
	}, {    \
        \"id\": 4,  \
        \"type\": \"sensecraft alarm\", \
        \"index\": 3,   \
        \"version\": \"1.0.0\", \
        \"params\": {   \
            \"silence_duration\": 30    \
        },  \
        \"wires\": []   \
    }   \
    ]   \  
}";

const char local_taskflow_pet[] = \
"{  \ 
	\"tlid\": 1,    \  
	\"ctd\": 1,    \  
	\"tn\": \"Local Pet Detection\",    \  
	\"type\": 0,    \  
	\"task_flow\": [{    \  
		\"id\": 1,    \  
		\"type\": \"ai camera\",    \  
		\"index\": 0,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"model_type\": 2,    \  
			\"modes\": 0,    \  
			\"conditions\": [{    \  
				\"class\": \"dog\",    \  
				\"mode\": 1,    \  
				\"type\": 2,    \  
				\"num\": 0    \  
			}],    \  
			\"conditions_combo\": 0,    \  
			\"silent_period\": {    \  
				\"silence_duration\": 5    \  
			},    \  
			\"output_type\": 0,    \  
			\"shutter\": 0    \  
		},    \  
		\"wires\": [    \  
			[2]    \  
		]    \  
	}, {    \  
		\"id\": 2,    \  
		\"type\": \"alarm trigger\",    \  
		\"index\": 1,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"text\": \"dog detected\",    \  
			\"audio\": \"\"    \  
		},    \  
		\"wires\": [    \  
			[3]    \  
		]    \  
	}, {    \  
		\"id\": 3,    \  
		\"type\": \"local alarm\",    \  
		\"index\": 2,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"sound\": 1,    \  
			\"rgb\": 1,    \
			\"img\": 0,    \  
			\"text\": 0,    \   
			\"duration\": 5    \  
		},    \  
		\"wires\": []    \  
	}, {    \
        \"id\": 4,  \
        \"type\": \"sensecraft alarm\", \
        \"index\": 3,   \
        \"version\": \"1.0.0\", \
        \"params\": {   \
            \"silence_duration\": 30    \
        },  \
        \"wires\": []   \
    }   \
    ]   \  
}";
const char local_taskflow_human[] = \
"{  \ 
	\"tlid\": 1,    \  
	\"ctd\": 1,    \  
	\"tn\": \"Local Human Detection\",    \  
	\"type\": 0,    \  
	\"task_flow\": [{    \  
		\"id\": 1,    \  
		\"type\": \"ai camera\",    \  
		\"index\": 0,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"model_type\": 1,    \  
			\"modes\": 0,    \  
			\"conditions\": [{    \  
				\"class\": \"person\",    \  
				\"mode\": 1,    \  
				\"type\": 2,    \  
				\"num\": 0    \  
			}],    \  
			\"conditions_combo\": 0,    \  
			\"silent_period\": {    \  
				\"silence_duration\": 5    \  
			},    \  
			\"output_type\": 0,    \  
			\"shutter\": 0    \  
		},    \  
		\"wires\": [    \  
			[2]    \  
		]    \  
	}, {    \  
		\"id\": 2,    \  
		\"type\": \"alarm trigger\",    \  
		\"index\": 1,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"text\": \"human detected\",    \  
			\"audio\": \"\"    \  
		},    \  
		\"wires\": [    \  
			[3, 4]    \  
		]    \  
	}, {    \  
		\"id\": 3,    \  
		\"type\": \"local alarm\",    \  
		\"index\": 2,    \  
		\"version\": \"1.0.0\",    \  
		\"params\": {    \  
			\"sound\": 1,    \  
			\"rgb\": 1,    \
			\"img\": 0,    \  
			\"text\": 0,    \  
			\"duration\": 5    \  
		},    \  
		\"wires\": []    \  
	}, {    \
        \"id\": 4,  \
        \"type\": \"sensecraft alarm\", \
        \"index\": 3,   \
        \"version\": \"1.0.0\", \
        \"params\": {   \
            \"silence_duration\": 30    \
        },  \
        \"wires\": []   \
    }   \
    ]    \  
}";
static void __task_flow_clean( void )
{
    esp_err_t ret = ESP_OK;

    struct app_taskflow_info info;
    info.len = 0;
    info.is_valid = false;
    ret = storage_write(TASK_FLOW_INFO_STORAGE, (void *)&info, sizeof(struct app_taskflow_info));
    if( ret != ESP_OK ) {
        ESP_LOGD(TAG, "taskflow info save err:%d", ret);
    } else {
        ESP_LOGD(TAG, "taskflow info save successful");
    }
}

static void __task_flow_save(const char *p_str, int len)
{
    esp_err_t ret = ESP_OK;

    __task_flow_clean();

    //save taskflow json
    ret = storage_write(TASK_FLOW_JSON_STORAGE, (void *)p_str, len);
    if( ret != ESP_OK ) {
        ESP_LOGD(TAG, "taskflow json save err:%d", ret);
        return;
    } else {
        ESP_LOGD(TAG, "taskflow json save successful");
    }

    // save taskflow info 
    struct app_taskflow_info info;
    info.len = len;
    info.is_valid = true;
    ret = storage_write(TASK_FLOW_INFO_STORAGE, (void *)&info, sizeof(struct app_taskflow_info));
    if( ret != ESP_OK ) {
        ESP_LOGD(TAG, "taskflow info save err:%d", ret);
    } else {
        ESP_LOGD(TAG, "taskflow info save successful");
    }
}

static void  __task_flow_restore()
{
    esp_err_t ret = ESP_OK;
    uint32_t flag = 1;
    struct app_taskflow_info info;
    size_t len = sizeof(info);
    ret = storage_read(TASK_FLOW_INFO_STORAGE, (void *)&info, &len);
    if( ret == ESP_OK  && len== (sizeof(info)) ) {
        ESP_LOGI(TAG, "Find taskflow info");
        if (info.is_valid && info.len > 0) {
            ESP_LOGI(TAG, "Need load taskflow...");
            char *p_json = psram_malloc(info.len);
            len = info.len;
            ret = storage_read(TASK_FLOW_JSON_STORAGE, (void *)p_json, &len);
            if( ret == ESP_OK ) {

                ESP_LOGI(TAG, "Start last taskflow");
                tf_engine_flow_set(p_json, len);

                esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                    VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, NULL, 0, portMAX_DELAY);

                //TODO notify MQTT?
            } else {
                ESP_LOGE(TAG, "Faild to load taskflow json");
            }
            free(p_json);
        }
    }
}


static void __task_flow_status_cb(void *p_arg, intmax_t tid, int status)
{
    //TODO  state may be discarded
    if( g_mqtt_connect_flag ) {
        app_sensecraft_mqtt_report_taskflow_status(tid, status);
    }
}

static void __task_flow_module_status_cb(void *p_arg, const char *p_name, int status)
{
    //TODO  state may be discarded
    if( g_mqtt_connect_flag ) {
        intmax_t tid = 0;
        tf_engine_tid_get(&tid);
        app_sensecraft_mqtt_report_taskflow_module_status(tid, TF_STATUS_ERR_MODULES_INTERNAL ,p_name, status);
    }
}

static void __taskflow_task(void *p_arg)
{
    while(1) {
        //TODO
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

static void __view_event_handler(void* handler_args, 
                                 esp_event_base_t base, 
                                 int32_t id, 
                                 void* event_data)
{
    switch (id)
    {
        case VIEW_EVENT_TASK_FLOW_STOP:
        {
            ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_STOP");
            tf_engine_stop();
            __task_flow_clean(); //TODO  local taskflow stop will execute?
            break;
        }
        case VIEW_EVENT_TASK_FLOW_START_BY_LOCAL:
        {
            uint32_t *p_tf_num = (uint32_t *)event_data;
            const char *p_task_flow = NULL;
            ESP_LOGI(TAG, "event: VIEW_EVENT_TASK_FLOW_START_BY_LOCAL:%d", *p_tf_num);

            switch (*p_tf_num)
            {
                case 0: {
                    p_task_flow = local_taskflow_gesture;
                    break;
                }
                case 1: {
                    p_task_flow = local_taskflow_pet;
                    break;
                }
                case 2: {
                    p_task_flow = local_taskflow_human;
                    break;
                }
                default:
                    break;
            }
            if( p_task_flow ) {
                tf_engine_flow_set(p_task_flow, strlen(p_task_flow));
                //TODO mqtt report
            }
            break;
        }
        default:
            break;
    }

}

static void __ctrl_event_handler(void* handler_args, 
                                 esp_event_base_t base, 
                                 int32_t id, 
                                 void* event_data)
{
    switch (id)
    {
        case CTRL_EVENT_MQTT_CONNECTED: {
            g_mqtt_connect_flag = true;
            break;
        }
        case CTRL_EVENT_TASK_FLOW_START_BY_MQTT:{
            ESP_LOGI(TAG, "event: CTRL_EVENT_TASK_FLOW_START_BY_MQTT");
            char *p_task_flow = *(char **)event_data;
            size_t len = strlen(p_task_flow);
            printf("taskflow:%s\r\n", p_task_flow);
            
            tf_engine_flow_set(p_task_flow, len);
            __task_flow_save(p_task_flow, len ); 
            free(p_task_flow);
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                    VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, NULL, 0, portMAX_DELAY);
            
            break;
        }
        case CTRL_EVENT_TASK_FLOW_START_BY_BLE: {
            ESP_LOGI(TAG, "event: CTRL_EVENT_TASK_FLOW_START_BY_BLE");
            esp_err_t ret = ESP_OK;
            char *p_task_flow = *(char **)event_data;
            size_t len = strlen(p_task_flow);
            char uuid[37];
            UUIDGen(uuid);

            tf_engine_flow_set(p_task_flow, len);
            __task_flow_save(p_task_flow, len); 

            ret = app_sensecraft_mqtt_report_taskflow_ack(uuid, p_task_flow, len);
            if( ret != ESP_OK ) {
                ESP_LOGW(TAG, "Failed to report taskflow ack to MQTT server");
            }

            free(p_task_flow);

            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                    VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, NULL, 0, portMAX_DELAY);

            break;
        }
        case CTRL_EVENT_TASK_FLOW_START_BY_SR:
        {
            ESP_LOGI(TAG, "event: CTRL_EVENT_TASK_FLOW_START_BY_SR");
            esp_err_t ret = ESP_OK;
            char *p_task_flow = *(char **)event_data;
            size_t len = strlen(p_task_flow);
            char uuid[37];
            UUIDGen(uuid);

            tf_engine_flow_set(p_task_flow, len);
            __task_flow_save(p_task_flow, len); 

            ret = app_sensecraft_mqtt_report_taskflow_ack(uuid, p_task_flow, len);
            if( ret != ESP_OK ) {
                ESP_LOGW(TAG, "Failed to report taskflow ack to MQTT server");
            }
            free(p_task_flow);
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE,  \
                                    VIEW_EVENT_TASK_FLOW_START_CURRENT_TASK, NULL, 0, portMAX_DELAY);
            break;
        }

        default:
            break;
    }

}
static  void taskflow_engine_module_init(void)
{
    ESP_ERROR_CHECK(tf_engine_init());
    ESP_ERROR_CHECK(tf_module_timer_register());
    ESP_ERROR_CHECK(tf_module_debug_register());
    ESP_ERROR_CHECK(tf_module_ai_camera_register());
    ESP_ERROR_CHECK(tf_module_img_analyzer_register());
    ESP_ERROR_CHECK(tf_module_local_alarm_register());
    ESP_ERROR_CHECK(tf_module_alarm_trigger_register());
    ESP_ERROR_CHECK(tf_module_sensecraft_alarm_register());
    //add more module

    ESP_ERROR_CHECK(tf_engine_status_cb_register(__task_flow_status_cb, NULL));
    ESP_ERROR_CHECK(tf_module_status_cb_register(__task_flow_module_status_cb, NULL));
}

esp_err_t app_taskflow_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif

    esp_err_t ret = ESP_OK;
    struct app_taskflow * p_taskflow = NULL;
    gp_taskflow = (struct app_taskflow *) psram_malloc(sizeof(struct app_taskflow));
    if (gp_taskflow == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    p_taskflow = gp_taskflow;
    memset(p_taskflow, 0, sizeof( struct app_taskflow ));

    // engine init and module register
    taskflow_engine_module_init();

    // __task_flow_restore(); //TODO 

    p_taskflow->sem_handle = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(NULL != p_taskflow->sem_handle, ESP_ERR_NO_MEM, err, TAG, "Failed to create semaphore");

    p_taskflow->p_task_stack_buf = (StackType_t *)psram_malloc(SENSECRAFT_TASK_STACK_SIZE);
    ESP_GOTO_ON_FALSE(NULL != p_taskflow->p_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    p_taskflow->p_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(NULL != p_taskflow->p_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    p_taskflow->task_handle = xTaskCreateStatic(__taskflow_task,
                                                "__taskflow_task",
                                                TASKFLOW_TASK_STACK_SIZE,
                                                (void *)p_taskflow,
                                                TASKFLOW_TASK_PRIO,
                                                p_taskflow->p_task_stack_buf,
                                                p_taskflow->p_task_buf);
    ESP_GOTO_ON_FALSE(p_taskflow->task_handle, ESP_FAIL, err, TAG, "Failed to create task");


    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        VIEW_EVENT_BASE, 
                                                        VIEW_EVENT_TASK_FLOW_STOP, 
                                                        __view_event_handler, 
                                                        NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        VIEW_EVENT_BASE, 
                                                        VIEW_EVENT_TASK_FLOW_START_BY_LOCAL, 
                                                        __view_event_handler, 
                                                        NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_TASK_FLOW_START_BY_MQTT, 
                                                        __ctrl_event_handler, 
                                                        NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_TASK_FLOW_START_BY_BLE, 
                                                        __ctrl_event_handler, 
                                                        NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_TASK_FLOW_START_BY_SR, 
                                                        __ctrl_event_handler, 
                                                        NULL));

    ESP_ERROR_CHECK(esp_event_handler_register_with(app_event_loop_handle, 
                                                        CTRL_EVENT_BASE, 
                                                        CTRL_EVENT_MQTT_CONNECTED, 
                                                        __ctrl_event_handler, 
                                                        NULL));

err:
    if(p_taskflow->task_handle ) {
        vTaskDelete(p_taskflow->task_handle);
        p_taskflow->task_handle = NULL;
    }
    if( p_taskflow->p_task_stack_buf ) {
        free(p_taskflow->p_task_stack_buf);
        p_taskflow->p_task_stack_buf = NULL;
    }
    if( p_taskflow->p_task_buf ) {
        free(p_taskflow->p_task_buf);
        p_taskflow->p_task_buf = NULL;
    }
    if (p_taskflow->sem_handle) {
        vSemaphoreDelete(p_taskflow->sem_handle);
        p_taskflow->sem_handle = NULL;
    }
    if (p_taskflow) {
        free(p_taskflow);
        p_taskflow = NULL;
    }
    return ret;
}