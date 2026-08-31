/* status_led.h — single WS2812 mode/connection indicator (hardware §3). */
#ifndef JCB_STATUS_LED_H
#define JCB_STATUS_LED_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_BOOT = 0,          /* white, solid — starting up            */
    LED_PLAY_SEARCHING,    /* blue, slow pulse — waiting for Joy-Cons */
    LED_PLAY_CONNECTED,    /* green, solid — 2 Joy-Cons + host ok    */
    LED_PLAY_PARTIAL,      /* green, slow pulse — some links up      */
    LED_DEGRADED,          /* amber, blink — a half dropped (FR-9)   */
    LED_CONFIG,            /* cyan, breathing — Config Mode          */
    LED_PAIRING,           /* magenta, fast blink — discovery on     */
    LED_ERROR,             /* red, fast blink — fault / recovering   */
} led_pattern_t;

void status_led_init(void);
void status_led_set(led_pattern_t p);

#ifdef __cplusplus
}
#endif

#endif /* JCB_STATUS_LED_H */
