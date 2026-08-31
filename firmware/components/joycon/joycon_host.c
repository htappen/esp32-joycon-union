/*
 * joycon_host.c — Bluepad32 custom-platform integration.
 *
 * !!! Verify against the PINNED Bluepad32 version before M0 !!!
 * The Bluepad32 C platform API (struct uni_platform, uni_hid_device_t,
 * uni_controller_t) has changed across releases. The symbols used here match
 * the 4.x line; field names / enum spellings must be reconciled at pin time
 * (plan §2, §9.4). Everything Bluepad32-specific is contained in this file so
 * the rest of the firmware is insulated.
 */

#include "joycon_host.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Bluepad32 / uni headers (from the managed component). */
#include "uni.h"

static const char *TAG = "jc_host";

#define JC_LOST_GRACE_MS 0   /* merge_engine owns the hold window (FR-9) */

typedef struct {
    bool                 connected;
    uni_hid_device_t    *dev;          /* Bluepad32 device handle          */
    uint8_t              addr[6];
    bool                 addr_valid;
    uint8_t              battery_pct;
} side_slot_t;

static struct {
    joycon_host_cfg_t cfg;
    side_slot_t       slot[2];         /* [JC_SIDE_LEFT], [JC_SIDE_RIGHT]  */
    volatile bool     pairing;
    bool              inited;
} S;

/* ------------------------------------------------------------------ */
/* Side identification                                                 */
/* ------------------------------------------------------------------ */

static int device_side(const uni_hid_device_t *d)
{
    /* Bluepad32 exposes the controller subtype; Joy-Cons come through as
     * distinct left/right types. Fall back to VID/PID (Nintendo 0x057E,
     * Joy-Con L 0x2006, Joy-Con R 0x2007). */
    switch (uni_hid_device_get_controller_subtype(d)) {
        case CONTROLLER_SUBTYPE_SWITCH_JOYCON_LEFT:  return JC_SIDE_LEFT;
        case CONTROLLER_SUBTYPE_SWITCH_JOYCON_RIGHT: return JC_SIDE_RIGHT;
        default: break;
    }
    uint16_t vid = 0, pid = 0;
    uni_hid_device_get_vendor_id(d, &vid);
    uni_hid_device_get_product_id(d, &pid);
    if (vid == 0x057E && pid == 0x2006) return JC_SIDE_LEFT;
    if (vid == 0x057E && pid == 0x2007) return JC_SIDE_RIGHT;
    return -1;
}

/* ------------------------------------------------------------------ */
/* uni_gamepad_t -> joycon_state_t                                     */
/* ------------------------------------------------------------------ */

/*
 * Bluepad32 parses a lone Joy-Con (vertical) into uni_gamepad_t with the
 * dpad + face buttons populated and one analog stick active. We translate
 * that back into our side-specific joycon_state_t. Bluepad32 has already
 * applied the Joy-Con's factory stick calibration, so the axes arrive in
 * its normalized [-512, 511] range — rescale to our int16 full-scale.
 */
static int16_t rescale_axis(int32_t v)
{
    int32_t o = v * 32767 / 512;
    if (o > 32767)  o = 32767;
    if (o < -32767) o = -32767;
    return (int16_t)o;
}

static void translate(jc_side_t side, const uni_gamepad_t *gp, joycon_state_t *out)
{
    memset(out, 0, sizeof(*out));
    out->side = side;
    out->present = true;

    if (side == JC_SIDE_LEFT) {
        out->stick_x = rescale_axis(gp->axis_x);
        out->stick_y = rescale_axis(-gp->axis_y);   /* uni: +y down -> +y up */

        uint32_t b = 0;
        if (gp->dpad & DPAD_UP)    b |= JC_L_DPAD_UP;
        if (gp->dpad & DPAD_DOWN)  b |= JC_L_DPAD_DOWN;
        if (gp->dpad & DPAD_LEFT)  b |= JC_L_DPAD_LEFT;
        if (gp->dpad & DPAD_RIGHT) b |= JC_L_DPAD_RIGHT;
        if (gp->buttons & BUTTON_SHOULDER_L) b |= JC_L_L;
        if (gp->buttons & BUTTON_TRIGGER_L)  b |= JC_L_ZL;
        if (gp->buttons & BUTTON_THUMB_L)    b |= JC_L_STICK;
        if (gp->misc_buttons & MISC_BUTTON_SELECT)  b |= JC_L_MINUS;
        if (gp->misc_buttons & MISC_BUTTON_CAPTURE) b |= JC_L_CAPTURE;
        /* SL/SR on the rail come through as the extra buttons when present. */
        if (gp->buttons & BUTTON_A) b |= JC_L_SL;
        if (gp->buttons & BUTTON_B) b |= JC_L_SR;
        out->buttons = b;
    } else {
        out->stick_x = rescale_axis(gp->axis_rx);
        out->stick_y = rescale_axis(-gp->axis_ry);

        uint32_t b = 0;
        if (gp->buttons & BUTTON_A) b |= JC_R_B;   /* uni A = south = Nintendo B */
        if (gp->buttons & BUTTON_B) b |= JC_R_A;   /* uni B = east  = Nintendo A */
        if (gp->buttons & BUTTON_X) b |= JC_R_Y;   /* uni X = west  = Nintendo Y */
        if (gp->buttons & BUTTON_Y) b |= JC_R_X;   /* uni Y = north = Nintendo X */
        if (gp->buttons & BUTTON_SHOULDER_R) b |= JC_R_R;
        if (gp->buttons & BUTTON_TRIGGER_R)  b |= JC_R_ZR;
        if (gp->buttons & BUTTON_THUMB_R)    b |= JC_R_STICK;
        if (gp->misc_buttons & MISC_BUTTON_START)  b |= JC_R_PLUS;
        if (gp->misc_buttons & MISC_BUTTON_SYSTEM) b |= JC_R_HOME;
        out->buttons = b;
    }
}

