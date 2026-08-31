/*
 * web_server.h — Config Mode Wi-Fi + single-page config portal
 * (FR-18..FR-22). Vanilla-JS SPA embedded in the binary (FR-19).
 *
 * Only started when entering Config Mode; web_server_stop() tears down the
 * HTTP server and Wi-Fi so Play Mode keeps the radio for BT (NFR-5).
 */
#ifndef JCB_WEB_SERVER_H
#define JCB_WEB_SERVER_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Callbacks into the rest of the firmware. All JSON writers must NUL-terminate
 * and must not overflow `len`. */
typedef struct {
    void      (*get_state_json)(char *buf, size_t len);     /* GET  /api/state    */
    void      (*get_live_json)(char *buf, size_t len);      /* WS   /ws           */
    void      (*get_mapping_json)(char *buf, size_t len);   /* GET  /api/mapping  */
    esp_err_t (*put_mapping_json)(const char *body, size_t len); /* PUT /api/mapping */
    void      (*get_version_json)(char *buf, size_t len);   /* GET  /api/version  */
    void      (*set_pairing)(bool enable);                  /* POST /api/pairing/{start,stop} */
    void      (*joycon_forget)(int side /*0=L,1=R*/);       /* POST /api/joycon/{L,R}/forget */
    void      (*host_forget)(void);                         /* POST /api/host/forget */
    void      (*reboot)(void);                              /* POST /api/reboot   */
    void      (*factory_reset)(void);                       /* POST /api/factory-reset */
} web_bridge_t;

/* Wi-Fi credentials for Config Mode. `main` fills these from Kconfig so this
 * component stays independent of the project's Kconfig symbols. If
 * `sta_ssid` is empty, Config Mode brings up the SoftAP directly; otherwise
 * it tries STA first and falls back to SoftAP on join failure. */
typedef struct {
    const char *sta_ssid;
    const char *sta_pass;
    const char *softap_ssid;
    const char *softap_pass;   /* < 8 chars => open AP */
    const char *mdns_hostname; /* e.g. "joycon-bridge" */
} web_wifi_cfg_t;

/* Bring up Wi-Fi + mDNS + the HTTP server. */
esp_err_t web_server_start(const web_bridge_t *bridge, const web_wifi_cfg_t *wifi);

void web_server_stop(void);

/* Push a status frame to all connected WebSocket clients (>= 5 Hz, FR-21).
 * Safe to call from any task. */
void web_server_broadcast(void);

#ifdef __cplusplus
}
#endif

#endif /* JCB_WEB_SERVER_H */
