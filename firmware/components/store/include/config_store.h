/*
 * config_store.h — versioned NVS persistence (FR-24, FR-25).
 *
 * Persists: the two bonded Joy-Con addresses, the merge/button-map config,
 * the bonded host address, and the A/B position preference (inside map).
 * BT link keys themselves are owned by BTstack's own bond storage; factory
 * reset clears both (see config_store_factory_reset + ble_xbox_hid /
 * joycon_host bond-wipe hooks).
 */
#ifndef JCB_CONFIG_STORE_H
#define JCB_CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "merge_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JCB_CONFIG_VERSION 1

typedef struct {
    uint8_t addr[6];
    bool    valid;
} bt_addr_entry_t;

typedef struct {
    uint16_t        version;
    merge_config_t  map;            /* button map + tuning (FR-16/17) */
    bt_addr_entry_t joycon_left;    /* FR-3 */
    bt_addr_entry_t joycon_right;   /* FR-3 */
    bt_addr_entry_t host;           /* FR-15 */
} jcb_config_t;

/* Fill `cfg` with compile-time defaults (no persistence touched). */
void config_store_defaults(jcb_config_t *cfg);

/* Open the NVS namespace. Call once after nvs_flash_init(). */
esp_err_t config_store_init(void);

/* Load persisted config into `cfg`; on absence or an older version fills
 * defaults and (for an older version) migrates + rewrites. Never fails hard. */
void config_store_load(jcb_config_t *cfg);

/* Persist `cfg` (writes only if the serialized blob changed). */
esp_err_t config_store_save(const jcb_config_t *cfg);

/* Erase all persisted config (FR-25). Caller is responsible for also wiping
 * BTstack bonds and rebooting. */
esp_err_t config_store_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* JCB_CONFIG_STORE_H */
