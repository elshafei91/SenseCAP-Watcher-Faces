#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"
#include "sdkconfig.h"
#include "esp_heap_caps.h"


#include "app_rgb.h"
#include "at_cmd.h"
#include "app_ble.h"
#include "app_device_info.h"
#include "app_wifi.h"
#include "event_loops.h"
#include "data_defs.h"

// Static and global variables
static int ble_status = BLE_DISCONNECTED;
static uint8_t char1_str[] = { 0x11, 0x22, 0x33 };
static uint8_t char2_str[] = { 0x44, 0x55, 0x66 };

uint8_t watcher_sn_buffer[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x11 };
uint8_t watcher_name[] = { '-', 'W', 'A', 'C', 'H' };
uint8_t adv_config_done = 0;
uint8_t watcher_adv_data_RAW[] = { 0x05, 0x03, 0x86, 0x28, 0x86, 0xA8, 0x18, 0x09 };
uint8_t raw_scan_rsp_data[] = { 0x06, 0x09, '-', 'W', 'A', 'C', 'H' };

// Attribute values
esp_attr_value_t gatts_char_write_val = {
    .attr_max_len = GATTS_DEMO_CHAR_VAL_LEN_MAX,
    .attr_len = sizeof(char1_str),
    .attr_value = char1_str,
};

esp_attr_value_t gatts_char_read_val = {
    .attr_max_len = GATTS_DEMO_CHAR_VAL_LEN_MAX,
    .attr_len = sizeof(char2_str),
    .attr_value = char2_str,
};

// Semaphore
SemaphoreHandle_t ble_status_mutex = NULL;

// Stack and Task
static StackType_t *ble_task_stack = NULL;
static StaticTask_t ble_task_buffer;

// Advertising parameters
esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Properties
esp_gatt_char_prop_t watcher_write_property = 0;
esp_gatt_char_prop_t watcher_read_property = 1;

// GATT profile instance struct definition
struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    uint16_t char_handl_rx;
    uint16_t char_handl_tx;
    esp_bt_uuid_t char_uuid_write;
    esp_bt_uuid_t char_uuid_read;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};
// GATT profile instance
struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = { [PROFILE_WATCHER_APP_ID] = {
                                                              .gatts_cb = gatts_profile_event_handler,
                                                              .gatts_if = ESP_GATT_IF_NONE,
                                                          } };
// Prepare type environment struct
typedef struct
{
    uint8_t *prepare_buf;
    int prepare_len;
} prepare_type_env_t;

prepare_type_env_t prepare_write_env;
prepare_type_env_t tiny_write_env;

static void ble_config_entry(void);

static void watcher_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
static void watcher_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

/**
 * @brief Converts binary data to its hexadecimal string representation.
 *
 * This tool function converts each byte of the input binary data into its
 * corresponding two-character hexadecimal string representation. The output
 * data will contain the ASCII characters for the hexadecimal values.
 *
 * @param out_data Pointer to the output buffer where the hexadecimal string will be stored.
 *                 This buffer should be at least twice the size of the input data.
 * @param in_data Pointer to the input buffer containing the binary data to be converted.
 * @param Size The number of bytes in the input buffer.
 */
static void hexTonum(unsigned char *out_data, unsigned char *in_data, unsigned short Size) // Tool Function
{
    for (unsigned char i = 0; i < Size; i++)
    {
        out_data[2 * i] = (in_data[i] >> 4);
        out_data[2 * i + 1] = (in_data[i] & 0x0F);
    }
    for (unsigned char i = 0; i < 2 * Size; i++)
    {
        if ((out_data[i] >= 0) && (out_data[i] <= 9))
        {
            out_data[i] = '0' + out_data[i];
        }
        else if ((out_data[i] >= 0x0A) && (out_data[i] <= 0x0F))
        {
            out_data[i] = 'A' - 10 + out_data[i];
        }
        else
        {
            return;
        }
    }
}

