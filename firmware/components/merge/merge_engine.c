/* merge_engine.c — see merge_engine.h */

#include "merge_engine.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Config                                                              */
/* ------------------------------------------------------------------ */

void merge_config_defaults(merge_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->ab_xbox_position      = false;   /* Nintendo position (requirements §4.4) */
    cfg->deadzone              = 2500;    /* ~7.6% of full scale                   */
    cfg->triggers_full_scale   = true;
    cfg->trigger_digital_value = 1023;
    cfg->hold_ms               = 200;     /* FR-9                                  */
    cfg->map_capture           = PAD_SHARE;
    cfg->map_left_sl           = 0;
    cfg->map_left_sr           = 0;
    cfg->map_right_sl          = 0;
    cfg->map_right_sr          = 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Apply a radial deadzone and rescale so the response starts at the edge of
 * the deadzone (no dead "step").  Operates on a single (x,y) pair. */
static void apply_deadzone(int16_t *x, int16_t *y, uint16_t dz)
{
    if (dz == 0) return;
    if (dz >= 32767) dz = 32766;   /* keep (32767 - dz) a safe divisor */

    int32_t px = *x;
    int32_t py = *y;

    /* Integer magnitude via Newton's method on px*px + py*py. */
    uint64_t mag2 = (uint64_t)(px * px) + (uint64_t)(py * py);
    if (mag2 == 0) return;

    uint32_t mag = 0;
    {
        uint32_t g = 1u << 16;
        while ((uint64_t)g * g > mag2) g >>= 1;
        /* refine */
        for (int i = 0; i < 24; i++) {
            uint32_t next = (g + (uint32_t)(mag2 / (g ? g : 1))) / 2;
            if (next == g) break;
            g = next;
        }
        mag = g;
    }

    if (mag <= dz) {
        *x = 0;
        *y = 0;
        return;
    }

    /* Rescale magnitude from (dz, 32767] onto (0, 32767]. */
    int64_t scaled = (int64_t)(mag - dz) * 32767 / (32767 - dz);
    int64_t nx = px * scaled / (int64_t)mag;
    int64_t ny = py * scaled / (int64_t)mag;

    *x = (int16_t)clamp_i32((int32_t)nx, -32767, 32767);
    *y = (int16_t)clamp_i32((int32_t)ny, -32767, 32767);
}

static uint32_t set_if(bool cond, uint32_t bit) { return cond ? bit : 0u; }

/* ------------------------------------------------------------------ */
/* Half state machine                                                  */
/* ------------------------------------------------------------------ */

static void advance_half(merge_half_state_t *state, uint32_t lost_ms,
                         uint32_t now_ms, uint32_t hold_ms)
{
    if (*state == HALF_HOLD && (now_ms - lost_ms) >= hold_ms) {
        *state = HALF_NEUTRAL;
    }
}

/* ------------------------------------------------------------------ */
/* Output composition                                                  */
/* ------------------------------------------------------------------ */

static void compose_left(const merge_engine_t *m, unified_pad_state_t *out)
{
    const merge_config_t *c = &m->cfg;
    const joycon_state_t *l = &m->left;

    bool degraded = (m->left_state != HALF_ACTIVE);
    bool live     = (m->left_state == HALF_ACTIVE || m->left_state == HALF_HOLD);

    out->degraded_left = degraded;

    if (!live) {
        out->lx = out->ly = 0;
        out->lt = 0;
        return;
    }

    int16_t x = l->stick_x;
    int16_t y = l->stick_y;
    apply_deadzone(&x, &y, c->deadzone);
    out->lx = x;
    out->ly = y;

    uint32_t b = l->buttons;
    out->buttons |= set_if(b & JC_L_DPAD_UP,    PAD_DPAD_UP);
    out->buttons |= set_if(b & JC_L_DPAD_DOWN,  PAD_DPAD_DOWN);
    out->buttons |= set_if(b & JC_L_DPAD_LEFT,  PAD_DPAD_LEFT);
    out->buttons |= set_if(b & JC_L_DPAD_RIGHT, PAD_DPAD_RIGHT);
    out->buttons |= set_if(b & JC_L_L,          PAD_LB);
    out->buttons |= set_if(b & JC_L_MINUS,      PAD_VIEW);
    out->buttons |= set_if(b & JC_L_STICK,      PAD_L3);
    out->buttons |= set_if(b & JC_L_CAPTURE,    c->map_capture);
    out->buttons |= set_if(b & JC_L_SL,         c->map_left_sl);
    out->buttons |= set_if(b & JC_L_SR,         c->map_left_sr);

    uint16_t tval = c->triggers_full_scale ? 1023 : c->trigger_digital_value;
    if (tval > 1023) tval = 1023;
    out->lt = (b & JC_L_ZL) ? tval : 0;
}

static void compose_right(const merge_engine_t *m, unified_pad_state_t *out)
{
    const merge_config_t *c = &m->cfg;
    const joycon_state_t *r = &m->right;

    bool degraded = (m->right_state != HALF_ACTIVE);
    bool live     = (m->right_state == HALF_ACTIVE || m->right_state == HALF_HOLD);

    out->degraded_right = degraded;

    if (!live) {
        out->rx = out->ry = 0;
        out->rt = 0;
        return;
    }

    int16_t x = r->stick_x;
    int16_t y = r->stick_y;
    apply_deadzone(&x, &y, c->deadzone);
    out->rx = x;
    out->ry = y;

    uint32_t b = r->buttons;

    /* Face buttons.  Nintendo layout: physical positions are
     *   X = top, B = bottom, Y = left, A = right.
     * Xbox layout wants:  Y = top, A = bottom, X = left, B = right.
     *
     * ab_xbox_position == false (default): map by POSITION, so the button in
     *   the Xbox-A spot (bottom) is Nintendo B, etc.
     * ab_xbox_position == true: map by LABEL, Nintendo A -> Xbox A. */
    if (!c->ab_xbox_position) {
        out->buttons |= set_if(b & JC_R_B, PAD_A);  /* bottom */
        out->buttons |= set_if(b & JC_R_A, PAD_B);  /* right  */
        out->buttons |= set_if(b & JC_R_Y, PAD_X);  /* left   */
        out->buttons |= set_if(b & JC_R_X, PAD_Y);  /* top    */
    } else {
        out->buttons |= set_if(b & JC_R_A, PAD_A);
        out->buttons |= set_if(b & JC_R_B, PAD_B);
        out->buttons |= set_if(b & JC_R_X, PAD_X);
        out->buttons |= set_if(b & JC_R_Y, PAD_Y);
    }

    out->buttons |= set_if(b & JC_R_R,     PAD_RB);
    out->buttons |= set_if(b & JC_R_PLUS,  PAD_MENU);
    out->buttons |= set_if(b & JC_R_STICK, PAD_R3);
    out->buttons |= set_if(b & JC_R_HOME,  PAD_GUIDE);
    out->buttons |= set_if(b & JC_R_SL,    c->map_right_sl);
    out->buttons |= set_if(b & JC_R_SR,    c->map_right_sr);

    uint16_t tval = c->triggers_full_scale ? 1023 : c->trigger_digital_value;
    if (tval > 1023) tval = 1023;
    out->rt = (b & JC_R_ZR) ? tval : 0;
}

static void recompute(merge_engine_t *m, uint32_t now_ms)
{
    advance_half(&m->left_state,  m->left_lost_ms,  now_ms, m->cfg.hold_ms);
    advance_half(&m->right_state, m->right_lost_ms, now_ms, m->cfg.hold_ms);

    unified_pad_state_t out = unified_pad_neutral();
    compose_left(m, &out);
    compose_right(m, &out);
    m->out = out;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void merge_engine_init(merge_engine_t *m, const merge_config_t *cfg)
{
    memset(m, 0, sizeof(*m));
    if (cfg) {
        m->cfg = *cfg;
    } else {
        merge_config_defaults(&m->cfg);
    }
    m->left.side   = JC_SIDE_LEFT;
    m->right.side  = JC_SIDE_RIGHT;
    m->left_state  = HALF_NEUTRAL;
    m->right_state = HALF_NEUTRAL;
    recompute(m, 0);
}

void merge_engine_set_config(merge_engine_t *m, const merge_config_t *cfg)
{
    if (cfg) m->cfg = *cfg;
}

void merge_engine_update(merge_engine_t *m, const joycon_state_t *st, uint32_t now_ms)
{
    if (!st) return;

    if (st->side == JC_SIDE_LEFT) {
        merge_half_state_t prev = m->left_state;
        m->left = *st;
        m->left.side = JC_SIDE_LEFT;
        if (st->present) {
            m->left_state = HALF_ACTIVE;
        } else if (prev == HALF_ACTIVE) {
            m->left_state = HALF_HOLD;
            m->left_lost_ms = now_ms;
        }
    } else if (st->side == JC_SIDE_RIGHT) {
        merge_half_state_t prev = m->right_state;
        m->right = *st;
        m->right.side = JC_SIDE_RIGHT;
        if (st->present) {
            m->right_state = HALF_ACTIVE;
        } else if (prev == HALF_ACTIVE) {
            m->right_state = HALF_HOLD;
            m->right_lost_ms = now_ms;
        }
    } else {
        return; /* unknown side */
    }

    recompute(m, now_ms);
}

void merge_engine_notify_lost(merge_engine_t *m, jc_side_t side, uint32_t now_ms)
{
    if (side == JC_SIDE_LEFT) {
        if (m->left_state == HALF_ACTIVE) {
            m->left_state = HALF_HOLD;
            m->left_lost_ms = now_ms;
        }
    } else if (side == JC_SIDE_RIGHT) {
        if (m->right_state == HALF_ACTIVE) {
            m->right_state = HALF_HOLD;
            m->right_lost_ms = now_ms;
        }
    }
    recompute(m, now_ms);
}

void merge_engine_tick(merge_engine_t *m, uint32_t now_ms)
{
    recompute(m, now_ms);
}

const unified_pad_state_t *merge_engine_output(const merge_engine_t *m)
{
    return &m->out;
}
