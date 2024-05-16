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

#include "at_cmd.h"
#include "app_ble.h"
#include "system_layer.h"
#include "app_device_info.h"
#include "app_wifi.h"
#include "event_loops.h"
#include "data_defs.h"

/*-----------------------------------------------------------------------------------*/
// variable defination place

static int ble_status = BLE_DISCONNECTED;

uint8_t watcher_sn_buffer[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
uint8_t watcher_name[] = { '-', 'W', 'A', 'C', 'H' };

static uint8_t char1_str[] = { 0x11, 0x22, 0x33 };
static uint8_t char2_str[] = { 0x44, 0x55, 0x66 };

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

uint8_t adv_config_done = 0;

uint8_t watcher_adv_data_RAW[] = { 0x05, 0x03, 0x86, 0x28, 0x86, 0xA8, 0x18, 0x09 };

uint8_t raw_scan_rsp_data[] = { // Length 15, Data Type 9 (Complete Local Name), Data 1 (ESP_GATTS_DEMO)
    0x06, 0x09, '-', 'W', 'A', 'C', 'H'
};

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

typedef struct
{
    uint8_t *prepare_buf;
    int prepare_len;
} prepare_type_env_t;

prepare_type_env_t prepare_write_env;

prepare_type_env_t tiny_write_env;

esp_ble_adv_params_t adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = { [PROFILE_WATCHER_APP_ID] = {
                                                              .gatts_cb = gatts_profile_event_handler,
                                                              .gatts_if = ESP_GATT_IF_NONE,
                                                          } };

//  watcher service property
esp_gatt_char_prop_t watcher_write_property = 0;
esp_gatt_char_prop_t watcher_read_property = 1;

SemaphoreHandle_t ble_status_mutex = NULL;

void ble_config_layer(void);
static void watcher_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
static void watcher_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);
//  tool function
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