/**
 * @brief Handles GAP (Generic Access Profile) events for BLE (Bluetooth Low Energy).
 *
 * This function processes various GAP events such as advertising data setup, advertising start,
 * advertising stop, and connection parameter updates. It ensures appropriate actions are taken
 * based on the event type and the status of the operations.
 *
 * @param event The GAP event type.
 * @param param Pointer to the structure containing the GAP callback parameters.
 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event)
    {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~adv_config_flag);
            if (adv_config_done == 0)
            {
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            adv_config_done &= (~scan_rsp_config_flag);
            if (adv_config_done == 0)
            {
                esp_ble_gap_start_advertising(&adv_params);
            }
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGE(GATTS_TAG, "Advertising start failed");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS)
            {
                ESP_LOGE(GATTS_TAG, "Advertising stop failed");
            }
            else
            {
                ESP_LOGI(GATTS_TAG, "Stop adv successfully");
            }
            break;
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            ESP_LOGI(GATTS_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d", param->update_conn_params.status,
                param->update_conn_params.min_int, param->update_conn_params.max_int, param->update_conn_params.conn_int, param->update_conn_params.latency, param->update_conn_params.timeout);
            break;
        default:
            break;
    }
}

/**
 * @brief Handles the preparation and writing of events for the specified GATT interface.
 *
 * This function processes the preparation of write events and handles the writing of data to the specified
 * GATT interface. It ensures that the data is properly prepared and written based on the given parameters.
 * If the write requires a response and is a prepare write, it checks for valid offsets and attribute lengths,
 * allocates memory for the prepare buffer if needed, and sends a response to the client. If the write does
 * not require preparation, it simply sends a response.
 *
 * @param gatts_if The GATT interface handle.
 * @param prepare_write_env Pointer to the environment structure used for preparing write events.
 * @param param Pointer to the structure containing the GATT callback parameters.
 */
static void watcher_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(GATTS_TAG, "watcher_write_event_env");
    esp_gatt_status_t status = ESP_GATT_OK;
    if (param->write.need_rsp)
    {
        if (param->write.is_prep)
        {
            if (param->write.offset > PREPARE_BUF_MAX_SIZE)
            {
                status = ESP_GATT_INVALID_OFFSET;
            }
            else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE)
            {
                status = ESP_GATT_INVALID_ATTR_LEN;
            }
            if (status == ESP_GATT_OK && prepare_write_env->prepare_buf == NULL)
            {
                prepare_write_env->prepare_buf = heap_caps_malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t), MALLOC_CAP_SPIRAM);

                prepare_write_env->prepare_len = 0;
                if (prepare_write_env->prepare_buf == NULL)
                {
                    ESP_LOGE(GATTS_TAG, "Gatt_server prep no mem");
                    status = ESP_GATT_NO_RESOURCES;
                }
            }

            esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)heap_caps_malloc(sizeof(esp_gatt_rsp_t), MALLOC_CAP_SPIRAM);
            if (gatt_rsp)
            {
                gatt_rsp->attr_value.len = param->write.len;
                gatt_rsp->attr_value.handle = param->write.handle;
                gatt_rsp->attr_value.offset = param->write.offset;
                gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
                memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
                esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
                if (response_err != ESP_OK)
                {
                    ESP_LOGE(GATTS_TAG, "Send response error\n");
                }
                free(gatt_rsp);
            }
            else
            {
                ESP_LOGE(GATTS_TAG, "malloc failed, no resource to send response error\n");
                status = ESP_GATT_NO_RESOURCES;
            }
            if (status != ESP_GATT_OK)
            {
                return;
            }
            memcpy(prepare_write_env->prepare_buf + param->write.offset, param->write.value, param->write.len);
            prepare_write_env->prepare_len += param->write.len;
        }
        else
        {
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);
        }
    }
}