/* ------------------------------------------------------------------ */
/* Bluepad32 platform callbacks                                        */
/* ------------------------------------------------------------------ */

static void plat_init(int argc, const char **argv)
{
    (void)argc; (void)argv;
    ESP_LOGI(TAG, "bluepad32 platform init");
}

static void plat_on_init_complete(void)
{
    /* Start scanning only if pairing is enabled or we have a bond to restore. */
    bool want_scan = S.pairing ||
                     S.slot[JC_SIDE_LEFT].addr_valid ||
                     S.slot[JC_SIDE_RIGHT].addr_valid;
    uni_bt_enable_new_connections_unsafe(S.pairing);
    if (want_scan) uni_bt_start_scanning_and_autoconnect_unsafe();
    ESP_LOGI(TAG, "init complete (pairing=%d)", (int)S.pairing);
}

static uni_error_t plat_on_device_discovered(bd_addr_t addr, const char *name,
                                             uint16_t cod, uint8_t rssi)
{
    (void)name; (void)cod; (void)rssi; (void)addr;
    /* Accept only while pairing, or a remembered address. Bluepad32 filters
     * by COD already; final side/duplicate checks happen on ready. */
    return S.pairing ? UNI_ERROR_SUCCESS : UNI_ERROR_IGNORE_DEVICE;
}

static void slot_bind(int side, uni_hid_device_t *d)
{
    side_slot_t *sl = &S.slot[side];
    if (sl->connected && sl->dev && sl->dev != d) {
        ESP_LOGW(TAG, "side %d already filled; replacing bond (FR-2)", side);
        uni_hid_device_disconnect(sl->dev);
    }
    sl->connected = true;
    sl->dev = d;

    bd_addr_t a;
    uni_hid_device_get_address(d, a);
    memcpy(sl->addr, a, 6);
    sl->addr_valid = true;

    if (S.cfg.on_link) S.cfg.on_link((jc_side_t)side, true, S.cfg.ctx);
    joycon_host_set_player_led((jc_side_t)side);   /* FR-6 */
    ESP_LOGI(TAG, "Joy-Con %s bound", side == JC_SIDE_LEFT ? "L" : "R");
}

static void plat_on_device_connected(uni_hid_device_t *d) { (void)d; }

static void plat_on_device_ready(uni_hid_device_t *d)
{
    int side = device_side(d);
    if (side < 0) {
        ESP_LOGW(TAG, "non-Joy-Con or unknown side; rejecting (FR-2)");
        uni_hid_device_disconnect(d);
        return;
    }
    slot_bind(side, d);
}

static void plat_on_device_disconnected(uni_hid_device_t *d)
{
    for (int side = 0; side < 2; side++) {
        if (S.slot[side].dev == d) {
            S.slot[side].connected = false;
            S.slot[side].dev = NULL;
            if (S.cfg.on_link) S.cfg.on_link((jc_side_t)side, false, S.cfg.ctx);
            ESP_LOGW(TAG, "Joy-Con %s disconnected", side == JC_SIDE_LEFT ? "L" : "R");
            /* Keep addr_valid so we auto-reconnect (FR-10). */
        }
    }
}

