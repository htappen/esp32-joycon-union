/*
 * ble_xbox_hid.c — BTstack HID-over-GATT peripheral.
 *
 * !!! Verify against the PINNED BTstack version bundled with Bluepad32 !!!
 * Modelled on BTstack's hog_mouse_demo / hids_device API. The main open
 * questions for M4 (plan §9.6):
 *   - Can att_server + hids_device be initialised on the Bluepad32-owned
 *     BTstack instance without Bluepad32 reinitialising ATT?
 *   - Does the dual-mode controller advertise LE while 2 Classic ACL links
 *     are up, within the heap floor?
 * All BTstack specifics are contained here.
 */

#include "ble_xbox_hid.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "btstack.h"
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"

#include "xbox_descriptor.h"
#include "xbox_hid.h"          /* generated from xbox_hid.gatt by compile_gatt.py */

static const char *TAG = "ble_xbox";

static struct {
    ble_xbox_cfg_t   cfg;
    hci_con_handle_t con_handle;
    bool             connected;
    bool             suspended;
    bool             can_send;
    uint8_t          last_report[XBOX_HID_REPORT_LEN];
    bool             report_dirty;
    uint8_t          battery;
    btstack_packet_callback_registration_t hci_cb;
    btstack_packet_callback_registration_t sm_cb;
    portMUX_TYPE     lock;
} S = {
    .con_handle = HCI_CON_HANDLE_INVALID,
    .battery    = 100,
    .lock       = portMUX_INITIALIZER_UNLOCKED,
};

/* --- Advertising ----------------------------------------------------- */

static uint8_t adv_data[] = {
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
          0x12, 0x18,   /* HID service 0x1812 */
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC4, 0x03,
    0x18, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
          'X','b','o','x',' ','W','i','r','e','l','e','s','s',' ',
          'C','o','n','t','r','o','l','l','e','r',
};

static void start_advertising(void)
{
    if (S.suspended) return;
    uint16_t adv_int_min = 0x0030;   /* 30 ms */
    uint16_t adv_int_max = 0x0030;
    bd_addr_t null_addr = {0};
    gap_advertisements_set_params(adv_int_min, adv_int_max, 0, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(sizeof(adv_data), adv_data);
    gap_advertisements_enable(1);
    ESP_LOGI(TAG, "advertising");
}

/* --- Report send flow ---------------------------------------------- */

static void flush_report(void)
{
    if (!S.connected || !S.can_send || !S.report_dirty) return;
    S.can_send = false;
    S.report_dirty = false;
    hids_device_send_input_report(S.con_handle, S.last_report, sizeof(S.last_report));
}

static void hids_packet_handler(uint8_t type, uint16_t channel,
                                uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_HIDS_META) return;

    switch (hci_event_hids_meta_get_subevent_code(packet)) {
        case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
            ESP_LOGI(TAG, "host enabled input notifications");
            hids_device_request_can_send_now_event(S.con_handle);
            break;
        case HIDS_SUBEVENT_CAN_SEND_NOW:
            S.can_send = true;
            flush_report();
            if (S.report_dirty) hids_device_request_can_send_now_event(S.con_handle);
            break;
        case HIDS_SUBEVENT_PROTOCOL_MODE:
            ESP_LOGI(TAG, "protocol mode = %u",
                     hids_subevent_protocol_mode_get_protocol_mode(packet));
            break;
        default:
            break;
    }
}

static void hci_packet_handler(uint8_t type, uint16_t channel,
                               uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING)
                start_advertising();
            break;
        case HCI_EVENT_LE_META:
            if (hci_event_le_meta_get_subevent_code(packet) ==
                HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
                S.con_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                S.connected = true;
                S.can_send = false;
                ESP_LOGI(TAG, "host connected (handle 0x%04x)", S.con_handle);
                if (S.cfg.on_conn) S.cfg.on_conn(true, S.cfg.ctx);
            }
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            S.connected = false;
            S.con_handle = HCI_CON_HANDLE_INVALID;
            ESP_LOGW(TAG, "host disconnected");
            if (S.cfg.on_conn) S.cfg.on_conn(false, S.cfg.ctx);
            start_advertising();           /* FR-12: re-advertise */
            break;
        default:
            break;
    }
}

