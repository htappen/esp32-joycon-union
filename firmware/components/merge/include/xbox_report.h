/*
 * xbox_report.h — pack a unified_pad_state_t into the Xbox One S / Series
 * Bluetooth HID input-report byte layout.
 *
 * The layout matches the descriptor in ble_out/xbox_descriptor.h, which is
 * modelled on the Xbox One S BT gamepad (MS VID 0x045E) as implemented by
 * ESP32-BLE-CompositeHID and decoded by the Linux xpadneo driver.  Keeping
 * this file free of ESP-IDF deps lets the mapping be golden-vector tested on
 * a host PC (firmware/test/).
 *
 * Report body (no report ID; 16 bytes, little-endian):
 *
 *   off  size  field
 *   0    2     LX   uint16  0..65535, center 32768, left=0    right=65535
 *   2    2     LY   uint16  0..65535, center 32768, up=0      down=65535
 *   4    2     RX   uint16  0..65535, center 32768, left=0    right=65535
 *   6    2     RY   uint16  0..65535, center 32768, up=0      down=65535
 *   8    2     LT   uint16  0..1023
 *   10   2     RT   uint16  0..1023
 *   12   1     HAT  uint8   0=neutral, 1=N,2=NE,3=E,4=SE,5=S,6=SW,7=W,8=NW
 *   13   2     BTN  uint16  bitmask (see XR_BTN_*)
 *   15   1     SHARE uint8  bit0 = Share/Capture
 */
#ifndef JCB_XBOX_REPORT_H
#define JCB_XBOX_REPORT_H

#include <stddef.h>
#include <stdint.h>

#include "pad_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XBOX_REPORT_LEN 16

/* Button bits in the 16-bit BTN field (matches xpadneo / CompositeHID). */
enum xbox_report_button {
    XR_BTN_A     = 0x0001,
    XR_BTN_B     = 0x0002,
    XR_BTN_X     = 0x0008,
    XR_BTN_Y     = 0x0010,
    XR_BTN_LB    = 0x0040,
    XR_BTN_RB    = 0x0080,
    XR_BTN_VIEW  = 0x0400,   /* "Back"  */
    XR_BTN_MENU  = 0x0800,   /* "Start" */
    XR_BTN_GUIDE = 0x1000,   /* "Xbox"  */
    XR_BTN_L3    = 0x2000,
    XR_BTN_R3    = 0x4000,
};

/*
 * Pack `in` into `buf`.  `buf` must hold at least XBOX_REPORT_LEN bytes.
 * Returns XBOX_REPORT_LEN, or 0 if buf is NULL / too small.
 */
size_t xbox_report_pack(const unified_pad_state_t *in, uint8_t *buf, size_t buflen);

/* Exposed for unit tests: dpad bitmask -> HID hat value (0..8). */
uint8_t xbox_report_hat(uint32_t pad_buttons);

/* Exposed for unit tests: int16 stick axis -> uint16 HID axis.
 * `invert` flips direction (used for the Y axes). */
uint16_t xbox_report_axis(int16_t v, int invert);

#ifdef __cplusplus
}
#endif

#endif /* JCB_XBOX_REPORT_H */
