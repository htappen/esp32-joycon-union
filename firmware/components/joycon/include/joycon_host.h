/*
 * joycon_host.h — Bluepad32 glue: bond exactly one left + one right Joy-Con,
 * translate each into a joycon_state_t, expose link/battery/pairing control
 * (FR-1..FR-6).
 *
 * Threading: callbacks fire on the Bluepad32 / BTstack task (core 0). They
 * must not block; feed a queue and return.
 */
#ifndef JCB_JOYCON_HOST_H
#define JCB_JOYCON_HOST_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "pad_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*joycon_state_cb_t)(const joycon_state_t *st, void *ctx);
typedef void (*joycon_link_cb_t)(jc_side_t side, bool connected, void *ctx);

typedef struct {
    joycon_state_cb_t on_state;   /* a fresh decoded report                */
    joycon_link_cb_t  on_link;    /* connect / disconnect for a side       */
    void             *ctx;

    /* Remembered addresses to auto-reconnect (FR-3). addr all-zero => none. */
    uint8_t remembered_left[6];
    uint8_t remembered_right[6];
} joycon_host_cfg_t;

/* Start Bluepad32 + BTstack Classic HID host. Call once, early. */
esp_err_t joycon_host_init(const joycon_host_cfg_t *cfg);

/* FR-1: allow/disallow discovery of new Joy-Cons in sync mode. */
void joycon_host_set_pairing(bool enable);
bool joycon_host_pairing_enabled(void);

/* FR-5: drop a side's bond (link key + remembered address). */
void joycon_host_forget(jc_side_t side);
void joycon_host_forget_all(void);

bool    joycon_host_connected(jc_side_t side);
uint8_t joycon_host_battery(jc_side_t side);      /* 0..100, 0xFF unknown */
void    joycon_host_get_addr(jc_side_t side, uint8_t out[6], bool *valid);

/* FR-6 (stretch): set the Joy-Con player LEDs to the "player 1" pattern. */
void joycon_host_set_player_led(jc_side_t side);

#ifdef __cplusplus
}
#endif

#endif /* JCB_JOYCON_HOST_H */
