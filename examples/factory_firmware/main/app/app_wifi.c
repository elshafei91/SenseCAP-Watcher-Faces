#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOSConfig.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "ping/ping_sock.h"

#include "app_wifi.h"
#include "data_defs.h"
#include "event_loops.h"
#include "system_layer.h"
#include "at_cmd.h"

#define WIFI_CONFIG_LAYER_STACK_SIZE 10240
#define WIFI_CONNECTED_BIT           BIT0
#define WIFI_FAIL_BIT                BIT1

struct app_wifi
{
    struct view_data_wifi_st st;
    bool is_cfg;
    int wifi_reconnect_cnt;
};

static struct app_wifi _g_wifi_cfg;
static SemaphoreHandle_t __g_wifi_mutex;
static SemaphoreHandle_t __g_data_mutex;
static SemaphoreHandle_t __g_net_check_sem;

static int s_retry_num = 0;
static int wifi_retry_max = 3;
static bool __g_ping_done = true;

static EventGroupHandle_t __wifi_event_group;

static const char *TAG = "app-wifi";

static int min(int a, int b)
{
    return (a < b) ? a : b;
}

static void __wifi_st_set(struct view_data_wifi_st *p_st)
{
    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    memcpy(&_g_wifi_cfg.st, p_st, sizeof(struct view_data_wifi_st));
    xSemaphoreGive(__g_data_mutex);
}

static void __wifi_st_get(struct view_data_wifi_st *p_st)
{
    xSemaphoreTake(__g_data_mutex, portMAX_DELAY);
    memcpy(p_st, &_g_wifi_cfg.st, sizeof(struct view_data_wifi_st));
    xSemaphoreGive(__g_data_mutex);
}

static void __wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
        case WIFI_EVENT_STA_START: {
            ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_START");
            struct view_data_wifi_st st;
            st.is_connected = false;
            st.is_network = false;
            st.is_connecting = true;
            memset(st.ssid, 0, sizeof(st.ssid));
            st.rssi = 0;
            __wifi_st_set(&st);

            esp_wifi_connect();
            break;
        }
        case WIFI_EVENT_STA_CONNECTED: {
            ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_CONNECTED");
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            struct view_data_wifi_st st;

            __wifi_st_get(&st);
            memset(st.ssid, 0, sizeof(st.ssid));
            memcpy(st.ssid, event->ssid, event->ssid_len);
            st.rssi = -50; // todo
            st.is_connected = true;
            st.is_connecting = false;
            __wifi_st_set(&st);

            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), portMAX_DELAY);

            struct view_data_wifi_connet_ret_msg msg;
            msg.ret = 0;
            strcpy(msg.msg, "Connection successful");
            esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg), portMAX_DELAY);
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED: {
            ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_DISCONNECTED");

            if ((wifi_retry_max == -1) || s_retry_num < wifi_retry_max)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
            }
            else
            {
                // update list  todo
                struct view_data_wifi_st st;

                __wifi_st_get(&st);
                st.is_connected = false;
                st.is_network = false;
                st.is_connecting = false;
                __wifi_st_set(&st);

                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), portMAX_DELAY);

                char *p_str = "";
                struct view_data_wifi_connet_ret_msg msg;
                msg.ret = 0;
                strcpy(msg.msg, "Connection failure");
                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg), portMAX_DELAY);
            }
            break;
        }
        default:
            break;
    }
}

static void __ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;

        // xEventGroupSetBits(__wifi_event_group, WIFI_CONNECTED_BIT);
        xSemaphoreGive(__g_net_check_sem); // goto check network
    }
}

TaskHandle_t xTask_wifi_config_layer;
static int __wifi_scan()
{
    wifi_ap_record_t *p_ap_info = (wifi_ap_record_t *)heap_caps_malloc(5 * sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM);
    uint16_t number = 5;
    uint16_t ap_count=0;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_err_t ret=esp_wifi_scan_start(NULL, true);
    if(ret!=ESP_OK)
    {
        ESP_LOGE(TAG, "wifi scan start failed");
        return -1;
    }
    else{
        ESP_LOGI(TAG, "wifi scan start success");
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, p_ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, " scan ap cont: %d", ap_count);

    struct view_data_wifi_st wifi_table_element;
    for (int i = 0; (i < number) && (i < ap_count); i++)
    {
        ESP_LOGI(TAG, "SSID: %s, RSSI:%d, Channel: %d", p_ap_info[i].ssid, p_ap_info[i].rssi, p_ap_info[i].primary);
        wifi_table_element.rssi = p_ap_info[i].rssi;
        wifi_table_element.is_connected = false;
        wifi_table_element.is_network = false;
        wifi_table_element.is_connecting = false;
        wifi_table_element.authmode = p_ap_info[i].authmode;
        strcpy(wifi_table_element.ssid, (char *)p_ap_info[i].ssid); // 是否能对齐
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST, &wifi_table_element, sizeof(struct view_data_wifi_st), portMAX_DELAY);
    }
    return ap_count;
}

