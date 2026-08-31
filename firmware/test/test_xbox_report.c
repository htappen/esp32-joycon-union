/* Golden-vector tests for xbox_report packing (Phase 2 exit criteria). */

#include "minunit.h"
#include "xbox_report.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void test_pack_len_and_neutral(void)
{
    unified_pad_state_t s = unified_pad_neutral();
    uint8_t buf[XBOX_REPORT_LEN];
    size_t n = xbox_report_pack(&s, buf, sizeof(buf));

    mu_eq_int("pack len", XBOX_REPORT_LEN, n);
    mu_eq_int("neutral LX centered", 32768, rd16(&buf[0]));
    mu_eq_int("neutral LY centered", 32768, rd16(&buf[2]));
    mu_eq_int("neutral RX centered", 32768, rd16(&buf[4]));
    mu_eq_int("neutral RY centered", 32768, rd16(&buf[6]));
    mu_eq_int("neutral LT", 0, rd16(&buf[8]));
    mu_eq_int("neutral RT", 0, rd16(&buf[10]));
    mu_eq_int("neutral hat", 0, buf[12]);
    mu_eq_int("neutral buttons", 0, rd16(&buf[13]));
    mu_eq_int("neutral share", 0, buf[15]);
}

void test_pack_rejects_small_buf(void)
{
    unified_pad_state_t s = unified_pad_neutral();
    uint8_t buf[XBOX_REPORT_LEN - 1];
    mu_eq_int("short buf rejected", 0, xbox_report_pack(&s, buf, sizeof(buf)));
    mu_eq_int("null buf rejected", 0, xbox_report_pack(&s, NULL, 99));
    mu_eq_int("null in rejected", 0, xbox_report_pack(NULL, buf, 99));
}

void test_axis_mapping(void)
{
    mu_eq_int("center", 32768, xbox_report_axis(0, 0));
    mu_eq_int("full right", 65535, xbox_report_axis(32767, 0));
    mu_eq_int("full left", 1, xbox_report_axis(-32767, 0));
    /* Y axis is inverted: stick up (+Y) must produce a LOW HID value. */
    mu_eq_int("up inverted -> low", 1, xbox_report_axis(32767, 1));
    mu_eq_int("down inverted -> high", 65535, xbox_report_axis(-32767, 1));
    mu_eq_int("int16 min guarded", 65535, xbox_report_axis(-32768, 1));
}

void test_hat_encoding(void)
{
    mu_eq_int("neutral", 0, xbox_report_hat(0));
    mu_eq_int("up", 1, xbox_report_hat(PAD_DPAD_UP));
    mu_eq_int("up-right", 2, xbox_report_hat(PAD_DPAD_UP | PAD_DPAD_RIGHT));
    mu_eq_int("right", 3, xbox_report_hat(PAD_DPAD_RIGHT));
    mu_eq_int("down-right", 4, xbox_report_hat(PAD_DPAD_DOWN | PAD_DPAD_RIGHT));
    mu_eq_int("down", 5, xbox_report_hat(PAD_DPAD_DOWN));
    mu_eq_int("down-left", 6, xbox_report_hat(PAD_DPAD_DOWN | PAD_DPAD_LEFT));
    mu_eq_int("left", 7, xbox_report_hat(PAD_DPAD_LEFT));
    mu_eq_int("up-left", 8, xbox_report_hat(PAD_DPAD_UP | PAD_DPAD_LEFT));
    mu_eq_int("up+down cancels", 0, xbox_report_hat(PAD_DPAD_UP | PAD_DPAD_DOWN));
    mu_eq_int("SOCD both axes cancel", 0,
              xbox_report_hat(PAD_DPAD_UP | PAD_DPAD_DOWN | PAD_DPAD_LEFT | PAD_DPAD_RIGHT));
}

void test_buttons_and_triggers(void)
{
    unified_pad_state_t s = unified_pad_neutral();
    s.buttons = PAD_A | PAD_B | PAD_X | PAD_Y | PAD_LB | PAD_RB |
                PAD_VIEW | PAD_MENU | PAD_GUIDE | PAD_L3 | PAD_R3 | PAD_SHARE;
    s.lt = 1023;
    s.rt = 5000;    /* out of range -> clamped */

    uint8_t buf[XBOX_REPORT_LEN];
    xbox_report_pack(&s, buf, sizeof(buf));

    uint16_t b = rd16(&buf[13]);
    uint16_t want = XR_BTN_A | XR_BTN_B | XR_BTN_X | XR_BTN_Y | XR_BTN_LB |
                    XR_BTN_RB | XR_BTN_VIEW | XR_BTN_MENU | XR_BTN_GUIDE |
                    XR_BTN_L3 | XR_BTN_R3;
    mu_eq_int("all mapped buttons", want, b);
    mu_eq_int("share bit split out", 1, buf[15]);
    mu_eq_int("LT full", 1023, rd16(&buf[8]));
    mu_eq_int("RT clamped", 1023, rd16(&buf[10]));
}

void test_recorded_sample_left_only(void)
{
    /* Represents an M1 log line: left stick pushed up-left, D-pad up held,
     * right Joy-Con absent. */
    unified_pad_state_t s = unified_pad_neutral();
    s.lx = -20000;
    s.ly = 18000;
    s.buttons = PAD_DPAD_UP | PAD_LB;
    s.degraded_right = true;

    uint8_t buf[XBOX_REPORT_LEN];
    xbox_report_pack(&s, buf, sizeof(buf));

    mu_check("LX left of center", rd16(&buf[0]) < 32768);
    mu_check("LY above center -> below HID center", rd16(&buf[2]) < 32768);
    mu_eq_int("RX still centered", 32768, rd16(&buf[4]));
    mu_eq_int("hat = up", 1, buf[12]);
    mu_eq_int("LB set", XR_BTN_LB, rd16(&buf[13]));
}

void run_xbox_report_tests(void)
{
    mu_run(test_pack_len_and_neutral);
    mu_run(test_pack_rejects_small_buf);
    mu_run(test_axis_mapping);
    mu_run(test_hat_encoding);
    mu_run(test_buttons_and_triggers);
    mu_run(test_recorded_sample_left_only);
}
