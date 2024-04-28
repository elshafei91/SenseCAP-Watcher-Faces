#include <string.h>
#include <stddef.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"
#include "string.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_defs.h"
#include "esp_gatt_common_api.h"
#include "esp_bt_main.h"


#include "app_ble.h"

#define GATTS_TABLE_TAG "Watcher_BLE_Server"

#define CONFIG_SET_RAW_ADV_DATA // config set RAW_DATA

#define WATCHER_BLE_PROFILE_NUM 1
#define WATCHER_BLE_PROFILE_APP_IDX 0
#define WATCHER_BLE_APP_ID 0x56
#define DEVICE_NAME "-8888"     // The Device Name Characteristics in GAP
#define WATCHER_BLE_SVC_INST_ID 0 // UNKNOW

////The UUID of the Characteristic
// #define WATCHER_GATT_UUID_BLE_DATA_RECEIVE 0xABF2
// #define WATCHER_GATT_UUID_BLE_DATA_NOTIFY 0xABF3
// #define WATCHER_GATT_UUID_BLE_COMMAND_RECEIVE 0xABF4
// #define WATCHER_GATT_UUID_BLE_COMMAND_NOTIFY 0xABF5
// #define WATCHER_GATT_UUID_BLE_DATA_RECEIVE_CHAR 0xABF6

#ifdef CONFIG_SET_RAW_ADV_DATA
// static  uint8_t watcher_adv_data_RAW[23] = {
//     /* Complete List of 16-bit Service Class UUIDs */
//     0x05, 0x04, 0xA8, 0x86,0x28,0x86,
//     /* Complete Local Name in advertising */
//     0x08, 0x09, 'W','A','T','C','H','E','R'};
static uint8_t watcher_sn_buffer[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
void hexTonum(unsigned char *out_data, unsigned char *in_data, unsigned short Size)    //Tool Function
{
    for(unsigned char i = 0; i < Size; i++)
    {
        out_data[2*i] = (in_data[i]>>4);
        out_data[2*i+1] = (in_data[i]&0x0F);  
    }
    for(unsigned char i = 0; i < 2*Size; i++)
    {
        if ((out_data[i] >= 0) && (out_data[i] <= 9)) 
        {
            out_data[i] = '0'+out_data[i];
        } 
        else if ((out_data[i] >= 0x0A) && (out_data[i] <= 0x0F)) 
        {
            out_data[i] = 'A'- 10 +out_data[i];
        } 
        else 
        {
            return;
        }
    }
}
static uint8_t watcher_adv_data_RAW[] = {
    0x05, 0x03, 0x86, 0x28, 0x86, 0xA8,
    0x18, 0x09};
static uint8_t watcher_name[] = {'-', '8', '8', '8', '8'};
#else

// serveice_uuid_1
static uint8_t service_uuid_1[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    // first uuid, 16bit, [12],[13] is the value
    0xaa, 0xaa};
static uint8_t service_uuid_2[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    // first uuid, 16bit, [12],[13] is the value
    0xaa, 0xaa};
/// 31 Byte
static esp_ble_adv_data_t watcher_adv_data_1 = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(service_uuid_1),
    .p_service_uuid = service_uuid_1,
};

static esp_ble_adv_data_t watcher_adv_data_2 = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(service_uuid_2),
    .p_service_uuid = service_uuid_2,
};
#endif

static uint16_t watcher_ble_mtu_size = 23;

static uint16_t watcher_ble_conn_id = 0xffff;



QueueHandle_t watcher_ble_queue = NULL;

static bool enable_data_ntf = false;
static bool watcher_ble_is_connected = false;

static esp_bd_addr_t watcher_ble_remote_bda = {
    0x0,
};

static uint16_t watcher_ble_handle_table[WATCHER_BLE_IDX_NB];

