/* xbox_report.c — see xbox_report.h */

#include "xbox_report.h"

uint16_t xbox_report_axis(int16_t v, int invert)
{
    if (invert) v = (v == -32768) ? 32767 : (int16_t)(-v);
    int32_t out = 32768 + (int32_t)v;   /* v in [-32767,32767] -> [1,65535] */
    if (out < 0)     out = 0;
    if (out > 65535) out = 65535;
    return (uint16_t)out;
}

uint8_t xbox_report_hat(uint32_t b)
{
    int up    = (b & PAD_DPAD_UP)    ? 1 : 0;
    int down  = (b & PAD_DPAD_DOWN)  ? 1 : 0;
    int left  = (b & PAD_DPAD_LEFT)  ? 1 : 0;
    int right = (b & PAD_DPAD_RIGHT) ? 1 : 0;

    /* Opposing pairs cancel. */
    if (up && down)    { up = down = 0; }
    if (left && right) { left = right = 0; }

    if (up && right)   return 2;
    if (down && right) return 4;
    if (down && left)  return 6;
    if (up && left)    return 8;
    if (up)            return 1;
    if (right)         return 3;
    if (down)          return 5;
    if (left)          return 7;
    return 0;
}

static uint16_t clamp_trig(uint16_t t) { return t > 1023 ? 1023 : t; }

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

size_t xbox_report_pack(const unified_pad_state_t *in, uint8_t *buf, size_t buflen)
{
    if (!in || !buf || buflen < XBOX_REPORT_LEN) return 0;

    put_u16(&buf[0],  xbox_report_axis(in->lx, 0));
    put_u16(&buf[2],  xbox_report_axis(in->ly, 1));   /* HID: up = 0 */
    put_u16(&buf[4],  xbox_report_axis(in->rx, 0));
    put_u16(&buf[6],  xbox_report_axis(in->ry, 1));
    put_u16(&buf[8],  clamp_trig(in->lt));
    put_u16(&buf[10], clamp_trig(in->rt));

    buf[12] = xbox_report_hat(in->buttons);

    uint16_t b = 0;
    uint32_t pb = in->buttons;
    if (pb & PAD_A)     b |= XR_BTN_A;
    if (pb & PAD_B)     b |= XR_BTN_B;
    if (pb & PAD_X)     b |= XR_BTN_X;
    if (pb & PAD_Y)     b |= XR_BTN_Y;
    if (pb & PAD_LB)    b |= XR_BTN_LB;
    if (pb & PAD_RB)    b |= XR_BTN_RB;
    if (pb & PAD_VIEW)  b |= XR_BTN_VIEW;
    if (pb & PAD_MENU)  b |= XR_BTN_MENU;
    if (pb & PAD_GUIDE) b |= XR_BTN_GUIDE;
    if (pb & PAD_L3)    b |= XR_BTN_L3;
    if (pb & PAD_R3)    b |= XR_BTN_R3;
    put_u16(&buf[13], b);

    buf[15] = (pb & PAD_SHARE) ? 0x01 : 0x00;

    return XBOX_REPORT_LEN;
}
