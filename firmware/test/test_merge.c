/* Golden-vector tests for merge_engine (Phase 2 exit criteria). */

#include "minunit.h"
#include "merge_engine.h"
#include "xbox_report.h"

static joycon_state_t left_neutral(void)
{
    joycon_state_t s = {0};
    s.side = JC_SIDE_LEFT;
    s.present = true;
    s.battery_pct = 100;
    return s;
}

static joycon_state_t right_neutral(void)
{
    joycon_state_t s = {0};
    s.side = JC_SIDE_RIGHT;
    s.present = true;
    s.battery_pct = 100;
    return s;
}

void test_neutral(void)
{
    merge_engine_t m;
    merge_engine_init(&m, NULL);

    joycon_state_t l = left_neutral();
    joycon_state_t r = right_neutral();
    merge_engine_update(&m, &l, 1000);
    merge_engine_update(&m, &r, 1000);

    const unified_pad_state_t *o = merge_engine_output(&m);
    mu_eq_int("neutral lx", 0, o->lx);
    mu_eq_int("neutral ly", 0, o->ly);
    mu_eq_int("neutral rx", 0, o->rx);
    mu_eq_int("neutral ry", 0, o->ry);
    mu_eq_int("neutral buttons", 0, o->buttons);
    mu_eq_int("neutral lt", 0, o->lt);
    mu_eq_int("neutral rt", 0, o->rt);
    mu_check("not degraded L", !o->degraded_left);
    mu_check("not degraded R", !o->degraded_right);
}

void test_all_buttons_nintendo_position(void)
{
    merge_engine_t m;
    merge_engine_init(&m, NULL);   /* default: Nintendo position */

    joycon_state_t l = left_neutral();
    joycon_state_t r = right_neutral();
    l.buttons = JC_L_DPAD_UP | JC_L_DPAD_DOWN | JC_L_DPAD_LEFT | JC_L_DPAD_RIGHT |
                JC_L_L | JC_L_ZL | JC_L_MINUS | JC_L_STICK | JC_L_CAPTURE;
    r.buttons = JC_R_A | JC_R_B | JC_R_X | JC_R_Y | JC_R_R | JC_R_ZR |
                JC_R_PLUS | JC_R_STICK | JC_R_HOME;

    merge_engine_update(&m, &l, 1000);
    merge_engine_update(&m, &r, 1000);
    const unified_pad_state_t *o = merge_engine_output(&m);

    uint32_t want = PAD_A | PAD_B | PAD_X | PAD_Y | PAD_LB | PAD_RB |
                    PAD_VIEW | PAD_MENU | PAD_GUIDE | PAD_L3 | PAD_R3 |
                    PAD_DPAD_UP | PAD_DPAD_DOWN | PAD_DPAD_LEFT | PAD_DPAD_RIGHT |
                    PAD_SHARE;   /* capture -> share by default */
    mu_eq_int("all buttons mask", want, o->buttons);
    mu_eq_int("ZL -> full LT", 1023, o->lt);
    mu_eq_int("ZR -> full RT", 1023, o->rt);
}

void test_ab_position_toggle(void)
{
    merge_config_t cfg;
    merge_config_defaults(&cfg);
    cfg.deadzone = 0;

    /* Nintendo position: physical B (bottom) -> Xbox A */
    merge_engine_t m;
    merge_engine_init(&m, &cfg);
    joycon_state_t r = right_neutral();
    r.buttons = JC_R_B;
    merge_engine_update(&m, &r, 100);
    mu_eq_int("nintendo: B->A", PAD_A, merge_engine_output(&m)->buttons);

    r.buttons = JC_R_A;
    merge_engine_update(&m, &r, 200);
    mu_eq_int("nintendo: A->B", PAD_B, merge_engine_output(&m)->buttons);

    /* Xbox position: label match */
    cfg.ab_xbox_position = true;
    merge_engine_init(&m, &cfg);
    r.buttons = JC_R_A;
    merge_engine_update(&m, &r, 300);
    mu_eq_int("xbox: A->A", PAD_A, merge_engine_output(&m)->buttons);
    r.buttons = JC_R_B;
    merge_engine_update(&m, &r, 400);
    mu_eq_int("xbox: B->B", PAD_B, merge_engine_output(&m)->buttons);
}

void test_stick_passthrough_and_deadzone(void)
{
    merge_config_t cfg;
    merge_config_defaults(&cfg);
    cfg.deadzone = 0;
    merge_engine_t m;
    merge_engine_init(&m, &cfg);

    joycon_state_t l = left_neutral();
    l.stick_x = 32767; l.stick_y = -32767;
    merge_engine_update(&m, &l, 10);
    const unified_pad_state_t *o = merge_engine_output(&m);
    mu_eq_int("stick x extreme", 32767, o->lx);
    mu_eq_int("stick y extreme", -32767, o->ly);

    /* Deadzone zeroes a small input. */
    merge_config_defaults(&cfg);
    cfg.deadzone = 5000;
    merge_engine_set_config(&m, &cfg);
    l.stick_x = 1000; l.stick_y = 0;
    merge_engine_update(&m, &l, 20);
    o = merge_engine_output(&m);
    mu_eq_int("inside deadzone -> 0", 0, o->lx);

    /* Just past the deadzone edge is small but non-zero, and full scale
     * still reaches the extreme. */
    l.stick_x = 32767; l.stick_y = 0;
    merge_engine_update(&m, &l, 30);
    o = merge_engine_output(&m);
    mu_eq_int("full past deadzone still maxes", 32767, o->lx);
}