/**
 * @brief Executes the prepared write events for the GATT server with tiny sittuation for data less than mtu handling.
 *
 * This function logs the prepared write event data, posts the event to a specified event loop,
 * and waits for a response. It processes the response data by sending it in multiple segments
 * if necessary, using BLE indications. The prepared buffer and response data are freed after
 * processing.
 *
 * @param gatts_if The GATT interface handle.
 * @param prepare_write_env Pointer to the environment structure used for preparing write events.
 * @param param Pointer to the structure containing the GATT callback parameters.
 */
static void watcher_exec_write_tiny_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{

    ESP_LOGI(GATTS_TAG, "watcher_exec_write_tiny_event_env");
    if (prepare_write_env->prepare_buf)
    {
        // message_event_t msg_at = { .msg = prepare_write_env->prepare_buf, .size = prepare_write_env->prepare_len };
        message_event_t msg_at;
        msg_at.size = prepare_write_env->prepare_len;
        msg_at.msg = (uint8_t *)heap_caps_malloc(msg_at.size + 1, MALLOC_CAP_SPIRAM);
        memcpy(msg_at.msg, prepare_write_env->prepare_buf, msg_at.size);
        size_t xBytesSent;
        // xBytesSent = xStreamBufferSend(xStreamBuffer, (void *)&msg_at, sizeof(msg_at), pdMS_TO_TICKS(1000));
        if (xQueueSend(message_queue, &msg_at, portMAX_DELAY) != pdPASS)
        {
            printf("Failed to send message to queue\n");
        }
        else
        {
            printf("Message sent to queue\n");
        }
        uint32_t ulNotificationValue;
        xTaskToNotify_AT = xTaskGetCurrentTaskHandle();
        ulNotificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));
        AT_Response msg_at_response;
        if (xQueueReceive(AT_response_queue, &msg_at_response, portMAX_DELAY) == pdTRUE)
        {
            uint8_t *response_data = NULL;
            if (msg_at_response.length > 0 && msg_at_response.response != NULL)
            {
                response_data = (uint8_t *)heap_caps_malloc(msg_at_response.length, MALLOC_CAP_SPIRAM);
                if (response_data == NULL)
                {
                    ESP_LOGE(GATTS_TAG, "No memory to send response");
                }
                else
                {
                    memcpy(response_data, msg_at_response.response, msg_at_response.length);
                }
                int segments = msg_at_response.length / 20;
                int remaining_bytes = msg_at_response.length % 20;

                for (int i = 0; i < segments; i++)
                {
                    esp_ble_gatts_send_indicate(gl_profile_tab[PROFILE_WATCHER_APP_ID].gatts_if, gl_profile_tab[PROFILE_WATCHER_APP_ID].conn_id, gl_profile_tab[PROFILE_WATCHER_APP_ID].char_handl_tx,
                        20, response_data + (i * 20), false);
                }

                if (remaining_bytes > 0)
                {
                    esp_ble_gatts_send_indicate(gl_profile_tab[PROFILE_WATCHER_APP_ID].gatts_if, gl_profile_tab[PROFILE_WATCHER_APP_ID].conn_id, gl_profile_tab[PROFILE_WATCHER_APP_ID].char_handl_tx,
                        remaining_bytes, response_data + (segments * 20), false);
                }
                free(prepare_write_env->prepare_buf);
                prepare_write_env->prepare_buf = NULL;
                free(response_data);
            }
        }
        free(msg_at_response.response);
        prepare_write_env->prepare_len = 0;
    }
}

/**
 * @brief Executes the prepared write events for the GATT server.
 *
 * This function processes the execution of prepared write events based on the execution flag.
 * If the write is to be executed, it logs the prepared data. If the write is canceled, it logs the cancellation.
 * The function posts the event to a specified event loop and waits for a response. It then processes
 * the response data by sending it in multiple segments using BLE indications. The prepared buffer
 * and response data are freed after processing.
 *
 * @param prepare_write_env Pointer to the environment structure used for preparing write events.
 * @param param Pointer to the structure containing the GATT callback parameters.
 */

