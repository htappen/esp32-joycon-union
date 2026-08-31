/*
 * pad_state.h — topology-independent controller state types.
 *
 * These types are the contract between the input side (joycon_decode /
 * joycon_host), the merge engine, and the output side (xbox_report / the
 * two-chip fallback's UART link).  They deliberately depend on nothing from
 * ESP-IDF so the merge engine and report packer build and unit-test on a
 * host PC (see firmware/test/).
 *
 * Conventions
 * -----------
 *  - Analog sticks: int16_t, range [-32767, 32767], center 0.
 *      +X = right, +Y = up (math convention).  joycon_decode is responsible
 *      for producing this orientation regardless of Joy-Con handedness.
 *  - Triggers in unified_pad_state: uint16_t, range [0, 1023] (Xbox 10-bit).
 *  - Time: milliseconds, monotonic, caller-supplied (esp_timer on target,
 *      a fake clock in tests).  Only *differences* are used, so the epoch
 *      does not matter as long as it is consistent within a run.
 */
#ifndef JCB_PAD_STATE_H
#define JCB_PAD_STATE_H

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Per-Joy-Con normalized state                                        */
/* ------------------------------------------------------------------ */

/* Left Joy-Con logical buttons (grip / vertical orientation). */
enum jc_left_button {
    JC_L_DPAD_UP    = 1u << 0,
    JC_L_DPAD_DOWN  = 1u << 1,
    JC_L_DPAD_LEFT  = 1u << 2,
    JC_L_DPAD_RIGHT = 1u << 3,
    JC_L_L          = 1u << 4,   /* top shoulder            */
    JC_L_ZL         = 1u << 5,   /* lower shoulder (digital) */
    JC_L_MINUS      = 1u << 6,
    JC_L_STICK      = 1u << 7,   /* stick click             */
    JC_L_CAPTURE    = 1u << 8,
    JC_L_SL         = 1u << 9,   /* rail button             */
    JC_L_SR         = 1u << 10,  /* rail button             */
};

/* Right Joy-Con logical buttons (grip / vertical orientation). */
enum jc_right_button {
    JC_R_A          = 1u << 0,   /* physical A (right)  */
    JC_R_B          = 1u << 1,   /* physical B (bottom) */
    JC_R_X          = 1u << 2,   /* physical X (top)    */
    JC_R_Y          = 1u << 3,   /* physical Y (left)   */
    JC_R_R          = 1u << 4,   /* top shoulder            */
    JC_R_ZR         = 1u << 5,   /* lower shoulder (digital) */
    JC_R_PLUS       = 1u << 6,
    JC_R_STICK      = 1u << 7,   /* stick click             */
    JC_R_HOME       = 1u << 8,
    JC_R_SL         = 1u << 9,   /* rail button             */
    JC_R_SR         = 1u << 10,  /* rail button             */
};

typedef enum {
    JC_SIDE_LEFT  = 0,
    JC_SIDE_RIGHT = 1,
} jc_side_t;

/* Normalized state of ONE physical Joy-Con, produced by joycon_decode. */
typedef struct {
    jc_side_t side;
    bool      present;          /* link is up and reporting            */
    uint32_t  buttons;          /* bitmask of jc_left_button / jc_right_button */
    int16_t   stick_x;          /* calibrated, [-32767, 32767]         */
    int16_t   stick_y;          /* calibrated, [-32767, 32767], +Y up  */
    uint8_t   battery_pct;      /* 0..100, 0xFF if unknown             */
    uint32_t  last_update_ms;   /* timestamp of the report this state came from */
} joycon_state_t;

/* ------------------------------------------------------------------ */
/* Unified (merged) controller state — the single output              */
/* ------------------------------------------------------------------ */

enum pad_button {
    PAD_A          = 1u << 0,
    PAD_B          = 1u << 1,
    PAD_X          = 1u << 2,
    PAD_Y          = 1u << 3,
    PAD_LB         = 1u << 4,
    PAD_RB         = 1u << 5,
    PAD_VIEW       = 1u << 6,   /* "Back"  */
    PAD_MENU       = 1u << 7,   /* "Start" */
    PAD_GUIDE      = 1u << 8,   /* "Xbox"  */
    PAD_L3         = 1u << 9,
    PAD_R3         = 1u << 10,
    PAD_DPAD_UP    = 1u << 11,
    PAD_DPAD_DOWN  = 1u << 12,
    PAD_DPAD_LEFT  = 1u << 13,
    PAD_DPAD_RIGHT = 1u << 14,
    PAD_SHARE      = 1u << 15,  /* Xbox Series "Share"; default source = Capture */
};

typedef struct {
    int16_t  lx, ly;           /* left stick  [-32767, 32767], +Y up */
    int16_t  rx, ry;           /* right stick [-32767, 32767], +Y up */
    uint16_t lt, rt;           /* triggers [0, 1023]                 */
    uint32_t buttons;          /* bitmask of pad_button              */

    bool degraded_left;        /* left half is holding-last or neutralized  */
    bool degraded_right;       /* right half is holding-last or neutralized */
} unified_pad_state_t;

/* Convenience: a fully-neutral unified state. */
static inline unified_pad_state_t unified_pad_neutral(void)
{
    unified_pad_state_t s = {0};
    return s;
}

#endif /* JCB_PAD_STATE_H */
