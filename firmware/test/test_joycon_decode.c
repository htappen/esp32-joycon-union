/* Tests for joycon_decode (Phase 1 host-testable portion). */

#include "minunit.h"
#include "joycon_decode.h"

#include <string.h>

/* Encode two 12-bit values into 3 bytes (inverse of unpack12). */
static void pack12(uint8_t *p, uint16_t a, uint16_t b)
{
    p[0] = a & 0xFF;
    p[1] = ((a >> 8) & 0x0F) | ((b & 0x0F) << 4);
    p[2] = (b >> 4) & 0xFF;
}

static void base_report(uint8_t body[11])
{
    memset(body, 0, 11);
    body[1] = 0x80;                 /* battery nibble = 8 (full)  */
    pack12(&body[5], 2048, 2048);   /* left stick centered        */
    pack12(&body[8], 2048, 2048);   /* right stick centered       */
}

void test_decode_short_report_rejected(void)
{
    uint8_t body[8] = {0};
    jc_stick_calib_t c; joycon_decode_default_calib(&c);
    joycon_state_t s;
    mu_check("short report rejected",
             !joycon_decode_input_report(JC_SIDE_LEFT, body, sizeof(body), &c, &s, 0));
}

void test_decode_left_neutral(void)
{
    uint8_t body[11];
    base_report(body);
    jc_stick_calib_t c; joycon_decode_default_calib(&c);
    joycon_state_t s;

    mu_check("decode ok", joycon_decode_input_report(JC_SIDE_LEFT, body, 11, &c, &s, 42));
    mu_eq_int("side", JC_SIDE_LEFT, s.side);
    mu_check("present", s.present);
    mu_eq_int("centered x", 0, s.stick_x);
    mu_eq_int("centered y", 0, s.stick_y);
    mu_eq_int("no buttons", 0, s.buttons);
    mu_eq_int("battery full", 100, s.battery_pct);
    mu_eq_int("timestamp", 42, s.last_update_ms);
}

void test_decode_left_buttons_and_stick(void)
{
    uint8_t body[11];
    base_report(body);
    body[4] = 0x02 | 0x40;           /* D-pad up + L */
    body[3] = 0x01 | 0x20;           /* Minus + Capture */
    pack12(&body[5], 2048 + 1300, 2048);   /* full right on X */

    jc_stick_calib_t c; joycon_decode_default_calib(&c);
    joycon_state_t s;
    joycon_decode_input_report(JC_SIDE_LEFT, body, 11, &c, &s, 0);

    mu_eq_int("dpad up", JC_L_DPAD_UP, s.buttons & JC_L_DPAD_UP);
    mu_eq_int("L", JC_L_L, s.buttons & JC_L_L);
    mu_eq_int("minus", JC_L_MINUS, s.buttons & JC_L_MINUS);
    mu_eq_int("capture", JC_L_CAPTURE, s.buttons & JC_L_CAPTURE);
    mu_check("stick x near full right", s.stick_x > 30000);
    mu_check("stick y still centered", s.stick_y == 0);
}

void test_decode_right_buttons(void)
{
    uint8_t body[11];
    base_report(body);
    body[2] = 0x08 | 0x40;           /* A + R */
    body[3] = 0x02 | 0x10;           /* Plus + Home */

    jc_stick_calib_t c; joycon_decode_default_calib(&c);
    joycon_state_t s;
    joycon_decode_input_report(JC_SIDE_RIGHT, body, 11, &c, &s, 0);

    mu_eq_int("A", JC_R_A, s.buttons & JC_R_A);
    mu_eq_int("R", JC_R_R, s.buttons & JC_R_R);
    mu_eq_int("plus", JC_R_PLUS, s.buttons & JC_R_PLUS);
    mu_eq_int("home", JC_R_HOME, s.buttons & JC_R_HOME);
}

void test_decode_factory_calib_left(void)
{
    /* left blob layout: [above][center][below], packed 12-bit pairs. */
    uint8_t blob[9];
    pack12(&blob[0], 1400, 1500);    /* x_above, y_above */
    pack12(&blob[3], 2100, 2050);    /* x_center, y_center */
    pack12(&blob[6], 1300, 1350);    /* x_below, y_below */

    jc_stick_calib_t c;
    joycon_decode_default_calib(&c);
    joycon_decode_stick_calib(JC_SIDE_LEFT, blob, &c);

    mu_check("loaded", c.loaded);
    mu_eq_int("x_center", 2100, c.x_center);
    mu_eq_int("y_center", 2050, c.y_center);
    mu_eq_int("x_above", 1400, c.x_above);
    mu_eq_int("x_below", 1300, c.x_below);
}

void run_joycon_decode_tests(void)
{
    mu_run(test_decode_short_report_rejected);
    mu_run(test_decode_left_neutral);
    mu_run(test_decode_left_buttons_and_stick);
    mu_run(test_decode_right_buttons);
    mu_run(test_decode_factory_calib_left);
}