static void watcher_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC)
    {
        esp_log_buffer_hex(GATTS_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }
    else
    {
        ESP_LOGI(GATTS_TAG, "ESP_GATT_PREP_WRITE_CANCEL");
    }
    if (prepare_write_env->prepare_buf)
    {
        message_event_t msg_at;
        msg_at.size = prepare_write_env->prepare_len;
        msg_at.msg = (uint8_t *)heap_caps_malloc(msg_at.size , MALLOC_CAP_SPIRAM);
        memcpy(msg_at.msg, prepare_write_env->prepare_buf, msg_at.size);
        esp_log_buffer_hex("TEST", msg_at.msg, msg_at.size);
        if (xQueueSend(message_queue, &msg_at, portMAX_DELAY) != pdPASS)
        {
            printf("Failed to send message to queue\n");
        }
        else
        {
            printf("Message sent to queue\n");
        }
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;

        uint32_t ulNotificationValue;
        xTaskToNotify_AT = xTaskGetCurrentTaskHandle();
        ulNotificationValue = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000)); // Do not change the timeout

        AT_Response msg_at_response;
        if (xQueueReceive(AT_response_queue, &msg_at_response, portMAX_DELAY) == pdTRUE)
        {
            uint8_t *response_data = NULL;
            if (msg_at_response.length > 0 && msg_at_response.response != NULL)
            {
                response_data = (uint8_t *)heap_caps_malloc(msg_at_response.length, MALLOC_CAP_SPIRAM);
                if (response_data == NULL)
                {
                    ESP_LOGE(GATTS_TAG, "No memory to send response");
                }
                else
                {
                    memcpy(response_data, msg_at_response.response, msg_at_response.length);
                }
                int segments = msg_at_response.length / 20;
                int remaining_bytes = msg_at_response.length % 20;
                for (int i = 0; i < segments; i++)
                {
                    esp_ble_gatts_send_indicate(gl_profile_tab[PROFILE_WATCHER_APP_ID].gatts_if, gl_profile_tab[PROFILE_WATCHER_APP_ID].conn_id, gl_profile_tab[PROFILE_WATCHER_APP_ID].char_handl_tx,
                        20, response_data + (i * 20), false);
                }

                if (remaining_bytes > 0)
                {
                    esp_ble_gatts_send_indicate(gl_profile_tab[PROFILE_WATCHER_APP_ID].gatts_if, gl_profile_tab[PROFILE_WATCHER_APP_ID].conn_id, gl_profile_tab[PROFILE_WATCHER_APP_ID].char_handl_tx,
                        remaining_bytes, response_data + (segments * 20), false);
                }
                free(response_data);
            }
        }
        free(msg_at_response.response);
        prepare_write_env->prepare_len = 0;
    }
}

/**
 * @brief Handles GATT (Generic Attribute Profile) events for BLE (Bluetooth Low Energy).
 *
 * This function processes various GATT events such as registration, reading, writing, and
 * executing write operations. It ensures appropriate actions are taken based on the event type
 * and the status of the operations.
 *
 * @param event The GATT event type.
 * @param gatts_if The GATT interface handle.
 * @param param Pointer to the structure containing the GATT callback parameters.
 */