static void watcher_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    esp_gatt_status_t status = ESP_GATT_OK;
    if (param->write.need_rsp)
    {
        if (param->write.is_prep)
        {
            ESP_LOGE(GATTS_TAG, "CHECK_POINT");
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
                ESP_LOGE(GATTS_TAG, "CHECK_POINT01");
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
static void watcher_exec_write_tiny_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT_TAG_TINY, value len %d, value :", param->write.len);
    esp_log_buffer_hex("HEX_TAG_tiny", prepare_write_env->prepare_buf, prepare_write_env->prepare_len);

    if (prepare_write_env->prepare_buf)
    {
        message_event_t msg_at = { .msg = prepare_write_env->prepare_buf, .size = prepare_write_env->prepare_len };
        esp_log_buffer_hex("HEX TAG2", msg_at.msg, msg_at.size);
        esp_event_post_to(at_event_loop_handle, AT_EVENTS, AT_EVENTS_COMMAND_ID, &msg_at, msg_at.size, portMAX_DELAY);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
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

                // Calculate the number of full segments and remaining bytes
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
        message_event_t msg_at = { .msg = prepare_write_env->prepare_buf, .size = prepare_write_env->prepare_len };
        esp_log_buffer_hex("HEX TAG2", msg_at.msg, msg_at.size);
        esp_event_post_to(at_event_loop_handle, AT_EVENTS, AT_EVENTS_COMMAND_ID, &msg_at, sizeof(msg_at), portMAX_DELAY);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;

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

                // Calculate the number of full segments and remaining bytes
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
                ESP_LOGE(GATTS_TABLE_TAG, "set device name failed, error code = %x", set_dev_name_ret);
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
#ifdef CONFIG_SET_RAW_ADV_DATA

            esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(send_buffer, sizeof(send_buffer) - 1);
            if (raw_adv_ret)
            {
                ESP_LOGE(GATTS_TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
            }
#else
            // config adv data
            esp_err_t ret = esp_ble_gap_config_adv_data(&adv_data);
            if (ret)
            {
                ESP_LOGE(GATTS_TAG, "config adv data failed, error code = %x", ret);
            }
            adv_config_done |= adv_config_flag;
            // config scan response data
            ret = esp_ble_gap_config_adv_data(&scan_rsp_data);
            if (ret)
            {
                ESP_LOGE(GATTS_TAG, "config scan response data failed, error code = %x", ret);
            }
            adv_config_done |= scan_rsp_config_flag;

#endif
            esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_WATCHER_APP_ID].service_id, GATTS_NUM_HANDLE_TEST_A);
            break;
        case ESP_GATTS_READ_EVT: {
            ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);
            esp_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
            rsp.attr_value.handle = param->read.handle;
            rsp.attr_value.len = 4;
            rsp.attr_value.value[0] = 0xde;
            rsp.attr_value.value[1] = 0xed;
            rsp.attr_value.value[2] = 0xbe;
            rsp.attr_value.value[3] = 0xef;
            esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            break;
        }
        case ESP_GATTS_WRITE_EVT: {
            ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);

            if (!param->write.is_prep)
            {
                ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT_TAG, value len %d, value :", param->write.len);
                esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);

                if (param->write.len == 2)
                {
                    uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                    if (descr_value == 0x0001)
                    {
                        ESP_LOGE(GATTS_TAG, "die 01");
                        if (watcher_write_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY)
                        {
                            ESP_LOGI(GATTS_TAG, "notify enable");
                            uint8_t notify_data[15];
                            for (int i = 0; i < sizeof(notify_data); ++i)
                            {
                                notify_data[i] = i % 0xff;
                            }
                            // the size of notify_data[] need less than MTU size
                            // esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_WATCHER_APP_ID].char_handle, sizeof(notify_data), notify_data, false);
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
                            // the size of indicate_data[] need less than MTU size
                            // esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_WATCHER_APP_ID].char_handle, sizeof(indicate_data), indicate_data, true);
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
                    ESP_LOGE(GATTS_TAG, "handle is %x", param->write.handle);
                    ESP_LOGE(GATTS_TAG, "handle is %x", gl_profile_tab[PROFILE_WATCHER_APP_ID].descr_handle);
                    tiny_write_env.prepare_buf = malloc(TINY_BUF_MAX_SIZE * sizeof(uint8_t));
                    memcpy(tiny_write_env.prepare_buf, param->write.value, param->write.len);
                    tiny_write_env.prepare_len = param->write.len;
                    ESP_LOGE(GATTS_TAG, "die 02");
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
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, &status, 1, portMAX_DELAY);
            AT_command_reg();
            esp_ble_conn_update_params_t conn_params = { 0 };
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            /* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
            conn_params.latency = 0;
            conn_params.max_int = 0x20; // max_int = 0x20*1.25ms = 40ms
            conn_params.min_int = 0x10; // min_int = 0x10*1.25ms = 20ms
            conn_params.timeout = 400;  // timeout = 400*10ms = 4000ms
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:", param->connect.conn_id, param->connect.remote_bda[0], param->connect.remote_bda[1],
                param->connect.remote_bda[2], param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
            gl_profile_tab[PROFILE_WATCHER_APP_ID].conn_id = param->connect.conn_id;
            // start sent the update connection parameters to the peer device.
            esp_ble_gap_update_conn_params(&conn_params);
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT:
            ESP_LOGI(GATTS_TAG, "ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
            AT_command_free();
            esp_ble_gap_start_advertising(&adv_params);
            ble_status = BLE_DISCONNECTED;
            bool status = false;
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, &status, 1, portMAX_DELAY);
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

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    /* If event is register event, store the gatts_if for each profile */
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

    /* If the gatts_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do
    {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++)
        {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                gatts_if == gl_profile_tab[idx].gatts_if)
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
    xTaskCreate(ble_config_layer, "ble_config_layer", 4096, NULL, 4, NULL);

#ifdef DEBUG_AT_CMD
    // xTaskCreate(vTaskMonitor, "TaskMonitor", 1024 * 10, NULL, 2, NULL);                      // check status of all tasks while  task_handle_AT_command is running
#endif
    return ESP_OK;
}

// stop ble system

void app_ble_deinit(void)
{
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
}

void app_ble_start(void)
{
    ESP_LOGE("app_ble_start", "");
    esp_ble_gap_start_advertising(&adv_params);
}

void app_ble_stop(void)
{
    ESP_LOGE("app_ble_stop", "");
    esp_ble_gap_stop_advertising();
}

void get_ble_status(int caller)
{
    switch (caller)
    {
        case UI_CALLER: {
            if (ble_status == BLE_CONNECTED)
            {
                // send BLE_CONNECTED to UI
                bool status = true;
                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, &status, 1, portMAX_DELAY);
            }
            else
            {
                bool status = false;
                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BLE_STATUS, &status, 1, portMAX_DELAY);
            }
            break;
        }
        case AT_CMD_CALLER: {
            break;
        }
    }
}

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

void ble_config_layer(void)
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
            ble_status= STATUS_WAITTING;
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
            ble_status= STATUS_WAITTING;
        }
        else{
            vTaskDelay(1000);
        }
    }
    vTaskDelay(100);
}