static int __wifi_connect(const char *p_ssid, const char *p_password, int retry_num)
{
    wifi_retry_max = retry_num; // todo
    s_retry_num = 0;

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, p_ssid, sizeof(wifi_config.sta.ssid));
    ESP_LOGI(TAG, "ssid: %s", p_ssid);
    if (p_password)
    {
        ESP_LOGI(TAG, "password: %s", p_password);
        strlcpy((char *)wifi_config.sta.password, p_password, sizeof(wifi_config.sta.password));
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // todo
    }
    else
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    _g_wifi_cfg.is_cfg = true;

    struct view_data_wifi_st st = { 0 };
    st.is_connected = false;
    st.is_connecting = false;
    st.is_network = false;
    __wifi_st_set(&st);

    ESP_ERROR_CHECK(esp_wifi_start());
    // esp_wifi_connect();

    ESP_LOGI(TAG, "connect...");

    return ESP_OK;
}

static void __wifi_cfg_restore(void)
{
    _g_wifi_cfg.is_cfg = false;

    struct view_data_wifi_st st = { 0 };
    st.is_connected = false;
    st.is_connecting = false;
    st.is_network = false;
    __wifi_st_set(&st);

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), portMAX_DELAY);

    // restore and stop
    esp_wifi_restore();
}

static void __wifi_shutdown(void)
{
    _g_wifi_cfg.is_cfg = false; // disable reconnect

    struct view_data_wifi_st st = { 0 };
    st.is_connected = false;
    st.is_connecting = false;
    st.is_network = false;
    __wifi_st_set(&st);

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), portMAX_DELAY);

    esp_wifi_stop();
}

static void __ping_end(esp_ping_handle_t hdl, void *args)
{
    ip_addr_t target_addr;
    uint32_t transmitted = 0;
    uint32_t received = 0;
    uint32_t total_time_ms = 0;
    uint32_t loss = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));

    if (transmitted > 0)
    {
        loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
    }
    else
    {
        loss = 100;
    }

    if (IP_IS_V4(&target_addr))
    {
        printf("\n--- %s ping statistics ---\n", inet_ntoa(*ip_2_ip4(&target_addr)));
    }
    else
    {
        printf("\n--- %s ping statistics ---\n", inet6_ntoa(*ip_2_ip6(&target_addr)));
    }
    printf("%ld packets transmitted, %ld received, %ld%% packet loss, time %ldms\n", transmitted, received, loss, total_time_ms);

    esp_ping_delete_session(hdl);

    struct view_data_wifi_st st;
    if (received > 0)
    {
        __wifi_st_get(&st);
        st.is_network = true;
        __wifi_st_set(&st);
    }
    else
    {
        __wifi_st_get(&st);
        st.is_network = false;
        __wifi_st_set(&st);
    }
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st), portMAX_DELAY);
    __g_ping_done = true;
}

static void __ping_start(void)
{
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();

    ip_addr_t target_addr;
    ipaddr_aton(PING_TEST_IP, &target_addr);

    config.target_addr = target_addr;

    esp_ping_callbacks_t cbs = { .cb_args = NULL, .on_ping_success = NULL, .on_ping_timeout = NULL, .on_ping_end = __ping_end };
    esp_ping_handle_t ping;
    esp_ping_new_session(&config, &cbs, &ping);
    __g_ping_done = false;
    esp_ping_start(ping);
}

extern char *app_https_upload_audio(uint8_t *data, size_t len);
// net check
static void __app_wifi_task(void *p_arg)
{
    int cnt = 0;
    struct view_data_wifi_st st;

    while (1)
    {
        xSemaphoreTake(__g_net_check_sem, pdMS_TO_TICKS(5000));
        __wifi_st_get(&st);

        // Periodically check the network connection status
        if (st.is_connected)
        {
            if (__g_ping_done)
            {
                if (st.is_network)
                {
                    cnt++;
                    // 5min check network
                    if (cnt > 60)
                    {
                        cnt = 0;
                        ESP_LOGI(TAG, "Network normal last time, retry check network...");
                        __ping_start();
                    }
                    // uint8_t buf[32];
                    // int len = 32;
                    // app_https_upload_audio(buf, len);
                }
                else
                {
                    ESP_LOGI(TAG, "Last network exception, check network...");
                    __ping_start();
                }
            }
        }
        else if (_g_wifi_cfg.is_cfg && !st.is_connecting)
        {
            // Periodically check the wifi connection status

            // 5min retry connect
            if (_g_wifi_cfg.wifi_reconnect_cnt > 5)
            {
                ESP_LOGI(TAG, " Wifi reconnect...");
                _g_wifi_cfg.wifi_reconnect_cnt = 0;
                wifi_retry_max = 3;
                s_retry_num = 0;

                esp_wifi_stop();
                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_start());
            }
            _g_wifi_cfg.wifi_reconnect_cnt++;
        }
    }
}

