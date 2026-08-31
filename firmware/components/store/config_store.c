/* config_store.c — see config_store.h */

#include "config_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "cfg";
static const char *NS  = "jcb";
static const char *KEY = "cfg";

void config_store_defaults(jcb_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->version = JCB_CONFIG_VERSION;
    merge_config_defaults(&cfg->map);
#ifdef CONFIG_JCB_AB_XBOX_POSITION_DEFAULT
    cfg->map.ab_xbox_position = true;
#endif
}

esp_err_t config_store_init(void)
{
    /* nvs_flash_init() is the caller's responsibility (app_main). Here we
     * just probe that the namespace opens. */
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err == ESP_OK) nvs_close(h);
    return err;
}

/* On-flash blob is just the packed struct prefixed by its version. Keeping
 * the struct POD (no pointers) makes this a straight memcpy; migration reads
 * the leading version field and upconverts. */
void config_store_load(jcb_config_t *cfg)
{
    config_store_defaults(cfg);

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed, using defaults");
        return;
    }

    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, KEY, NULL, &len);
    if (err != ESP_OK || len == 0) {
        ESP_LOGI(TAG, "no stored config, using defaults");
        nvs_close(h);
        return;
    }

    if (len == sizeof(jcb_config_t)) {
        jcb_config_t tmp;
        if (nvs_get_blob(h, KEY, &tmp, &len) == ESP_OK) {
            if (tmp.version == JCB_CONFIG_VERSION) {
                *cfg = tmp;
                ESP_LOGI(TAG, "config loaded (v%u)", tmp.version);
            } else if (tmp.version < JCB_CONFIG_VERSION) {
                ESP_LOGW(TAG, "migrating config v%u -> v%u", tmp.version,
                         JCB_CONFIG_VERSION);
                /* v1 is the first version; future migrations slot in here,
                 * field by field, before the rewrite. */
                *cfg = tmp;
                cfg->version = JCB_CONFIG_VERSION;
                nvs_close(h);
                config_store_save(cfg);
                return;
            } else {
                ESP_LOGW(TAG, "stored config newer (v%u) than firmware; ignoring",
                         tmp.version);
            }
        }
    } else {
        ESP_LOGW(TAG, "stored config size %u != %u; ignoring", (unsigned)len,
                 (unsigned)sizeof(jcb_config_t));
    }

    nvs_close(h);
}

esp_err_t config_store_save(const jcb_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    /* Skip the write if unchanged (flash-wear + coexistence hygiene). */
    jcb_config_t cur;
    size_t len = sizeof(cur);
    if (nvs_get_blob(h, KEY, &cur, &len) == ESP_OK && len == sizeof(cur) &&
        memcmp(&cur, cfg, sizeof(cur)) == 0) {
        nvs_close(h);
        return ESP_OK;
    }

    err = nvs_set_blob(h, KEY, cfg, sizeof(*cfg));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "config saved (%s)", esp_err_to_name(err));
    return err;
}

esp_err_t config_store_factory_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(h);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGW(TAG, "factory reset: NVS namespace cleared (%s)", esp_err_to_name(err));
    return err;
}
