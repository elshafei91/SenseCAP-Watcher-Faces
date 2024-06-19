#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

#include "sensecap-watcher.h"

#include "data_defs.h"
#include "event_loops.h"
#include "util.h"
#include "app_ble.h"
#include "factory_info.h"


/*
Bluetooth LE uses four association models depending on the I/O capabilities of the devices.

Just Works: designed for scenarios where at least one of the devices does not have a display capable of displaying a six digit number nor does it have a keyboard capable of entering six decimal digits.

Numeric Comparison: designed for scenarios where both devices are capable of displaying a six digit number and both are capable of having the user enter “yes” or “no”. A good example of this model is the cell phone / PC scenario.

Out of Band: designed for scenarios where an Out of Band mechanism is used to both discover the devices as well as to exchange or transfer cryptographic numbers used in the pairing process.

Passkey Entry: designed for the scenario where one device has input capability but does not have the capability to display six digits and the other device has output capabilities. A good example of this model is the PC and keyboard scenario.
*/
#define BLE_SM_IO_CAP_DISP_ONLY         0
#define BLE_SM_IO_CAP_DISP_YES_NO       1
#define BLE_SM_IO_CAP_KEYBOARD_ONLY     2
#define BLE_SM_IO_CAP_NO_IO             3
#define BLE_SM_IO_CAP_KEYBOARD_DISP     4

#define SENSECAP_SN_STR_LEN             18


static const char *TAG = "ble";


static uint8_t adv_data[31] = {
    0x05, 0x03, 0x86, 0x28, 0x86, 0xA8,
    0x18, 0x09, '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f', '0', '1', '-', 'W', 'A', 'C', 'H'
};

static uint8_t ble_mac_addr[6] = {0};
static uint8_t own_addr_type;
static SemaphoreHandle_t g_sem_mac_addr;

void ble_store_config_init(void);
static int bleprph_gap_event(struct ble_gap_event *event, void *arg);



/**
 * Logs information about a connection to the console.
 */
static void bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    char *addr = (char *)desc->our_ota_addr.val;
    ESP_LOGI(TAG, "handle=%d our_ota_addr_type=%d our_ota_addr=%02X:%02X:%02X:%02X:%02X:%02X",
                desc->conn_handle, desc->our_ota_addr.type,
                addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    addr = (char *)desc->our_id_addr.val;
    ESP_LOGI(TAG, " our_id_addr_type=%d our_id_addr=%02X:%02X:%02X:%02X:%02X:%02X",
                desc->our_id_addr.type, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    addr = (char *)desc->peer_ota_addr.val;
    ESP_LOGI(TAG, " peer_ota_addr_type=%d peer_ota_addr=%02X:%02X:%02X:%02X:%02X:%02X",
                desc->peer_ota_addr.type, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    addr = (char *)desc->peer_id_addr.val;
    ESP_LOGI(TAG, " peer_id_addr_type=%d peer_id_addr=%02X:%02X:%02X:%02X:%02X:%02X",
                desc->peer_id_addr.type, addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    ESP_LOGI(TAG, " conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                "encrypted=%d authenticated=%d bonded=%d\n",
                desc->conn_itvl, desc->conn_latency,
                desc->supervision_timeout,
                desc->sec_state.encrypted,
                desc->sec_state.authenticated,
                desc->sec_state.bonded);
}

/**
 * Enables advertising with Seeed defined data
 */
static void bleprph_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    const char *name;
    int rc;


    rc = ble_gap_adv_set_data(adv_data, sizeof(adv_data));
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unused by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        ESP_LOGI(TAG, "connection %s; status=%d ",
                    event->connect.status == 0 ? "established" : "failed",
                    event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            bleprph_print_conn_desc(&desc);
        }

        if (event->connect.status != 0) {
            /* Connection failed; resume advertising. */
            bleprph_advertise();
        }

        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect; reason=%d ", event->disconnect.reason);
        bleprph_print_conn_desc(&event->disconnect.conn);

        /* Connection terminated; resume advertising. */
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        ESP_LOGI(TAG, "connection updated; status=%d ",
                    event->conn_update.status);
        rc = ble_gap_conn_find(event->conn_update.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "advertise complete; reason=%d",
                    event->adv_complete.reason);
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        ESP_LOGI(TAG, "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        ESP_LOGI(TAG, "notify_tx event; conn_handle=%d attr_handle=%d "
                    "status=%d is_indication=%d",
                    event->notify_tx.conn_handle,
                    event->notify_tx.attr_handle,
                    event->notify_tx.status,
                    event->notify_tx.indication);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d "
                    "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                    event->subscribe.conn_handle,
                    event->subscribe.attr_handle,
                    event->subscribe.reason,
                    event->subscribe.prev_notify,
                    event->subscribe.cur_notify,
                    event->subscribe.prev_indicate,
                    event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "PASSKEY_ACTION_EVENT started");
        // struct ble_sm_io pkey = {0};
        // int key = 0;

        // if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        //     pkey.action = event->passkey.params.action;
        //     pkey.passkey = 123456; // This is the passkey to be entered on peer
        //     ESP_LOGI(TAG, "Enter passkey %" PRIu32 "on the peer side", pkey.passkey);
        //     rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        //     ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        // } else if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
        //     ESP_LOGI(TAG, "Passkey on device's display: %" PRIu32 , event->passkey.params.numcmp);
        //     ESP_LOGI(TAG, "Accept or reject the passkey through console in this format -> key Y or key N");
        //     pkey.action = event->passkey.params.action;
        //     if (scli_receive_key(&key)) {
        //         pkey.numcmp_accept = key;
        //     } else {
        //         pkey.numcmp_accept = 0;
        //         ESP_LOGE(TAG, "Timeout! Rejecting the key");
        //     }
        //     rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        //     ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        // } else if (event->passkey.params.action == BLE_SM_IOACT_OOB) {
        //     static uint8_t tem_oob[16] = {0};
        //     pkey.action = event->passkey.params.action;
        //     for (int i = 0; i < 16; i++) {
        //         pkey.oob[i] = tem_oob[i];
        //     }
        //     rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        //     ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        // } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
        //     ESP_LOGI(TAG, "Enter the passkey through console in this format-> key 123456");
        //     pkey.action = event->passkey.params.action;
        //     if (scli_receive_key(&key)) {
        //         pkey.passkey = key;
        //     } else {
        //         pkey.passkey = 0;
        //         ESP_LOGE(TAG, "Timeout! Passing 0 as the key");
        //     }
        //     rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        //     ESP_LOGI(TAG, "ble_sm_inject_io result: %d", rc);
        // }
        return 0;

    case BLE_GAP_EVENT_AUTHORIZE:
        ESP_LOGI(TAG, "authorize event: conn_handle=%d attr_handle=%d is_read=%d",
                    event->authorize.conn_handle,
                    event->authorize.attr_handle,
                    event->authorize.is_read);

        /* The default behaviour for the event is to reject authorize request */
        event->authorize.out_response = BLE_GAP_AUTHORIZE_REJECT;
        return 0;
    }

    return 0;
}

