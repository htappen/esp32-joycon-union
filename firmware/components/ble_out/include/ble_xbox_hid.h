/*
 * ble_xbox_hid.h — BTstack LE HID peripheral presenting the merged
 * controller to the downstream host as an Xbox-style gamepad (FR-11..FR-15).
 *
 * Shares the single BTstack instance with Bluepad32 (the Classic HID host).
 * Wiring an LE peripheral next to Bluepad32 is the M4 integration risk
 * (plan §1.1 / §4 Phase 4). If it can't hold heap above the NFR-6 floor,
 * the two-chip fallback moves this file's job onto a second ESP32 running
 * ESP32-BLE-CompositeHID, fed the unified_pad_state over UART.
 */
#ifndef JCB_BLE_XBOX_HID_H
#define JCB_BLE_XBOX_HID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ble_xbox_conn_cb_t)(bool connected, void *ctx);

typedef struct {
    ble_xbox_conn_cb_t on_conn;
    void              *ctx;
    uint8_t            remembered_host[6];   /* all-zero => none (FR-15) */
    bool               allow_new_pairing;
} ble_xbox_cfg_t;

esp_err_t ble_xbox_hid_init(const ble_xbox_cfg_t *cfg);

/* Push a 16-byte packed input report (from xbox_report_pack). Non-blocking:
 * coalesces to the latest value if the link can't send yet. */
void ble_xbox_hid_send_report(const uint8_t *report, size_t len);

bool ble_xbox_hid_connected(void);

/* FR-14: report battery level (0..100) to the host. */
void ble_xbox_hid_set_battery(uint8_t pct);

/* FR-12: allow/deny bonding a new host; controls advertising policy. */
void ble_xbox_hid_set_pairing(bool allow);

/* FR-15: drop the bonded host. */
void ble_xbox_hid_forget_host(void);

/* Suspend / resume advertising + link (Config Mode coexistence, FR-22). */
void ble_xbox_hid_suspend(void);
void ble_xbox_hid_resume(void);

#ifdef __cplusplus
}
#endif

#endif /* JCB_BLE_XBOX_HID_H */
