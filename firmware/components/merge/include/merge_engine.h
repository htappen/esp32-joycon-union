/*
 * merge_engine.h — combine one left + one right Joy-Con into a single
 * unified_pad_state_t (requirements FR-7..FR-10).
 *
 * Topology-independent: no ESP-IDF dependency, host-unit-tested.
 *
 * Degraded-half state machine (per half), FR-9 / FR-10:
 *
 *     ACTIVE --lost--> HOLD --(hold_ms elapsed)--> NEUTRAL
 *       ^  \                                         /
 *       |   \----------------- report --------------/
 *       +--------------------- report --------------+
 *
 *   ACTIVE  : half reflects the live Joy-Con state.
 *   HOLD    : link lost <= hold_ms ago; keep emitting the last-known values,
 *             degraded flag already set.
 *   NEUTRAL : link lost > hold_ms ago; emit centered stick / no buttons,
 *             degraded flag set.
 *   A fresh report at any time returns the half to ACTIVE and clears degraded.
 */
#ifndef JCB_MERGE_ENGINE_H
#define JCB_MERGE_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#include "pad_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tunables the web UI can edit; persisted by config_store (FR-16, FR-17). */
typedef struct {
    /* A/B/X/Y position.  false (default) = Nintendo position: the physical
     * bottom button (Nintendo B) becomes Xbox A.  true = match the printed
     * label: physical A -> Xbox A. (requirements §4.4) */
    bool ab_xbox_position;

    /* Radial deadzone applied to each stick, in stick units [0, 32767]. */
    uint16_t deadzone;

    /* Digital ZL/ZR -> trigger value.  true (default): 0 or 1023 (full).
     * false: 0 or `trigger_digital_value`. */
    bool     triggers_full_scale;
    uint16_t trigger_digital_value; /* used only when !triggers_full_scale, [0,1023] */

    /* How long to hold a lost half's last state before neutralizing it (ms). */
    uint32_t hold_ms;                /* default 200 (FR-9) */

    /* Assignable buttons (requirements §4.4).  Each is a bitmask of exactly
     * one pad_button, or 0 for "unmapped". */
    uint32_t map_capture;            /* default PAD_SHARE */
    uint32_t map_left_sl;            /* default 0 */
    uint32_t map_left_sr;            /* default 0 */
    uint32_t map_right_sl;           /* default 0 */
    uint32_t map_right_sr;           /* default 0 */
} merge_config_t;

/* Fill `cfg` with the documented defaults. */
void merge_config_defaults(merge_config_t *cfg);

typedef enum {
    HALF_ACTIVE = 0,
    HALF_HOLD,
    HALF_NEUTRAL,
} merge_half_state_t;

typedef struct {
    merge_config_t    cfg;

    joycon_state_t    left;        /* last-known left  Joy-Con state */
    joycon_state_t    right;       /* last-known right Joy-Con state */

    merge_half_state_t left_state;
    merge_half_state_t right_state;
    uint32_t          left_lost_ms;
    uint32_t          right_lost_ms;

    unified_pad_state_t out;       /* recomputed by tick()/update_*  */
} merge_engine_t;

/* Initialize with a config (copied).  Both halves start NEUTRAL/degraded. */
void merge_engine_init(merge_engine_t *m, const merge_config_t *cfg);

/* Replace the live config (copied).  Takes effect on the next recompute. */
void merge_engine_set_config(merge_engine_t *m, const merge_config_t *cfg);

/* Feed a fresh decoded Joy-Con report.  `now_ms` is the current time.
 * `st->side` selects the half; a mismatched side is ignored. */
void merge_engine_update(merge_engine_t *m, const joycon_state_t *st, uint32_t now_ms);

/* Notify that a half's link dropped (FR-9).  Starts the HOLD window. */
void merge_engine_notify_lost(merge_engine_t *m, jc_side_t side, uint32_t now_ms);

/* Advance the hold/neutralize timers and recompute the output.  Call this
 * from the output loop even when no new Joy-Con report arrived (>= 100 Hz). */
void merge_engine_tick(merge_engine_t *m, uint32_t now_ms);

/* Current merged output (valid after init; refreshed by update/tick). */
const unified_pad_state_t *merge_engine_output(const merge_engine_t *m);

#ifdef __cplusplus
}
#endif

#endif /* JCB_MERGE_ENGINE_H */
