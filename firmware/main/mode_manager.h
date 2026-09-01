/*
 * mode_manager.h — Play/Config state machine.
 *
 *   mode_manager_request() -> asynchronous Play/Config transition
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
} mode_manager_cb_t;

void       mode_manager_init(const mode_manager_cb_t *cb);
jcb_mode_t mode_manager_current(void);

/* Let other code (e.g. a web UI "return to Play") request a transition. */
void mode_manager_request(jcb_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* JCB_MODE_MANAGER_H */
