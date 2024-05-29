/*
 * SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "app_sr.h"
#include "esp_mn_speech_commands.h"
#include "esp_process_sdkconfig.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_mn_iface.h"
#include "model_path.h"

#include "app_audio.h"
#include "data_defs.h"
#include "event_loops.h"
#include "sensecap-watcher.h"


static const char *TAG = "app_sr";

static esp_afe_sr_iface_t *afe_handle = NULL;
static srmodel_list_t *models = NULL;
static bool manul_detect_flag = false;

sr_data_t *g_sr_data = NULL;

#define I2S_CHANNEL_NUM      2

extern bool record_flag;
extern uint32_t record_total_len;

static void audio_feed_task(void *arg)
{
    ESP_LOGI(TAG, "Feed Task");
    size_t bytes_read = 0;
    esp_afe_sr_data_t *afe_data = (esp_afe_sr_data_t *) arg;
    int audio_chunksize = afe_handle->get_feed_chunksize(afe_data);

    int feed_channel = bsp_get_feed_channel();
    ESP_LOGI(TAG, "audio_chunksize=%d, feed_channel=%d", audio_chunksize, feed_channel);

    /* Allocate audio buffer and check for result */
    int16_t *audio_buffer = heap_caps_malloc(audio_chunksize * sizeof(int16_t) * feed_channel, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); //TODO 
    assert(audio_buffer);
    g_sr_data->afe_in_buffer = audio_buffer;

    while (true) {
        if (g_sr_data->event_group && xEventGroupGetBits(g_sr_data->event_group)) {
            xEventGroupSetBits(g_sr_data->event_group, FEED_DELETED);
            vTaskDelete(NULL);
        }

        /* Read audio data from I2S bus */
        bsp_get_feed_data(false, audio_buffer, audio_chunksize * sizeof(int16_t) * feed_channel);

        //TODO
        /* Channel Adjust */
        // for (int  i = audio_chunksize - 1; i >= 0; i--) {
        //     audio_buffer[i * 3 + 2] = 0;
        //     audio_buffer[i * 3 + 1] = audio_buffer[i * 2 + 1];
        //     audio_buffer[i * 3 + 0] = audio_buffer[i * 2 + 0];
        // }

        afe_handle->feed(afe_data, audio_buffer);

        /* Checking if WIFI is connected */
        // if (g_sr_data->wifi_connected) {
        //     /* Feed samples of an audio stream to the AFE_SR */
        //     afe_handle->feed(afe_data, audio_buffer);
        // }
        audio_record_save(audio_buffer, audio_chunksize);
    }
}

static void audio_detect_task(void *arg)
{
    ESP_LOGI(TAG, "Detection task");
    static afe_vad_state_t local_state;
    static uint8_t frame_keep = 0;

    bool detect_flag = false;
    esp_afe_sr_data_t *afe_data = arg;

    while (true) {
        if (NEED_DELETE & xEventGroupGetBits(g_sr_data->event_group)) {
            xEventGroupSetBits(g_sr_data->event_group, DETECT_DELETED);
            vTaskDelete(g_sr_data->sr_task);
            vTaskDelete(NULL);
        }
        afe_fetch_result_t *res = afe_handle->fetch(afe_data);
        if (!res || res->ret_value == ESP_FAIL) {
            ESP_LOGW(TAG, "AFE Fetch Fail");
            continue;
        }
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, LOG_BOLD(LOG_COLOR_GREEN) "wakeword detected");
            sr_result_t result = {
                .wakenet_mode = WAKENET_DETECTED,
                .state = ESP_MN_STATE_DETECTING,
                .command_id = 0,
            };
            xQueueSend(g_sr_data->result_que, &result, 0);
        } else if (res->wakeup_state == WAKENET_CHANNEL_VERIFIED || manul_detect_flag) {
            detect_flag = true;
            if (manul_detect_flag) {
                manul_detect_flag = false;
                sr_result_t result = {
                    .wakenet_mode = WAKENET_DETECTED,
                    .state = ESP_MN_STATE_DETECTING,
                    .command_id = 0,
                };
                // esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_AUDIO_WAKE, NULL, 0, portMAX_DELAY); //must before xQueueSend
                xQueueSend(g_sr_data->result_que, &result, 0);
            }
            frame_keep = 0;
            g_sr_data->afe_handle->disable_wakenet(afe_data);
            ESP_LOGI(TAG, LOG_BOLD(LOG_COLOR_GREEN) "AFE_FETCH_CHANNEL_VERIFIED, channel index: %d\n", res->trigger_channel_id);
        } 

        if (true == detect_flag) {

            if (local_state != res->vad_state) {
                local_state = res->vad_state;
                frame_keep = 0;
            } else {
                frame_keep++;
            }
            if ((100 == frame_keep) && (AFE_VAD_SILENCE == res->vad_state)) {
                sr_result_t result = {
                    .wakenet_mode = WAKENET_NO_DETECT,
                    .state = ESP_MN_STATE_TIMEOUT,
                    .command_id = 0,
                };
                xQueueSend(g_sr_data->result_que, &result, 0);
                g_sr_data->afe_handle->enable_wakenet(afe_data);
                detect_flag = false;
                continue;
            }
        }
    }
    /* Task never returns */
    vTaskDelete(NULL);
}

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    switch (id)
    {
        case VIEW_EVENT_WIFI_ST: {
            struct view_data_wifi_st *p_st = ( struct view_data_wifi_st *)event_data;
            if( p_st->is_connected) {
                g_sr_data->wifi_connected = true;
            } else {
                g_sr_data->wifi_connected = false;
            }
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST:%d", g_sr_data->wifi_connected);
            break;
        }
        default:
            break;
    }
}

