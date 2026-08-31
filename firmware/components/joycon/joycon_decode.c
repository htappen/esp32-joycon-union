/* joycon_decode.c — see joycon_decode.h */

#include "joycon_decode.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Calibration                                                         */
/* ------------------------------------------------------------------ */

void joycon_decode_default_calib(jc_stick_calib_t *c)
{
    memset(c, 0, sizeof(*c));
    /* Nominal Joy-Con center ~2048 with ~1300 counts of travel each way. */
    c->x_center = c->y_center = 2048;
    c->x_below = c->x_above = 1300;
    c->y_below = c->y_above = 1300;
    c->deadzone = 160;
    c->loaded = false;
}

/* Unpack two 12-bit little-endian values from 3 bytes. */
static void unpack12(const uint8_t *p, uint16_t *a, uint16_t *b)
{
    *a = (uint16_t)(p[0] | ((p[1] & 0x0F) << 8));
    *b = (uint16_t)((p[1] >> 4) | (p[2] << 4));
}

void joycon_decode_stick_calib(jc_side_t side, const uint8_t blob9[9],
                               jc_stick_calib_t *out)
{
    uint16_t g0x, g0y, g1x, g1y, g2x, g2y;
    unpack12(&blob9[0], &g0x, &g0y);
    unpack12(&blob9[3], &g1x, &g1y);
    unpack12(&blob9[6], &g2x, &g2y);

    if (side == JC_SIDE_LEFT) {
        /* left: [above][center][below] */
        out->x_above = g0x; out->y_above = g0y;
        out->x_center = g1x; out->y_center = g1y;
        out->x_below = g2x; out->y_below = g2y;
    } else {
        /* right: [center][below][above] */
        out->x_center = g0x; out->y_center = g0y;
        out->x_below = g1x; out->y_below = g1y;
        out->x_above = g2x; out->y_above = g2y;
    }

    /* Guard against a bad/blank (0xFFF) read. */
    if (out->x_center == 0 || out->x_center == 0x0FFF) {
        joycon_decode_default_calib(out);
        return;
    }
    if (out->x_above == 0) out->x_above = 1300;
    if (out->x_below == 0) out->x_below = 1300;
    if (out->y_above == 0) out->y_above = 1300;
    if (out->y_below == 0) out->y_below = 1300;
    if (out->deadzone == 0) out->deadzone = 160;
    out->loaded = true;
}

void joycon_decode_stick_params(const uint8_t params[2], jc_stick_calib_t *io)
{
    uint16_t dz = (uint16_t)(params[0] | ((params[1] & 0x0F) << 8));
    if (dz > 0 && dz < 1000) io->deadzone = dz;
}

/* ------------------------------------------------------------------ */
/* Input report 0x30                                                   */
/* ------------------------------------------------------------------ */

/* Scale a raw axis value to [-32767, 32767] using calibration, with the
 * calibration deadzone removed and the response rescaled. */
static int16_t scale_axis(uint16_t raw, uint16_t center,
                          uint16_t below, uint16_t above, uint16_t dz)
{
    int32_t delta = (int32_t)raw - (int32_t)center;
    int32_t range;

    if (delta >= 0) {
        range = above ? above : 1;
    } else {
        range = below ? below : 1;
        delta = -delta;
    }

    if ((uint16_t)delta <= dz) return 0;

    int32_t span = range - (int32_t)dz;
    if (span <= 0) span = 1;
    int32_t mag = (delta - (int32_t)dz) * 32767 / span;
    if (mag > 32767) mag = 32767;

    return (raw >= center) ? (int16_t)mag : (int16_t)(-mag);
}

bool joycon_decode_input_report(jc_side_t side, const uint8_t *body, size_t len,
                                const jc_stick_calib_t *calib,
                                joycon_state_t *out, uint32_t now_ms)
{
    if (!body || len < 11 || !out || !calib) return false;

    const uint8_t btn_right  = body[2];
    const uint8_t btn_shared = body[3];
    const uint8_t btn_left   = body[4];

    joycon_state_t s;
    memset(&s, 0, sizeof(s));
    s.side = side;
    s.present = true;
    s.last_update_ms = now_ms;

    /* Battery: high nibble of body[1]. 8=full .. 0=empty; bit0 = charging. */
    uint8_t batt_raw = (body[1] & 0xF0) >> 4;
    static const uint8_t batt_map[9] = {5, 10, 25, 40, 50, 65, 75, 90, 100};
    s.battery_pct = batt_map[batt_raw > 8 ? 8 : batt_raw];

    if (side == JC_SIDE_LEFT) {
        uint16_t rx, ry;
        unpack12(&body[5], &rx, &ry);
        s.stick_x = scale_axis(rx, calib->x_center, calib->x_below, calib->x_above, calib->deadzone);
        s.stick_y = scale_axis(ry, calib->y_center, calib->y_below, calib->y_above, calib->deadzone);

        uint32_t b = 0;
        if (btn_left & 0x02) b |= JC_L_DPAD_UP;
        if (btn_left & 0x01) b |= JC_L_DPAD_DOWN;
        if (btn_left & 0x08) b |= JC_L_DPAD_LEFT;
        if (btn_left & 0x04) b |= JC_L_DPAD_RIGHT;
        if (btn_left & 0x40) b |= JC_L_L;
        if (btn_left & 0x80) b |= JC_L_ZL;
        if (btn_left & 0x20) b |= JC_L_SL;
        if (btn_left & 0x10) b |= JC_L_SR;
        if (btn_shared & 0x01) b |= JC_L_MINUS;
        if (btn_shared & 0x08) b |= JC_L_STICK;
        if (btn_shared & 0x20) b |= JC_L_CAPTURE;
        s.buttons = b;
    } else {
        uint16_t rx, ry;
        unpack12(&body[8], &rx, &ry);
        s.stick_x = scale_axis(rx, calib->x_center, calib->x_below, calib->x_above, calib->deadzone);
        s.stick_y = scale_axis(ry, calib->y_center, calib->y_below, calib->y_above, calib->deadzone);

        uint32_t b = 0;
        if (btn_right & 0x08) b |= JC_R_A;
        if (btn_right & 0x04) b |= JC_R_B;
        if (btn_right & 0x02) b |= JC_R_X;
        if (btn_right & 0x01) b |= JC_R_Y;
        if (btn_right & 0x40) b |= JC_R_R;
        if (btn_right & 0x80) b |= JC_R_ZR;
        if (btn_right & 0x20) b |= JC_R_SL;
        if (btn_right & 0x10) b |= JC_R_SR;
        if (btn_shared & 0x02) b |= JC_R_PLUS;
        if (btn_shared & 0x04) b |= JC_R_STICK;
        if (btn_shared & 0x10) b |= JC_R_HOME;
        s.buttons = b;
    }

    *out = s;
    return true;
}