static void sm_packet_handler(uint8_t type, uint16_t channel,
                              uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;
        case SM_EVENT_PAIRING_COMPLETE:
            ESP_LOGI(TAG, "pairing complete status=%u",
                     sm_event_pairing_complete_get_status(packet));
            break;
        default:
            break;
    }
}

/* --- Public API --------------------------------------------------- */

esp_err_t ble_xbox_hid_init(const ble_xbox_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    S.cfg = *cfg;

    /* BTstack core (l2cap/sm) is already up via Bluepad32; only add the LE
     * peripheral profile. att_server_init must run against the combined db
     * generated from xbox_hid.gatt — see the M4 note at the top of file. */
    att_server_init(profile_data, NULL, NULL);

    device_information_service_server_init();
    device_information_service_server_set_manufacturer_name(XBOX_MANUFACTURER);
    device_information_service_server_set_pnp_id(0x02, XBOX_VID, XBOX_PID, XBOX_VERSION);

    battery_service_server_init(S.battery);

    hids_device_init(0 /* country code */, xbox_hid_report_descriptor,
                     sizeof(xbox_hid_report_descriptor));

    S.hci_cb.callback = &hci_packet_handler;
    hci_add_event_handler(&S.hci_cb);
    S.sm_cb.callback = &sm_packet_handler;
    sm_add_event_handler(&S.sm_cb);
    att_server_register_packet_handler(hids_packet_handler);
    hids_device_register_packet_handler(hids_packet_handler);

    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);

    if (hci_get_state() == HCI_STATE_WORKING) start_advertising();

    ESP_LOGI(TAG, "initialised (pid 0x%04x)", XBOX_PID);
    return ESP_OK;
}

void ble_xbox_hid_send_report(const uint8_t *report, size_t len)
{
    if (!report || len != XBOX_HID_REPORT_LEN) return;

    portENTER_CRITICAL(&S.lock);
    if (memcmp(S.last_report, report, len) != 0) {
        memcpy(S.last_report, report, len);
        S.report_dirty = true;
    }
    portEXIT_CRITICAL(&S.lock);

    if (S.connected && S.report_dirty) {
        if (S.can_send) flush_report();
        else            hids_device_request_can_send_now_event(S.con_handle);
    }
}

bool ble_xbox_hid_connected(void) { return S.connected; }

void ble_xbox_hid_set_battery(uint8_t pct)
{
    if (pct > 100) pct = 100;
    S.battery = pct;
    battery_service_server_set_battery_value(pct);
}

void ble_xbox_hid_set_pairing(bool allow)
{
    gap_advertisements_enable(0);
    if (allow) {
        gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_STATIC);
    }
    start_advertising();
    ESP_LOGI(TAG, "new-host pairing %s", allow ? "allowed" : "denied");
}

void ble_xbox_hid_forget_host(void)
{
    if (S.connected) gap_disconnect(S.con_handle);
    /* Wipe LE bonding DB. */
    int i;
    for (i = 0; i < le_device_db_max_count(); i++) {
        bd_addr_t addr; int type;
        le_device_db_info(i, &type, addr, NULL);
        if (type != BD_ADDR_TYPE_UNKNOWN) le_device_db_remove(i);
    }
    ESP_LOGW(TAG, "bonded host forgotten");
    start_advertising();
}

void ble_xbox_hid_suspend(void)
{
    S.suspended = true;
    gap_advertisements_enable(0);
    if (S.connected) gap_disconnect(S.con_handle);
    ESP_LOGI(TAG, "suspended (Config Mode)");
}

void ble_xbox_hid_resume(void)
{
    S.suspended = false;
    start_advertising();
    ESP_LOGI(TAG, "resumed");
}