esp_err_t app_sr_set_language(sr_language_t new_lang)
{
    ESP_RETURN_ON_FALSE(NULL != g_sr_data, ESP_ERR_INVALID_STATE, TAG, "SR is not running");

    if (new_lang == g_sr_data->lang) {
        ESP_LOGW(TAG, "nothing to do");
        return ESP_OK;
    } else {
        g_sr_data->lang = new_lang;
    }
    ESP_LOGI(TAG, "Set language %s", SR_LANG_EN == g_sr_data->lang ? "EN" : "CN");
    if (g_sr_data->model_data) {
        g_sr_data->multinet->destroy(g_sr_data->model_data);
    }
    char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "");
    ESP_LOGI(TAG, "load wakenet:%s", wn_name);
    g_sr_data->afe_handle->set_wakenet(g_sr_data->afe_data, wn_name);
    return ESP_OK;
}

esp_err_t app_sr_start(bool record_en)
{
    esp_err_t ret = ESP_OK;
    ESP_RETURN_ON_FALSE(NULL == g_sr_data, ESP_ERR_INVALID_STATE, TAG, "SR already running");

    g_sr_data = heap_caps_calloc(1, sizeof(sr_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(NULL != g_sr_data, ESP_ERR_NO_MEM, TAG, "Failed create sr data");

    g_sr_data->result_que = xQueueCreate(3, sizeof(sr_result_t));
    ESP_GOTO_ON_FALSE(NULL != g_sr_data->result_que, ESP_ERR_NO_MEM, err, TAG, "Failed create result queue");

    g_sr_data->event_group = xEventGroupCreate();
    ESP_GOTO_ON_FALSE(NULL != g_sr_data->event_group, ESP_ERR_NO_MEM, err, TAG, "Failed create event_group");

    BaseType_t ret_val;
    models = esp_srmodel_init("model");
    afe_handle = (esp_afe_sr_iface_t *)&ESP_AFE_SR_HANDLE;
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();

    afe_config.wakenet_model_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);    
    afe_config.aec_init = false;
    afe_config.pcm_config.total_ch_num = 1;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 0;
    afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config.wakenet_init = true;
    afe_config.voice_communication_init = false;

    esp_afe_sr_data_t *afe_data = afe_handle->create_from_config(&afe_config);
    g_sr_data->afe_handle = afe_handle;
    g_sr_data->afe_data = afe_data;

    g_sr_data->lang = SR_LANG_MAX;
    ret = app_sr_set_language(SR_LANG_EN);
    ESP_GOTO_ON_FALSE(ESP_OK == ret, ESP_FAIL, err, TAG,  "Failed to set language");

    ret = esp_event_handler_instance_register_with(app_event_loop_handle,VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, __view_event_handler, NULL, NULL);
    ESP_GOTO_ON_FALSE(ESP_OK == ret, ESP_FAIL, err, TAG,  "Failed to register event");
    
    //feed task
    g_sr_data->feed_task_stack_buf = (StackType_t *)heap_caps_malloc( FEED_TASK_STACK_SIZE,  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(g_sr_data->feed_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    g_sr_data->feed_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(g_sr_data->feed_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    g_sr_data->feed_task = xTaskCreateStaticPinnedToCore(audio_feed_task,
                                                        "audio_feed_task",
                                                        FEED_TASK_STACK_SIZE,
                                                        (void *)afe_data,
                                                        FEED_TASK_PRIO,
                                                        g_sr_data->feed_task_stack_buf,
                                                        g_sr_data->feed_task_buf, 0); //TODO core 0 ？
    ESP_GOTO_ON_FALSE(g_sr_data->feed_task, ESP_FAIL, err, TAG, "Failed to create task");

    // detect task
    g_sr_data->detect_task_stack_buf = (StackType_t *)heap_caps_malloc(DETECT_TASK_STACK_SIZE,  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(g_sr_data->detect_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    g_sr_data->detect_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(g_sr_data->detect_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    g_sr_data->detect_task = xTaskCreateStaticPinnedToCore(audio_detect_task,
                                                            "audio_detect_task",
                                                            DETECT_TASK_STACK_SIZE,
                                                            (void *)afe_data,
                                                            DETECT_TASK_PRIO,
                                                            g_sr_data->detect_task_stack_buf,
                                                            g_sr_data->detect_task_buf, 1);
    ESP_GOTO_ON_FALSE(g_sr_data->detect_task, ESP_FAIL, err, TAG, "Failed to create task");


    // sr task
#if SR_TASK_STACK_ON_PSRAM
    g_sr_data->sr_task_stack_buf = (StackType_t *)heap_caps_malloc( SR_TASK_STACK_SIZE,  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(g_sr_data->sr_task_stack_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task stack");

    g_sr_data->sr_task_buf =  heap_caps_malloc(sizeof(StaticTask_t),  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_GOTO_ON_FALSE(g_sr_data->sr_task_buf, ESP_ERR_NO_MEM, err, TAG, "Failed to malloc task TCB");

    g_sr_data->sr_task = xTaskCreateStaticPinnedToCore(sr_handler_task,
                                                    "sr_handler_task",
                                                    SR_TASK_STACK_SIZE,
                                                    NULL,
                                                    SR_TASK_PRIO,
                                                    g_sr_data->sr_task_stack_buf,
                                                    g_sr_data->sr_task_buf, 1); //TODO core 0 ？
    ESP_GOTO_ON_FALSE(g_sr_data->sr_task, ESP_FAIL, err, TAG, "Failed to create task");
#else
    ret_val = xTaskCreatePinnedToCore(&sr_handler_task, "SR Handler Task", SR_TASK_STACK_SIZE, NULL, SR_TASK_PRIO, &g_sr_data->sr_task, 1);
    ESP_GOTO_ON_FALSE(pdPASS == ret_val, ESP_FAIL, err, TAG,  "Failed create audio handler task");
#endif
    audio_record_init(); 

    return ESP_OK;
err:
    app_sr_stop();
    return ret;
}

esp_err_t app_sr_stop(void)
{
    ESP_RETURN_ON_FALSE(NULL != g_sr_data, ESP_ERR_INVALID_STATE, TAG, "SR is not running");
    xEventGroupSetBits(g_sr_data->event_group, NEED_DELETE);
    xEventGroupWaitBits(g_sr_data->event_group, NEED_DELETE | FEED_DELETED | DETECT_DELETED | HANDLE_DELETED, 1, 1, portMAX_DELAY);

    if (g_sr_data->result_que) {
        vQueueDelete(g_sr_data->result_que);
        g_sr_data->result_que = NULL;
    }

    if (g_sr_data->event_group) {
        vEventGroupDelete(g_sr_data->event_group);
        g_sr_data->event_group = NULL;
    }

    if (g_sr_data->fp) {
        fclose(g_sr_data->fp);
        g_sr_data->fp = NULL;
    }

    if (g_sr_data->model_data) {
        g_sr_data->multinet->destroy(g_sr_data->model_data);
    }

    if (g_sr_data->afe_data) {
        g_sr_data->afe_handle->destroy(g_sr_data->afe_data);
    }

    if (g_sr_data->afe_in_buffer) {
        heap_caps_free(g_sr_data->afe_in_buffer);
    }

    if (g_sr_data->afe_out_buffer) {
        heap_caps_free(g_sr_data->afe_out_buffer);
    }

    if( g_sr_data->feed_task_stack_buf) {
        heap_caps_free(g_sr_data->feed_task_stack_buf);
    }
    if( g_sr_data->feed_task_buf) {
        heap_caps_free(g_sr_data->feed_task_buf);
    }
    
    if( g_sr_data->detect_task_stack_buf) {
        heap_caps_free(g_sr_data->detect_task_stack_buf);
    }
    if( g_sr_data->detect_task_buf) {
        heap_caps_free(g_sr_data->detect_task_buf);
    }

#if SR_TASK_STACK_ON_PSRAM
    if( g_sr_data->sr_task_stack_buf) {
        heap_caps_free(g_sr_data->sr_task_stack_buf);
    }
    if( g_sr_data->sr_task_buf) {
        heap_caps_free(g_sr_data->sr_task_buf);
    }
#endif
    heap_caps_free(g_sr_data);
    g_sr_data = NULL;
    return ESP_OK;
}

esp_err_t app_sr_get_result(sr_result_t *result, TickType_t xTicksToWait)
{
    ESP_RETURN_ON_FALSE(NULL != g_sr_data, ESP_ERR_INVALID_STATE, TAG, "SR is not running");
    xQueueReceive(g_sr_data->result_que, result, xTicksToWait);
    return ESP_OK;
}

esp_err_t app_sr_start_once(void)
{
    ESP_RETURN_ON_FALSE(NULL != g_sr_data, ESP_ERR_INVALID_STATE, TAG, "SR is not running");
    manul_detect_flag = true;
    return ESP_OK;
}