void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
        case ESP_GATTS_REG_EVT:
            uint8_t watcher_sn_buffer_t[18] = { 0 };
            uint8_t *sn = get_sn(BLE_CALLER);
            memcpy(watcher_sn_buffer, sn, sizeof(watcher_sn_buffer));
            hexTonum(watcher_sn_buffer_t, watcher_sn_buffer, sizeof(watcher_sn_buffer));
            uint8_t send_buffer[32] = { 0 };
            uint8_t device_buffer[32] = { 0 };

            uint8_t *ptr = send_buffer;
            memcpy(ptr, watcher_adv_data_RAW, sizeof(watcher_adv_data_RAW));
            ptr += sizeof(watcher_adv_data_RAW);
            memcpy(ptr, watcher_sn_buffer_t, 18);
            ptr += sizeof(watcher_sn_buffer_t);
            memcpy(ptr, watcher_name, 5);

            printf("BLE_ADV_NAME is %s\n", &send_buffer[8]);
            memcpy(device_buffer, &send_buffer[8], 23);
            esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name((const char *)device_buffer);

            if (set_dev_name_ret)
            {
                ESP_LOGE("ADV", "set device name failed, error code = %x", set_dev_name_ret);
            }

            ESP_LOGI(GATTS_TAG, "REGISTER_APP_EVT, status %d, app_id %d", param->reg.status, param->reg.app_id);
            gl_profile_tab[PROFILE_WATCHER_APP_ID].service_id.is_primary = true;
            gl_profile_tab[PROFILE_WATCHER_APP_ID].service_id.id.inst_id = 0x00;
            gl_profile_tab[PROFILE_WATCHER_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_128;

            uint8_t uuid[ESP_UUID_LEN_128] = { 0x55, 0xE4, 0x05, 0xD2, 0xAF, 0x9F, 0xA9, 0x8F, 0xE5, 0x4A, 0x7D, 0xFE, 0x43, 0x53, 0x53, 0x49 };
            for (int i = 0; i < ESP_UUID_LEN_128; i++)
            {
                gl_profile_tab[PROFILE_WATCHER_APP_ID].service_id.id.uuid.uuid.uuid128[i] = uuid[i];
            }
            esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(send_buffer, sizeof(send_buffer) - 1);
            if (raw_adv_ret)
            {
                ESP_LOGE(GATTS_TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
            }
            esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_WATCHER_APP_ID].service_id, GATTS_NUM_HANDLE_WATCHER);
            break;
        case ESP_GATTS_READ_EVT: {
            ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
            rsp.attr_value.handle = param->read.handle;
            rsp.attr_value.len = 4;
            rsp.attr_value.value[0] = 0x00;
            rsp.attr_value.value[1] = 0x00;
            rsp.attr_value.value[2] = 0x00;
            rsp.attr_value.value[3] = 0x00;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            break;
        }
        case ESP_GATTS_WRITE_EVT: {
            // ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
            // ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT_TAG, value len %d, value :", param->write.len);
                esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
            if (!param->write.is_prep)
            {
                // ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT_TAG, value len %d, value :", param->write.len);
                // esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);

                if (param->write.len == 2)
                {
                    uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                    if (descr_value == 0x0001)
                    {
                        if (watcher_write_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY)
                        {
                            ESP_LOGI(GATTS_TAG, "notify enable");
                            uint8_t notify_data[15];
                            for (int i = 0; i < sizeof(notify_data); ++i)
                            {
                                notify_data[i] = i % 0xff;
                            }
                        }
                    }
                    else if (descr_value == 0x0002)
                    {
                        if (watcher_read_property & ESP_GATT_CHAR_PROP_BIT_INDICATE)
                        {
                            ESP_LOGI(GATTS_TAG, "indicate enable");
                            uint8_t indicate_data[15];
                            for (int i = 0; i < sizeof(indicate_data); ++i)
                            {
                                indicate_data[i] = i % 0xff;
                            }
                        }
                    }
                    else if (descr_value == 0x0000)
                    {
                        ESP_LOGI(GATTS_TAG, "notify/indicate disable ");
                    }
                    else
                    {
                        ESP_LOGE(GATTS_TAG, "unknown descr value");
                        esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
                    }
                }
                else
                {
                    tiny_write_env.prepare_buf = heap_caps_malloc(TINY_BUF_MAX_SIZE * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
                    memcpy(tiny_write_env.prepare_buf, param->write.value, param->write.len);
                    tiny_write_env.prepare_len = param->write.len;
                    watcher_exec_write_tiny_event_env(gatts_if, &tiny_write_env, param);
                    free(tiny_write_env.prepare_buf);
                }
            }
            watcher_write_event_env(gatts_if, &prepare_write_env, param);
            break;
        }
        case ESP_GATTS_EXEC_WRITE_EVT:
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            watcher_exec_write_event_env(&prepare_write_env, param);
            break;
        case ESP_GATTS_MTU_EVT:
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
            break;
        case ESP_GATTS_UNREG_EVT:
            break;
        case ESP_GATTS_CREATE_EVT:
            ESP_LOGI(GATTS_TAG, "CREATE_SERVICE_EVT, status %d,  service_handle %d", param->create.status, param->create.service_handle);

            gl_profile_tab[PROFILE_WATCHER_APP_ID].service_handle = param->create.service_handle;
            gl_profile_tab[PROFILE_WATCHER_APP_ID].char_uuid_read.len = ESP_UUID_LEN_128;
            gl_profile_tab[PROFILE_WATCHER_APP_ID].char_uuid_write.len = ESP_UUID_LEN_128;
            uint8_t char_uuid_write[ESP_UUID_LEN_128] = { 0xB3, 0x9B, 0x72, 0x34, 0xBE, 0xEC, 0xD4, 0xA8, 0xF4, 0x43, 0x41, 0x88, 0x43, 0x53, 0x53, 0x49 };
            uint8_t char_uuid_read[ESP_UUID_LEN_128] = { 0x16, 0x96, 0x24, 0x47, 0xC6, 0x23, 0x61, 0xBA, 0xD9, 0x4B, 0x4D, 0x1E, 0x43, 0x53, 0x53, 0x49 };
            for (int i = 0; i < ESP_UUID_LEN_128; i++)
            {
                gl_profile_tab[PROFILE_WATCHER_APP_ID].char_uuid_write.uuid.uuid128[i] = char_uuid_write[i];
            }
            for (int i = 0; i < ESP_UUID_LEN_128; i++)
            {
                gl_profile_tab[PROFILE_WATCHER_APP_ID].char_uuid_read.uuid.uuid128[i] = char_uuid_read[i];
            }

            esp_ble_gatts_start_service(gl_profile_tab[PROFILE_WATCHER_APP_ID].service_handle);

            watcher_write_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

            esp_err_t add_char_ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_WATCHER_APP_ID].service_handle, &gl_profile_tab[PROFILE_WATCHER_APP_ID].char_uuid_write, ESP_GATT_PERM_WRITE,
                watcher_write_property, &gatts_char_write_val, NULL);
            if (add_char_ret)
            {
                ESP_LOGE(GATTS_TAG, "add char1 failed, error code =%x", add_char_ret);
            }
            ESP_LOGI("add_WRITE_HANDL", "gl_profile_tab[PROFILE_WATCHER_APP_ID].cha_handl_attr: %d", param->add_char.attr_handle);
            watcher_read_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
            add_char_ret = esp_ble_gatts_add_char(gl_profile_tab[PROFILE_WATCHER_APP_ID].service_handle, &gl_profile_tab[PROFILE_WATCHER_APP_ID].char_uuid_read, ESP_GATT_PERM_READ,
                watcher_read_property, &gatts_char_read_val, NULL);
            ESP_LOGI("add_READ_HANDL", "gl_profile_tab[PROFILE_WATCHER_APP_ID].cha_handl_attr: %d", param->add_char.attr_handle);
            if (add_char_ret)
            {
                ESP_LOGE(GATTS_TAG, "add char2 failed, error code =%x", add_char_ret);
            }
            break;
        case ESP_GATTS_ADD_INCL_SRVC_EVT:
            break;
        case ESP_GATTS_ADD_CHAR_EVT: {
            ESP_LOGI(GATTS_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d", param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
            gl_profile_tab[PROFILE_WATCHER_APP_ID].char_handle = param->add_char.attr_handle;
            gl_profile_tab[PROFILE_WATCHER_APP_ID].char_handl_tx = param->add_char.attr_handle;
            gl_profile_tab[PROFILE_WATCHER_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
            gl_profile_tab[PROFILE_WATCHER_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

            esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_WATCHER_APP_ID].service_handle, &gl_profile_tab[PROFILE_WATCHER_APP_ID].descr_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL,
                NULL);
            break;
        }
        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            gl_profile_tab[PROFILE_WATCHER_APP_ID].descr_handle = param->add_char_descr.attr_handle;
            ESP_LOGI(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d", param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
            break;
        case ESP_GATTS_DELETE_EVT:
            break;
        case ESP_GATTS_START_EVT:
            ESP_LOGI(GATTS_TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
            break;
        case ESP_GATTS_STOP_EVT:
            break;
        case ESP_GATTS_CONNECT_EVT: {
            ble_status = BLE_CONNECTED;
            bool status = true;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, &status, 1, portMAX_DELAY);
            AT_command_reg();
            esp_ble_conn_update_params_t conn_params = { 0 };
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
            conn_params.latency = 0;
            conn_params.max_int = 0x20; // max_int = 0x20*1.25ms = 40ms
            conn_params.min_int = 0x20; // min_int = 0x10*1.25ms = 20ms
            conn_params.timeout = 400;  // timeout = 400*10ms = 4000ms
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:", param->connect.conn_id, param->connect.remote_bda[0], param->connect.remote_bda[1],
                param->connect.remote_bda[2], param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            gl_profile_tab[PROFILE_WATCHER_APP_ID].conn_id = param->connect.conn_id;
            esp_ble_gap_update_conn_params(&conn_params);
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
            AT_command_free();
            esp_ble_gap_start_advertising(&adv_params);
            ble_status = BLE_DISCONNECTED;
            bool status = false;
            esp_event_post_to(app_event_loop_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, &status, 1, portMAX_DELAY);
            break;
        case ESP_GATTS_CONF_EVT:
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONF_EVT, status %d attr_handle %d", param->conf.status, param->conf.handle);
            if (param->conf.status != ESP_GATT_OK)
            {
                esp_log_buffer_hex(GATTS_TAG, param->conf.value, param->conf.len);
            }
            break;
        case ESP_GATTS_OPEN_EVT:
        case ESP_GATTS_CANCEL_OPEN_EVT:
        case ESP_GATTS_CLOSE_EVT:
        case ESP_GATTS_LISTEN_EVT:
        case ESP_GATTS_CONGEST_EVT:
        default:
            break;
    }
}

/**
 * @brief GATT server event handler.
 *
 * This function handles GATT server events. When the ESP_GATTS_REG_EVT event occurs, it registers
 * the application with the GATT interface. For all other events, it iterates through the profile table
 * and calls the corresponding profile's event handler callback if the GATT interface matches.
 *
 * @param event The GATT server event type.
 * @param gatts_if The GATT interface handle. It can be ESP_GATT_IF_NONE to indicate all interfaces.
 * @param param Pointer to the structure containing the GATT callback parameters.
 */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT)
    {
        if (param->reg.status == ESP_GATT_OK)
        {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        }
        else
        {
            ESP_LOGI(GATTS_TAG, "Reg app failed, app_id %04x, status %d", param->reg.app_id, param->reg.status);
            return;
        }
    }
    do
    {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++)
        {
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[idx].gatts_if)
            {
                if (gl_profile_tab[idx].gatts_cb)
                {
                    gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    }
    while (0);
}

