/*
 * joycon_decode.h — parse Nintendo Switch (gen 1) Joy-Con Bluetooth HID
 * standard input report 0x30 into a normalized joycon_state_t, and decode
 * the factory stick calibration read from Joy-Con SPI flash.
 *
 * Pure C (no ESP-IDF) so it is host-testable.  References:
 *   dekuNukem/Nintendo_Switch_Reverse_Engineering
 *     - bluetooth_hid_notes.md         (report 0x30 layout)
 *     - spi_flash_notes.md             (calibration at 0x603D / 0x6046 / 0x6080)
 *
 * Orientation: the merged controller holds each Joy-Con vertically (grip
 * style), so raw stick axes need no rotation — raw +X is right, raw +Y is up,
 * which already matches pad_state.h.
 */
#ifndef JCB_JOYCON_DECODE_H
#define JCB_JOYCON_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pad_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-axis calibration: center plus travel above/below center (raw 12-bit). */
typedef struct {
    uint16_t x_center, y_center;
    uint16_t x_below,  x_above;   /* magnitudes (raw counts) */
    uint16_t y_below,  y_above;
    uint16_t deadzone;            /* from 0x6080 params; 0 => derive a default */
    bool     loaded;
} jc_stick_calib_t;

/* Nominal calibration used until the SPI read succeeds. */
void joycon_decode_default_calib(jc_stick_calib_t *c);

/*
 * Decode a 9-byte factory stick-calibration blob.
 *   side == JC_SIDE_LEFT  : blob from SPI 0x603D (9 bytes)
 *   side == JC_SIDE_RIGHT : blob from SPI 0x6046 (9 bytes)
 * The left/right blobs order their (max, center, min) groups differently;
 * this function handles both.
 */
void joycon_decode_stick_calib(jc_side_t side, const uint8_t blob9[9],
                               jc_stick_calib_t *out);

/* Merge the 2-byte deadzone/range params from SPI 0x6080 (+3 offset). */
void joycon_decode_stick_params(const uint8_t params[2], jc_stick_calib_t *io);

/*
 * Parse a standard input report.  `body` points at the first byte AFTER the
 * 0x30 report-ID byte (i.e. body[0] == timer).  `len` must be >= 11.
 * Returns false (and leaves *out untouched) on a malformed/short report.
 */
bool joycon_decode_input_report(jc_side_t side, const uint8_t *body, size_t len,
                                const jc_stick_calib_t *calib,
                                joycon_state_t *out, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* JCB_JOYCON_DECODE_H */