static void plat_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl)
{
    if (ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) return;
    int side = -1;
    for (int i = 0; i < 2; i++) if (S.slot[i].dev == d) side = i;
    if (side < 0) return;

    S.slot[side].battery_pct = ctl->battery;   /* uni: 0..255; scaled below */

    joycon_state_t st;
    translate((jc_side_t)side, &ctl->gamepad, &st);
    st.battery_pct = ctl->battery == UNI_BATTERY_UNKNOWN
                         ? 0xFF
                         : (uint8_t)((ctl->battery * 100) / 255);
    st.last_update_ms = (uint32_t)(esp_timer_get_time() / 1000);

    if (S.cfg.on_state) S.cfg.on_state(&st, S.cfg.ctx);
}

static const uni_property_t *plat_get_property(uni_property_idx_t idx) { (void)idx; return NULL; }
static void plat_on_oob_event(uni_platform_oob_event_t e, void *d) { (void)e; (void)d; }

static struct uni_platform s_platform = {
    .name                  = "joycon-bridge",
    .init                  = plat_init,
    .on_init_complete      = plat_on_init_complete,
    .on_device_discovered  = plat_on_device_discovered,
    .on_device_connected   = plat_on_device_connected,
    .on_device_ready       = plat_on_device_ready,
    .on_device_disconnected = plat_on_device_disconnected,
    .on_controller_data    = plat_on_controller_data,
    .get_property          = plat_get_property,
    .on_oob_event          = plat_on_oob_event,
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

esp_err_t joycon_host_init(const joycon_host_cfg_t *cfg)
{
    if (S.inited) return ESP_ERR_INVALID_STATE;
    if (!cfg) return ESP_ERR_INVALID_ARG;
    S.cfg = *cfg;

    static const uint8_t zero[6] = {0};
    if (memcmp(cfg->remembered_left, zero, 6) != 0) {
        memcpy(S.slot[JC_SIDE_LEFT].addr, cfg->remembered_left, 6);
        S.slot[JC_SIDE_LEFT].addr_valid = true;
    }
    if (memcmp(cfg->remembered_right, zero, 6) != 0) {
        memcpy(S.slot[JC_SIDE_RIGHT].addr, cfg->remembered_right, 6);
        S.slot[JC_SIDE_RIGHT].addr_valid = true;
    }

    uni_platform_set_custom(&s_platform);
    uni_init(0, NULL);          /* starts the BTstack run loop task */
    S.inited = true;
    ESP_LOGI(TAG, "initialized");
    return ESP_OK;
}

void joycon_host_set_pairing(bool enable)
{
    S.pairing = enable;
    uni_bt_enable_new_connections_safe(enable);
    if (enable) uni_bt_start_scanning_and_autoconnect_safe();
    else        uni_bt_stop_scanning_safe();
    ESP_LOGI(TAG, "pairing %s", enable ? "ENABLED" : "disabled");
}

bool joycon_host_pairing_enabled(void) { return S.pairing; }

void joycon_host_forget(jc_side_t side)
{
    if (side != JC_SIDE_LEFT && side != JC_SIDE_RIGHT) return;
    side_slot_t *sl = &S.slot[side];
    if (sl->dev) uni_hid_device_disconnect(sl->dev);
    if (sl->addr_valid) {
        bd_addr_t a; memcpy(a, sl->addr, 6);
        uni_bt_del_keys_safe(a);
    }
    memset(sl, 0, sizeof(*sl));
    if (S.cfg.on_link) S.cfg.on_link(side, false, S.cfg.ctx);
    ESP_LOGW(TAG, "forgot Joy-Con %s", side == JC_SIDE_LEFT ? "L" : "R");
}

void joycon_host_forget_all(void)
{
    joycon_host_forget(JC_SIDE_LEFT);
    joycon_host_forget(JC_SIDE_RIGHT);
}

bool joycon_host_connected(jc_side_t side)
{
    return (side == JC_SIDE_LEFT || side == JC_SIDE_RIGHT) && S.slot[side].connected;
}

uint8_t joycon_host_battery(jc_side_t side)
{
    if (side != JC_SIDE_LEFT && side != JC_SIDE_RIGHT) return 0xFF;
    return S.slot[side].connected ? S.slot[side].battery_pct : 0xFF;
}

void joycon_host_get_addr(jc_side_t side, uint8_t out[6], bool *valid)
{
    if (side != JC_SIDE_LEFT && side != JC_SIDE_RIGHT) { *valid = false; return; }
    memcpy(out, S.slot[side].addr, 6);
    *valid = S.slot[side].addr_valid;
}

void joycon_host_set_player_led(jc_side_t side)
{
    side_slot_t *sl = &S.slot[side];
    if (!sl->connected || !sl->dev) return;
    /* Player 1 = LED 1 lit. Bluepad32 exposes a set-player-leds helper. */
    uni_hid_device_set_player_leds(sl->dev, 0x01);
}