/**
 * @brief Initializes the BLE (Bluetooth Low Energy) application.
 *
 * This function performs the initialization of the BLE application. It includes setting up NVS (Non-Volatile Storage)
 * if BLE_DEBUG is defined, releasing memory for the classic Bluetooth, initializing and enabling the Bluetooth
 * controller and bluedroid stack, registering GATT and GAP callbacks, setting the local MTU, and starting the
 * BLE configuration task.
 *
 * @return esp_err_t The error code indicating the success or failure of the initialization.
 */
esp_err_t app_ble_init(void)
{
    esp_err_t ret;

#ifdef BLE_DEBUG
    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
#endif

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        ESP_LOGE(GATTS_TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
    }
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        ESP_LOGE(GATTS_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
    }

    ret = esp_bluedroid_init();
    if (ret)
    {
        ESP_LOGE(GATTS_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
    }
    ret = esp_bluedroid_enable();
    if (ret)
    {
        ESP_LOGE(GATTS_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
    }

    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret)
    {
        ESP_LOGE(GATTS_TAG, "gatts register error, error code = %x", ret);
    }
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret)
    {
        ESP_LOGE(GATTS_TAG, "gap register error, error code = %x", ret);
    }
    ret = esp_ble_gatts_app_register(PROFILE_WATCHER_APP_ID);
    if (ret)
    {
        ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret);
    }
    if (ret)
    {
        ESP_LOGE(GATTS_TAG, "gatts app register error, error code = %x", ret);
    }
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret)
    {
        ESP_LOGE(GATTS_TAG, "set local  MTU failed, error code = %x", local_mtu_ret);
    }
    AT_cmd_init();
    ble_task_stack = (StackType_t *)heap_caps_malloc(8192 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    TaskHandle_t task_handle = xTaskCreateStatic(ble_config_entry, "ble_config_entry", 8192, NULL, 4, ble_task_stack, &ble_task_buffer);
    if (task_handle == NULL)
    {
        printf("Failed to create task\n");
    }
    return ESP_OK;
}