void test_missing_left(void)
{
    merge_engine_t m;
    merge_engine_init(&m, NULL);

    joycon_state_t r = right_neutral();
    r.buttons = JC_R_A;                    /* nintendo pos -> PAD_B */
    merge_engine_update(&m, &r, 1000);

    /* Never saw a left Joy-Con: left half is degraded + neutral from the start. */
    const unified_pad_state_t *o = merge_engine_output(&m);
    mu_check("degraded left", o->degraded_left);
    mu_check("right ok", !o->degraded_right);
    mu_eq_int("right button present", PAD_B, o->buttons & PAD_B);
    mu_eq_int("left contributes nothing", 0, o->buttons & (PAD_LB | PAD_VIEW | PAD_L3));
}

void test_missing_right_hold_then_neutralize(void)
{
    merge_config_t cfg;
    merge_config_defaults(&cfg);
    cfg.hold_ms = 200;
    merge_engine_t m;
    merge_engine_init(&m, &cfg);

    joycon_state_t l = left_neutral();
    joycon_state_t r = right_neutral();
    r.buttons = JC_R_X;                    /* nintendo pos -> PAD_Y */
    merge_engine_update(&m, &l, 1000);
    merge_engine_update(&m, &r, 1000);
    mu_eq_int("right Y held live", PAD_Y, merge_engine_output(&m)->buttons & PAD_Y);

    /* Link drops at t=1000; within the hold window the last state persists. */
    merge_engine_notify_lost(&m, JC_SIDE_RIGHT, 1000);
    merge_engine_tick(&m, 1100);
    const unified_pad_state_t *o = merge_engine_output(&m);
    mu_check("degraded_right set during hold", o->degraded_right);
    mu_eq_int("held Y still reported", PAD_Y, o->buttons & PAD_Y);

    /* After the hold window the half neutralizes. */
    merge_engine_tick(&m, 1201);
    o = merge_engine_output(&m);
    mu_check("still degraded_right", o->degraded_right);
    mu_eq_int("Y cleared after hold", 0, o->buttons & PAD_Y);
    mu_eq_int("right stick centered", 0, o->rx);

    /* Reconnect: resume immediately, degraded cleared (FR-10). */
    r.buttons = JC_R_B;                    /* nintendo pos -> PAD_A */
    merge_engine_update(&m, &r, 1500);
    o = merge_engine_output(&m);
    mu_check("degraded_right cleared on reconnect", !o->degraded_right);
    mu_eq_int("post-reconnect A", PAD_A, o->buttons & PAD_A);
}

void test_trigger_config(void)
{
    merge_config_t cfg;
    merge_config_defaults(&cfg);
    cfg.triggers_full_scale = false;
    cfg.trigger_digital_value = 512;
    merge_engine_t m;
    merge_engine_init(&m, &cfg);

    joycon_state_t l = left_neutral();
    l.buttons = JC_L_ZL;
    merge_engine_update(&m, &l, 10);
    mu_eq_int("configurable trigger value", 512, merge_engine_output(&m)->lt);
}

void test_assignable_sl_sr(void)
{
    merge_config_t cfg;
    merge_config_defaults(&cfg);
    cfg.map_left_sl  = PAD_VIEW;
    cfg.map_right_sr = PAD_MENU;
    cfg.map_capture  = 0;                  /* unmapped */
    merge_engine_t m;
    merge_engine_init(&m, &cfg);

    joycon_state_t l = left_neutral();
    joycon_state_t r = right_neutral();
    l.buttons = JC_L_SL | JC_L_CAPTURE;
    r.buttons = JC_R_SR;
    merge_engine_update(&m, &l, 10);
    merge_engine_update(&m, &r, 10);
    const unified_pad_state_t *o = merge_engine_output(&m);
    mu_eq_int("left SL -> VIEW", PAD_VIEW, o->buttons & PAD_VIEW);
    mu_eq_int("right SR -> MENU", PAD_MENU, o->buttons & PAD_MENU);
    mu_eq_int("capture unmapped", 0, o->buttons & PAD_SHARE);
}

void run_merge_tests(void)
{
    mu_run(test_neutral);
    mu_run(test_all_buttons_nintendo_position);
    mu_run(test_ab_position_toggle);
    mu_run(test_stick_passthrough_and_deadzone);
    mu_run(test_missing_left);
    mu_run(test_missing_right_hold_then_neutralize);
    mu_run(test_trigger_config);
    mu_run(test_assignable_sl_sr);
}