// ADV param
/*
.adv_int_min and .adv_int_max set the minimum and maximum intervals for broadcasts respectively. These values are in units of 0.625ms,
So the settings here are 20ms to 40ms. This range determines how often the device broadcasts its presence. The smaller the interval, the more likely the device is to be discovered.
But it will also consume more power.
.adv_type sets the broadcast type to ADV_TYPE_IND,
This is a connectable, directed broadcast suitable for most BLE applications.
.own_addr_type sets the device address type to BLE_ADDR_TYPE_PUBLIC, which means using the public device address.
.own_addr_type sets the device address type to BLE_ADDR_TYPE_PUBLIC, which means using the public device address.
.channel_map sets the broadcast channel to ADV_CHNL_ALL, which means broadcasting on all broadcast channels.
.adv_filter_policy sets the filtering policy to ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY, which means any device is allowed to scan and connect.
*/
static esp_ble_adv_params_t watcher_ble_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// struct of gatts_profile_inst
/*
esp_gatts_cb_t gatts_cb;: This is a callback function used to handle GATT server events.
uint16_t gatts_if;: This is the interface ID of the GATT service.
uint16_t app_id;: This is the ID of the application.
uint16_t conn_id;: This is the ID of the connection.
uint16_t service_handle;: This is the handle of the GATT service.
esp_gatt_srvc_id_t service_id;: This is the ID of the GATT service.
uint16_t char_handle;: This is the handle of the characteristic in the GATT service.
esp_bt_uuid_t char_uuid;: This is the UUID of the characteristic in the GATT service.
esp_gatt_perm_t perm;: This is the permission of the feature in the GATT service.
esp_gatt_char_prop_t property;: This is the property of the characteristic in the GATT service.
uint16_t descr_handle;: This is the handle of the descriptor in the GATT service.
esp_bt_uuid_t descr_uuid;: This is the UUID of the descriptor in the GATT service.
*/
struct gatts_profile_inst
{
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};

// link list recv data
/*
len: the length of the data
node_buff: the data buffer
next_node: the next node
*/
typedef struct watcher_recv_node
{
    int32_t len;
    uint8_t *node_buff;
    struct watcher_recv_buffer *next_node;
} watcher_recv_data_node_t;

static watcher_recv_data_node_t *temp_node_p1 = NULL;
static watcher_recv_data_node_t *temp_node_p2 = NULL;
// STATUS 1 --LL
typedef struct watcher_receive_data_buff
{
    int32_t node_num;
    int32_t buff_size;
    watcher_recv_data_node_t *first_node;
} watcher_recv_data_buff_t;

static watcher_recv_data_buff_t Watcher_recv_data_buffer = {
    .node_num = 0,
    .buff_size = 0,
    .first_node = NULL};

// status 2 raw_buffer
typedef struct
{
    uint8_t *prepare_buf;
    int prepare_len;
} prepare_type_env_t;

static prepare_type_env_t prepare_write_env;

// event handle and static function declaration
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);                 //  GATT Server callback function
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);                                           // GAP callback function
static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);         // GATT Server callback function
static void watcher_ble_task();                                                                                                       // BLE task
static void write_exec(uint16_t status);                                                                                       // Test_interface of custom_write_exec
void watcher_prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param); // Prepare the write event environment
void watcher_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);                            // Execute the write event environment