/**
 * @brief Gets the current BLE status and sends it to the appropriate caller.
 *
 * This function checks the BLE status and sends the status to the specified caller.
 * If the caller is the UI, it sends the BLE status (connected or not connected) to the UI.
 * If the caller is AT command, it currently does nothing.
 *
 * @param caller The identifier of the caller requesting the BLE status.
 *               - UI_CALLER: Sends BLE status to the UI.
 *               - AT_CMD_CALLER: Currently not implemented.
 */
void set_ble_status(int caller, int status)
{
    switch (caller)
    {
        case UI_CALLER:
            ble_status = status;
            vTaskDelay(1000);
            break;
        case AT_CMD_CALLER:
            ble_status = status;
            break;
    }
}

/**
 * @brief BLE configuration task entry.
 *
 * This function runs as a task and handles the BLE configuration and status management.
 * It checks the BLE status and starts or stops advertising accordingly. A mutex is created
 * to ensure thread-safe access to the BLE status. The task runs in an infinite loop, periodically
 * checking and updating the BLE status.
 */
void ble_config_entry(void)
{
    esp_err_t ret;
    if (ble_status_mutex == NULL)
    {
        ble_status_mutex = xSemaphoreCreateMutex();
    }

    while (1)
    {
        
        if (ble_status == BLE_DISCONNECTED)
        {
            ret = esp_ble_gap_start_advertising(&adv_params);
            if (ret)
            {
                ESP_LOGE("BLE_BUTTON", "start advertising failed: %s", esp_err_to_name(ret));
            }
            else
            {
                ESP_LOGI("BLE_BUTTON", "start advertising succeeded");
            }
            ble_status = STATUS_WAITTING;
            //set_rgb_with_priority(AT_CMD_CALLER,breath_red);
        }
        else if (ble_status == BLE_CONNECTED)
        {
            ret = esp_ble_gap_stop_advertising();
            if (ret)
            {
                ESP_LOGE("BLE_BUTTON", "stop advertising failed: %s", esp_err_to_name(ret));
            }
            else
            {
                ESP_LOGI("BLE_BUTTON", "stop advertising succeeded");
            }
            ble_status = STATUS_WAITTING;
            release_rgb(AT_CMD_CALLER);
        }
        else
        {
            vTaskDelay(1000);
        }
    }
}
