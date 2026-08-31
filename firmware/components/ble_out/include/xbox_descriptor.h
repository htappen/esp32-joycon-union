/*
 * xbox_descriptor.h — HID report descriptor + identity for the emulated
 * Xbox One S / Series wireless controller (FR-11).
 *
 * The report layout produced by this descriptor is byte-for-byte what
 * merge/xbox_report.c packs (see that header for the field table). It is
 * modelled on the Xbox One S Bluetooth gamepad and cross-checked against
 * ESP32-BLE-CompositeHID (XboxGamepadDevice) and the Linux xpadneo driver.
 *
 * !! Phase 3 exit gate: this MUST be validated on Windows 11 (expect XInput
 *    binding) and Android (standard gamepad). If Windows refuses to XInput-
 *    bind it, fall back to a generic BLE HID gamepad descriptor (still works
 *    on Win + Android, no XInput) — see plan §6 risk table.
 */
#ifndef JCB_XBOX_DESCRIPTOR_H
#define JCB_XBOX_DESCRIPTOR_H

#include <stdint.h>

/* --- BLE identity ------------------------------------------------------- */
#define XBOX_VID          0x045E   /* Microsoft                              */
#define XBOX_PID          0x0B13   /* Xbox Wireless Controller (Series)      */
#define XBOX_PID_ONE_S    0x02FD   /* alt: Xbox One S — swap if Win prefers  */
#define XBOX_VERSION      0x0903
#define XBOX_DEVICE_NAME  "Xbox Wireless Controller"
#define XBOX_MANUFACTURER "Microsoft"
#define XBOX_APPEARANCE   0x03C4   /* Generic Gamepad                        */

#define XBOX_HID_REPORT_ID 0x01

/* PnP ID characteristic value: source(USB=0x02), VID(LE), PID(LE), ver(LE) */
#define XBOX_PNP_ID_BYTES  0x02, 0x5E, 0x04, 0x13, 0x0B, 0x03, 0x09

/* --- HID report descriptor -------------------------------------------- */
static const uint8_t xbox_hid_report_descriptor[] = {
    0x05, 0x01,             /* Usage Page (Generic Desktop)               */
    0x09, 0x05,             /* Usage (Game Pad)                           */
    0xA1, 0x01,             /* Collection (Application)                   */
    0x85, XBOX_HID_REPORT_ID, /*   Report ID (1)                          */

    /* --- Sticks: X, Y (left), Z, Rz (right) — 16-bit each ------------- */
    0x09, 0x01,             /*   Usage (Pointer)                          */
    0xA1, 0x00,             /*   Collection (Physical)                    */
    0x09, 0x30,             /*     Usage (X)                              */
    0x09, 0x31,             /*     Usage (Y)                              */
    0x09, 0x32,             /*     Usage (Z)                              */
    0x09, 0x35,             /*     Usage (Rz)                             */
    0x15, 0x00,             /*     Logical Minimum (0)                    */
    0x27, 0xFF, 0xFF, 0x00, 0x00, /* Logical Maximum (65535)              */
    0x75, 0x10,             /*     Report Size (16)                       */
    0x95, 0x04,             /*     Report Count (4)                       */
    0x81, 0x02,             /*     Input (Data,Var,Abs)                   */
    0xC0,                   /*   End Collection                           */

    /* --- Triggers: Brake (LT), Accelerator (RT) — 10-bit ------------- */
    0x05, 0x02,             /*   Usage Page (Simulation Controls)         */
    0x09, 0xC5,             /*   Usage (Brake)                            */
    0x09, 0xC4,             /*   Usage (Accelerator)                      */
    0x15, 0x00,             /*   Logical Minimum (0)                      */
    0x26, 0xFF, 0x03,       /*   Logical Maximum (1023)                   */
    0x75, 0x10,             /*   Report Size (16)                         */
    0x95, 0x02,             /*   Report Count (2)                         */
    0x81, 0x02,             /*   Input (Data,Var,Abs)                     */

    /* --- D-pad: 8-way hat + 4-bit pad -------------------------------- */
    0x05, 0x01,             /*   Usage Page (Generic Desktop)             */
    0x09, 0x39,             /*   Usage (Hat switch)                       */
    0x15, 0x01,             /*   Logical Minimum (1)                      */
    0x25, 0x08,             /*   Logical Maximum (8)                      */
    0x35, 0x00,             /*   Physical Minimum (0)                     */
    0x46, 0x3B, 0x01,       /*   Physical Maximum (315)                   */
    0x66, 0x14, 0x00,       /*   Unit (Degrees)                           */
    0x75, 0x04,             /*   Report Size (4)                          */
    0x95, 0x01,             /*   Report Count (1)                         */
    0x81, 0x42,             /*   Input (Data,Var,Abs,Null)                */
    0x75, 0x04,             /*   Report Size (4)                          */
    0x95, 0x01,             /*   Report Count (1)                         */
    0x81, 0x03,             /*   Input (Const) — padding                  */

    /* --- Buttons 1..16 (bit map matches XR_BTN_*) -------------------- */
    0x05, 0x09,             /*   Usage Page (Button)                      */
    0x19, 0x01,             /*   Usage Minimum (Button 1)                 */
    0x29, 0x10,             /*   Usage Maximum (Button 16)                */
    0x15, 0x00,             /*   Logical Minimum (0)                      */
    0x25, 0x01,             /*   Logical Maximum (1)                      */
    0x75, 0x01,             /*   Report Size (1)                          */
    0x95, 0x10,             /*   Report Count (16)                        */
    0x81, 0x02,             /*   Input (Data,Var,Abs)                     */

    /* --- Share / Capture button (1 bit + 7 pad) --------------------- */
    0x05, 0x0C,             /*   Usage Page (Consumer)                    */
    0x0A, 0xB2, 0x00,       /*   Usage (Record)                           */
    0x15, 0x00,             /*   Logical Minimum (0)                      */
    0x25, 0x01,             /*   Logical Maximum (1)                      */
    0x75, 0x01,             /*   Report Size (1)                          */
    0x95, 0x01,             /*   Report Count (1)                         */
    0x81, 0x02,             /*   Input (Data,Var,Abs)                     */
    0x75, 0x07,             /*   Report Size (7)                          */
    0x95, 0x01,             /*   Report Count (1)                         */
    0x81, 0x03,             /*   Input (Const) — padding                  */

    0xC0                    /* End Collection                             */
};

/* Report body length excluding the report-ID byte (see xbox_report.h). */
#define XBOX_HID_REPORT_LEN 16

#endif /* JCB_XBOX_DESCRIPTOR_H */