static struct gatts_profile_inst watcher_ble_profile_tab[WATCHER_BLE_PROFILE_NUM] = {
    [WATCHER_BLE_PROFILE_APP_IDX] = {
        .gatts_cb = gatts_profile_event_handler, // callback function handle
        .gatts_if = ESP_GATT_IF_NONE,
        /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};

/*
 *  WATCHER_BLE PROFILE ATTRIBUTES
 ****************************************************************************************
 */

/* Service */
// For watcher TASK
//  MAIN Test Space is Test_A
static const uint16_t watcher_BLE_SERVICE_UUID_TEST = CONFIG_watcher_BLE_SERVICE_UUID_TEST;
static const uint16_t watcher_BLE_CHAR_UUID_TEST_A = CONFIG_watcher_BLE_CHAR_UUID_TEST_A;
static const uint16_t watcher_BLE_CHAR_UUID_TEST_B = CONFIG_watcher_BLE_CHAR_UUID_TEST_B;
static const uint16_t watcher_BLE_CHAR_UUID_TEST_C = CONFIG_watcher_BLE_CHAR_UUID_TEST_C;

// BASE BLE Task
static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;

static const uint8_t char_prop_read = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE;
// static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read_write_notify = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t task_1_cache[2] = {0x00, 0x00};
// static const uint8_t heart_measurement_ccc[2] = {0x00, 0x00};         //UNKNOW
static const uint8_t char_value[4] = {0x11, 0x22, 0x33, 0x44};

static const esp_gatts_attr_db_t gatt_db[WATCHER_BLE_IDX_NB] =
    {
        // Service Declaration
        [WATCHER_BLE_IDX_SVC] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&primary_service_uuid, ESP_GATT_PERM_READ, sizeof(uint16_t), sizeof(watcher_BLE_SERVICE_UUID_TEST), (uint8_t *)&watcher_BLE_SERVICE_UUID_TEST}},

        /* Characteristic Declaration */
        [watcher_ble_IDX_CHAR_A] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write_notify}},

        /* Characteristic Value */
        [watcher_ble_IDX_CHAR_VAL_A] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&watcher_BLE_CHAR_UUID_TEST_A, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, WATCHER_BLE_DATA_MAX_LEN, sizeof(char_value), (uint8_t *)char_value}},

        /* Client Characteristic Configuration Descriptor */
        [watcher_ble_IDX_CHAR_CFG_A] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, sizeof(uint16_t), sizeof(task_1_cache), (uint8_t *)task_1_cache}},

        /* Characteristic Declaration */
        [watcher_ble_IDX_CHAR_B] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read}},

        /* Characteristic Value */
        [watcher_ble_IDX_CHAR_VAL_B] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&watcher_BLE_CHAR_UUID_TEST_B, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, WATCHER_BLE_DATA_MAX_LEN, sizeof(char_value), (uint8_t *)char_value}},

        /* Characteristic Declaration */
        [watcher_ble_IDX_CHAR_C] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_write}},

        /* Characteristic Value */
        [watcher_ble_IDX_CHAR_VAL_C] =
            {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&watcher_BLE_CHAR_UUID_TEST_C, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, WATCHER_BLE_DATA_MAX_LEN, sizeof(char_value), (uint8_t *)char_value}},

};

static void write_exec(uint16_t status)
{
    if (status == 0x0c)
    {
        printf("Writed 0\n");
    }
    else
    {
        printf("Write check\n");
    }
    printf("Write executing\n");
}

esp_err_t app_ble_init(void)
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT(); // Assignment controller_config