static void __bleprph_on_reset(int reason)
{
    ESP_LOGI(TAG, ">>> on_reset, reason=%d\n", reason);
}

static void __bleprph_on_sync(void)
{
    int rc;

    ESP_LOGI(TAG, ">>> on_sync ...");

    /* Make sure we have proper identity address set (public preferred) */
    ESP_ERROR_CHECK(ble_hs_util_ensure_addr(0));

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "error determining address type; rc=%d", rc);
        return;
    }

    /* Printing ADDR */
    uint8_t addr_val[6];
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);
    xSemaphoreTake(g_sem_mac_addr, pdMS_TO_TICKS(10000));
    memset(ble_mac_addr, 0, sizeof(ble_mac_addr));
    for (int i = 0; i < 6 && rc == 0; i++)
    {
        ble_mac_addr[i] = addr_val[5 - i];
    }
    xSemaphoreGive(g_sem_mac_addr);

    ESP_LOGI(TAG, "BLE Address: %02X:%02X:%02X:%02X:%02X:%02X", 
                    addr_val[5], addr_val[4], addr_val[3], addr_val[2], addr_val[1], addr_val[0]);

    /* Begin advertising. */
    bleprph_advertise();
}

static void __bleprph_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}

esp_err_t app_ble_init(void)
{
#if CONFIG_ENABLE_FACTORY_FW_DEBUG_LOG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    esp_err_t ret;

    g_sem_mac_addr = xSemaphoreCreateMutex();

    ret = nimble_port_init();
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to init nimble %d ", ret);

    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = __bleprph_on_reset;
    ble_hs_cfg.sync_cb = __bleprph_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;  //Security Manager Local Input Output Capabilities
    ble_hs_cfg.sm_sc = 0;  //Security Manager Secure Connections flag

    ESP_ERROR_CHECK(gatt_svr_init());

    const char *sn = factory_info_sn_get();
    memcpy(adv_data + 8, sn, SENSECAP_SN_STR_LEN);
    char ble_name[24] = { 0 };
    memcpy((char *)ble_name, adv_data + 8, SENSECAP_SN_STR_LEN + 5/*-WACH*/);
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(ble_name));

    ble_store_config_init();

    nimble_port_freertos_init(__bleprph_host_task);

    return ret;
}

void set_ble_status(int caller, int status)
{

}

uint8_t *app_ble_get_mac_address(void)
{
    uint8_t *btaddr = NULL;

    xSemaphoreTake(g_sem_mac_addr, pdMS_TO_TICKS(10000));
    if (ble_mac_addr[5] != 0)
        btaddr = ble_mac_addr;
    xSemaphoreGive(g_sem_mac_addr);

    return btaddr;
}
