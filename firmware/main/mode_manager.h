/*
 * mode_manager.h — Play/Config state machine + the BOOT/GPIO0 user button
 * (FR-23) + boot chord for factory reset (FR-25).
 *
 *   long-press (>= JCB_MODE_BUTTON_LONG_MS)  -> toggle Play <-> Config
 *   held at boot for ~3 s                    -> factory reset + reboot
 */
#ifndef JCB_MODE_MANAGER_H
#define JCB_MODE_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MODE_PLAY = 0, MODE_CONFIG } jcb_mode_t;

typedef struct {
    void (*enter_config)(void);   /* start Wi-Fi + portal, suspend host if needed */
    void (*enter_play)(void);     /* stop portal, resume host                     */
    void (*factory_reset)(void);  /* wipe NVS + bonds, then reboot                */
} mode_manager_cb_t;

void       mode_manager_init(const mode_manager_cb_t *cb);
jcb_mode_t mode_manager_current(void);

/* Let other code (e.g. a web UI "return to Play") request a transition. */
void mode_manager_request(jcb_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* JCB_MODE_MANAGER_H */