static void __view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id)
    {
        case VIEW_EVENT_WIFI_CONNECT: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CONNECT");
            struct view_data_wifi_config *p_cfg = (struct view_data_wifi_config *)event_data;

            if (p_cfg->have_password)
            {
                __wifi_connect(p_cfg->ssid, (const char *)p_cfg->password, 3);
            }
            else
            {
                __wifi_connect(p_cfg->ssid, NULL, 3);
            }
            break;
        }
        case VIEW_EVENT_WIFI_CFG_DELETE: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CFG_DELETE");
            __wifi_cfg_restore();
            break;
        }
        case VIEW_EVENT_SHUTDOWN: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_SHUTDOWN");
            __wifi_shutdown();
            break;
        }
        default:
            break;
    }
}

static void __wifi_cfg_init(void)
{
    memset(&_g_wifi_cfg, 0, sizeof(_g_wifi_cfg));
}

int set_wifi_config(wifi_config *config)
{
    int result = 0;
    switch (config->caller)
    {
        case UI_CALLER: {
            // code
            break;
        }
        case AT_CMD_CALLER: {
            // code
            struct view_data_wifi_config outer_config;
            memset(outer_config.ssid, 0, sizeof(outer_config.ssid));
            strncpy(outer_config.ssid , config->ssid, sizeof(outer_config.ssid ) - 1);
            outer_config.ssid[sizeof(outer_config.ssid) - 1] = '\0';
            if(config->password != NULL){
                outer_config.have_password=1;
            }
            else{
                outer_config.have_password=0;
            }
            strncpy(outer_config.password, config->password,sizeof(outer_config.password) - 1);
            outer_config.password[sizeof(outer_config.password) - 1] = '\0';

            result=esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, &outer_config, sizeof(struct view_data_wifi_config), portMAX_DELAY);
            break;
        }
        default: {
            break;
        }
    }
    return result;
}

void wifi_config_layer(void *pvParameters)
{
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGE(TAG, "wifi_config_layer");
        __wifi_scan();
    }
}

void app_wifi_config_layer_init()
{
    xTaskCreate(&wifi_config_layer, "wifi_config_layer", 1024 * 5, NULL, 4, &xTask_wifi_config_layer);
}
int app_wifi_init(void)
{
    __g_wifi_mutex = xSemaphoreCreateMutex();
    __g_data_mutex = xSemaphoreCreateMutex();
    __g_net_check_sem = xSemaphoreCreateBinary();

    __wifi_cfg_init();

    xTaskCreate(&__app_wifi_task, "__app_wifi_task", 1024 * 5, NULL, 10, NULL);

    // StaticTask_t wifi_config_layer_task_buffer;
    // StackType_t *wifi_config_layer_stack_buffer = heap_caps_malloc(WIFI_CONFIG_LAYER_STACK_SIZE * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    // if (wifi_config_layer_stack_buffer)
    // {
    //     xTaskCreateStatic(&wifi_config_layer,
    //         "wifi_config_layer",            // wifi_config_layer
    //         WIFI_CONFIG_LAYER_STACK_SIZE,   // 1024*5
    //         NULL,                           // NULL
    //         5,                             // 10
    //         wifi_config_layer_stack_buffer, // wifi_config_layer_stack_buffer
    //         &wifi_config_layer_task_buffer); // wifi_config_layer_task_buffer
    // }
    // else
    // {
    //     ESP_LOGE(TAG, "wifi_config_layer_task_buffer or wifi_config_layer_stack_buffer malloc failed");
    // }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    ESP_LOGI(TAG, "esp_wifi_init:%d, %s", ret, esp_err_to_name(ret));
    ESP_ERROR_CHECK(ret);

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &__wifi_event_handler, 0, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &__ip_event_handler, 0, &instance_got_ip));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT, __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CFG_DELETE, __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SHUTDOWN, __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST, __view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, __view_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg;
    struct view_data_wifi_st wifi_table_element_connected;

    esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);
    // wifi_table_element_connected.= wifi_cfg.sta.password;
    strcpy(wifi_table_element_connected.ssid, (char *)wifi_cfg.sta.ssid);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ, &wifi_table_element_connected, sizeof(struct view_data_wifi_st), portMAX_DELAY);

    if (strlen((const char *)wifi_cfg.sta.ssid))
    {
        _g_wifi_cfg.is_cfg = true;
        ESP_LOGI(TAG, "last config ssid: %s", wifi_cfg.sta.ssid);
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    else
    {
        ESP_LOGI(TAG, "Not config wifi, Entry wifi config screen");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    return 0;
}