#ifdef BLE_DEBUG
    // Initialize NVS
    ret = nvs_flash_init(); // init NVS FLASH
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
#endif

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)); // release classic BLE

    ret = esp_bt_controller_init(&bt_cfg); // init controller_config
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE); // enable BT_mode
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ESP_LOGI(GATTS_TABLE_TAG, "%s init bluetooth", __func__);
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg); // init BLE stack
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return ESP_FAIL;
    }
    ret = esp_bluedroid_enable(); // enable BLE stack
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return ESP_FAIL;
    }
    ret = esp_ble_gatts_register_callback(gatts_event_handler); // register gatts_event_handler
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s gatts register failed: %s,ERROR AT LINE: %d", __func__, esp_err_to_name(ret), __LINE__);
        return ESP_FAIL;
    }

    ret = esp_ble_gap_register_callback(gap_event_handler); // register gap event
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s gap register failed: %s", __func__, esp_err_to_name(ret));
        return ESP_FAIL;
    }

    ret = esp_ble_gatts_app_register(WATCHER_BLE_APP_ID); // register gatts app
    if (ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s gatts app register failed: %s", __func__, esp_err_to_name(ret));
        return ESP_FAIL;
    }
    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500); // set local MTU
    if (local_mtu_ret)
    {
        ESP_LOGE(GATTS_TABLE_TAG, "%s set local  MTU failed: %s", __func__, esp_err_to_name(local_mtu_ret));
    }
    xTaskCreate(watcher_ble_task, "watcher_ble_task", 4096 * 5, NULL, 10, NULL); // create BLE task
    return ESP_OK;
}
void watcher_ble_task()
{
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// Function to handle prepare write events from a BLE GATT client
void watcher_prepare_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    // Log the details of the prepare write event
    ESP_LOGI(GATTS_TABLE_TAG, "prepare write, handle = %d, value len = %d", param->write.handle, param->write.len);

    // Initialize the status to OK
    esp_gatt_status_t status = ESP_GATT_OK;

    // Check if the offset provided is greater than the allowed buffer size
    if (param->write.offset > WATCHER_BLE_DATA_BUFF_MAX_LEN)
    {
        status = ESP_GATT_INVALID_OFFSET; // Set status as invalid offset
    }
    // Check if the sum of offset and length exceeds the buffer size
    else if ((param->write.offset + param->write.len) > WATCHER_BLE_DATA_BUFF_MAX_LEN)
    {
        status = ESP_GATT_INVALID_ATTR_LEN; // Set status as invalid attribute length
    }

    // If status is OK and no buffer is allocated yet
    if (status == ESP_GATT_OK && prepare_write_env->prepare_buf == NULL)
    {
        // Allocate memory for the buffer
        prepare_write_env->prepare_buf = (uint8_t *)malloc(WATCHER_BLE_DATA_BUFF_MAX_LEN * sizeof(uint8_t));
        // Reset prepare length to zero
        prepare_write_env->prepare_len = 0;

        // Check if memory allocation failed
        if (prepare_write_env->prepare_buf == NULL)
        {
            // Log an error message
            ESP_LOGE(GATTS_TABLE_TAG, "%s, Gatt_server prep no mem", __func__);
            status = ESP_GATT_NO_RESOURCES; // Set status as no resources
        }
    }

    // Send a response if needed
    if (param->write.need_rsp)
    {
        // Allocate memory for the response structure
        esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
        if (gatt_rsp != NULL)
        {
            // Set up the response with the current write values
            gatt_rsp->attr_value.len = param->write.len;
            gatt_rsp->attr_value.handle = param->write.handle;
            gatt_rsp->attr_value.offset = param->write.offset;
            gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            // Copy the value to the response structure
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            // Send the response
            esp_err_t response_err = esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            if (response_err != ESP_OK)
            {
                // Log an error if the response sending failed
                ESP_LOGE(GATTS_TABLE_TAG, "Send response error");
            }
            // Free the response structure
            free(gatt_rsp);
        }
        else
        {
            // Log an error if memory allocation for the response failed
            ESP_LOGE(GATTS_TABLE_TAG, "%s, malloc failed", __func__);
            status = ESP_GATT_NO_RESOURCES;
        }
    }
    // If there's no error, copy the value to the buffer
    if (status != ESP_GATT_OK)
    {
        return;
    }
    memcpy(prepare_write_env->prepare_buf + param->write.offset,
           param->write.value,
           param->write.len);
    prepare_write_env->prepare_len += param->write.len;
}

// Function to handle execute write events  not matter anything
void watcher_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    // If the flag indicates execute write
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC && prepare_write_env->prepare_buf)
    {
        // Log the prepared buffer as hex
        esp_log_buffer_hex(GATTS_TABLE_TAG, prepare_write_env->prepare_buf, prepare_write_env->prepare_len);
    }
    else
    {
        // Log that the prepare write was cancelled
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATT_PREP_WRITE_CANCEL");
    }
    // Free the buffer if allocated
    if (prepare_write_env->prepare_buf)
    {
        free(prepare_write_env->prepare_buf);
        prepare_write_env->prepare_buf = NULL;
    }
    // Reset prepare length to zero
    prepare_write_env->prepare_len = 0;
}

static void gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event)
    {
    case ESP_GATTS_REG_EVT:
    { // 注册事件
        uint8_t watcher_sn_buffer_t[18]={0};  
        hexTonum(watcher_sn_buffer_t,watcher_sn_buffer,sizeof(watcher_sn_buffer));
        uint8_t send_buffer[32]={0};
        uint8_t device_buffer[32]={0};

        uint8_t *ptr = send_buffer;
        memcpy(ptr, watcher_adv_data_RAW, sizeof(watcher_adv_data_RAW));
        ptr += sizeof(watcher_adv_data_RAW);
        memcpy(ptr, watcher_sn_buffer_t, 18);
        ptr += sizeof(watcher_sn_buffer_t);
        memcpy(ptr, watcher_name, 5); 
        
        printf("BLE_ADV_NAME is %s\n",&send_buffer[8]);
        memcpy(device_buffer,&send_buffer[8],23);
        esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name((const char *) device_buffer);
        if (set_dev_name_ret)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "set device name failed, error code = %x", set_dev_name_ret);
        }
#ifdef CONFIG_SET_RAW_ADV_DATA

        esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(send_buffer, sizeof(send_buffer)-1);
        

        if (raw_adv_ret)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "config raw adv data failed, error code = %x ", raw_adv_ret);
        }
        // raw_adv_ret = esp_ble_gap_config_adv_data_raw(watcher_adv_data_RAW_2, sizeof(watcher_adv_data_RAW_2));
        // if (raw_adv_ret)
        // {
        //     ESP_LOGE(GATTS_TABLE_TAG, "config raw adv2 data failed, error code = %x ", raw_adv_ret);
        // }
        // esp_err_t raw_scan_ret = esp_ble_gap_config_scan_rsp_data_raw(raw_scan_rsp_data, sizeof(raw_scan_rsp_data));
        // if (raw_scan_ret)
        // {
        //     ESP_LOGE(GATTS_TABLE_TAG, "config raw scan rsp data failed, error code = %x", raw_scan_ret);
        // }
#else
        // config adv data
        esp_err_t ret = esp_ble_gap_config_adv_data(&watcher_adv_data_1);
        if (ret)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "config adv data failed, error code = %x", ret);
        }
        // config scan response data
        ret = esp_ble_gap_config_adv_data(&watcher_adv_data_2);
        if (ret)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "config scan response data failed, error code = %x", ret);
        }
#endif
        esp_err_t create_attr_ret = esp_ble_gatts_create_attr_tab(gatt_db, gatts_if, WATCHER_BLE_IDX_NB, WATCHER_BLE_SVC_INST_ID);
        if (create_attr_ret)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "create attr table failed, error code = %x", create_attr_ret);
        }
    }
    break;
    case ESP_GATTS_READ_EVT: // 读事件
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_READ_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);
        // ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %" PRIu32 ", handle %d", param->read.conn_id, param->read.trans_id, param->read.handle);
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 4;
        rsp.attr_value.value[0] = 0xde;
        rsp.attr_value.value[1] = 0xed;
        rsp.attr_value.value[2] = 0xbe;
        rsp.attr_value.value[3] = 0xef;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
        break;

        break;
    case ESP_GATTS_WRITE_EVT: // 写事件
        if (watcher_ble_handle_table[watcher_ble_IDX_CHAR_A] == param->write.handle)
        {
            if (param->write.len == 1)
            {
                write_exec((int)param->write.value[0]);
                printf("recv:%d", param->write.value[0]);
            }
        }
        if (!param->write.is_prep)
        {
            // the data length of gattc write  must be less than GATTS_DEMO_CHAR_VAL_LEN_MAX.
            ESP_LOGI(GATTS_TABLE_TAG, "GATT_WRITE_EVT, handle = %d, value len = %d, value :", param->write.handle, param->write.len);
            esp_log_buffer_hex(GATTS_TABLE_TAG, param->write.value, param->write.len);
            if (watcher_ble_handle_table[watcher_ble_IDX_CHAR_CFG_A] == param->write.handle && param->write.len == 2)
            {
                uint16_t descr_value = param->write.value[1] << 8 | param->write.value[0];
                if (descr_value == 0x0001)
                {
                    watcher_ble_profile_tab[watcher_ble_IDX_CHAR_VAL_A].conn_id = param->write.conn_id;
                }
                else if (descr_value == 0x0002)
                {
                    uint8_t indicate_data[15];
                    for (int i = 0; i < sizeof(indicate_data); ++i)
                    {
                        indicate_data[i] = i % 0xff;
                    }
                    esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, watcher_ble_handle_table[watcher_ble_IDX_CHAR_VAL_A],
                                                sizeof(indicate_data), indicate_data, true);
                }
                else if (descr_value == 0x0000)
                {
                    printf("des 0000");
                }
                else
                {
                    ESP_LOGE(GATTS_TABLE_TAG, "unknown descr value");
                    esp_log_buffer_hex(GATTS_TABLE_TAG, param->write.value, param->write.len);
                }
            }
            /* send response when param->write.need_rsp is true*/
            if (param->write.need_rsp)
            {
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
            }
        }
        else
        {
            /* handle prepare write */
            watcher_prepare_write_event_env(gatts_if, &prepare_write_env, param);
        }
        break;
    case ESP_GATTS_EXEC_WRITE_EVT:
        // the length of gattc prepare write data must be less than GATTS_DEMO_CHAR_VAL_LEN_MAX.
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_EXEC_WRITE_EVT");
        watcher_exec_write_event_env(&watcher_prepare_write_event_env, param);
        break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_CONF_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONF_EVT, status = %d, attr_handle %d", param->conf.status, param->conf.handle);
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "SERVICE_START_EVT, status %d, service_handle %d", param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_CONNECT_EVT, conn_id = %d", param->connect.conn_id);
        esp_log_buffer_hex(GATTS_TABLE_TAG, param->connect.remote_bda, 6);
        esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        /* For the iOS system, please refer to Apple official documents about the BLE connection parameters restrictions. */
        conn_params.latency = 0;
        conn_params.max_int = 0x20; // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10; // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 400;  // timeout = 400*10ms = 4000ms
        // start sent the update connection parameters to the peer device.
        esp_ble_gap_update_conn_params(&conn_params);
        break;
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(GATTS_TABLE_TAG, "ESP_GATTS_DISCONNECT_EVT, reason = 0x%x", param->disconnect.reason);
        esp_ble_gap_start_advertising(&watcher_ble_adv_params);
        break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
    {
        if (param->add_attr_tab.status != ESP_GATT_OK)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "create attribute table failed, error code=0x%x", param->add_attr_tab.status);
        }
        else if (param->add_attr_tab.num_handle != WATCHER_BLE_IDX_NB)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "create attribute table abnormally, num_handle (%d) \
                        doesn't equal to HRS_IDX_NB(%d)",
                     param->add_attr_tab.num_handle, WATCHER_BLE_IDX_NB);
        }
        else
        {
            ESP_LOGI(GATTS_TABLE_TAG, "create attribute table successfully, the number handle = %d", param->add_attr_tab.num_handle);
            memcpy(watcher_ble_handle_table, param->add_attr_tab.handles, sizeof(watcher_ble_handle_table));
            esp_ble_gatts_start_service(watcher_ble_handle_table[WATCHER_BLE_IDX_SVC]);
        }
        break;
    }
    case ESP_GATTS_STOP_EVT:
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    case ESP_GATTS_UNREG_EVT:
    case ESP_GATTS_DELETE_EVT:
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
            watcher_ble_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if; // 不同的客户端有不同的接口从底层上传
        }
        else
        {
            ESP_LOGE(GATTS_TABLE_TAG, "reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }
    do
    {
        int idx;
        for (idx = 0; idx < PROFILE_NUM; idx++)
        {
            /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
            if (gatts_if == ESP_GATT_IF_NONE || gatts_if == watcher_ble_profile_tab[idx].gatts_if)
            {
                if (watcher_ble_profile_tab[idx].gatts_cb)
                {
                    watcher_ble_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    esp_err_t err;
#ifdef BLE_DEBUG
    ESP_LOGE(GATTS_TABLE_TAG, "GAP_EVT, event %d", event);
#endif
    switch (event)
    {
#ifdef CONFIG_SET_RAW_ADV_DATA
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: // set raw complete
        esp_ble_gap_start_advertising(&watcher_ble_adv_params);
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT: // raw_adv set complete
        esp_ble_gap_start_advertising(&watcher_ble_adv_params);
        break;

#else
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT: // set adv data complete
        esp_ble_gap_start_advertising(&watcher_ble_adv_params);
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT: // set scan response data complete
        esp_ble_gap_start_advertising(&watcher_ble_adv_params);
        break;
#endif
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT: // start adv finish
        if ((err = param->adv_start_cmpl.status) != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "Advertising start failed: %s", esp_err_to_name(err)); // 判断广播是否完成
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT: // stop adv finish
        if ((err = param->adv_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS)
        {
            ESP_LOGE(GATTS_TABLE_TAG, "Advertising stop failed: %s", esp_err_to_name(err)); // 判断广播是否停止
        }
        else
        {
            ESP_LOGI(GATTS_TABLE_TAG, "Stop adv successfully\n");
        }
        break;
#ifdef BLE_DEBUG
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT: // 更新连接数参数事件
        ESP_LOGI(GATTS_TABLE_TAG, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                 param->update_conn_params.status,
                 param->update_conn_params.min_int,
                 param->update_conn_params.max_int,
                 param->update_conn_params.conn_int,
                 param->update_conn_params.latency,
                 param->update_conn_params.timeout);
        break;
#endif
    default:
        break;
    }
}
