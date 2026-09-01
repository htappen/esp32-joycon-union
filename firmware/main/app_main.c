/*
 * app_main.c — task wiring for the Joy-Con Bridge.
 *
 *   joycon_host ──(queue)──▶ merge_engine ──▶ xbox_report ──▶ ble_xbox_hid
 *        │                        ▲                                 │
 *        └── link up/down ────────┘                                 │
 *   mode_manager ── Play/Config ──▶ web_server (Config only) ───────┘
 *   config_store  <── load/save ──  merge_config + bonded addresses
 *
 * BT stacks (Bluepad32 host + BTstack LE peripheral) run pinned to core 0;
 * the merge/output loop runs on core 1 (plan §3, NFR-1).
 */

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "ble_xbox_hid.h"
#include "config_store.h"
#include "joycon_host.h"
#include "merge_engine.h"
#include "mode_manager.h"
#include "status_led.h"
#include "web_server.h"
#include "xbox_report.h"

static const char *TAG = "app";
#define FW_VERSION "0.1.0-dev"

static QueueHandle_t   s_jc_queue;     /* joycon_state_t from BT task */
static merge_engine_t  s_merge;
static jcb_config_t    s_cfg;
static portMUX_TYPE    s_cfg_lock = portMUX_INITIALIZER_UNLOCKED;

static inline uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ------------------------------------------------------------------ */
/* joycon_host callbacks (BT task — keep short)                        */
/* ------------------------------------------------------------------ */

static void on_jc_state(const joycon_state_t *st, void *ctx)
{
    (void)ctx;
    xQueueSend(s_jc_queue, st, 0);   /* drop if full; next report is ~16 ms away */
}

static void on_jc_link(jc_side_t side, bool connected, void *ctx)
{
    (void)ctx;
    if (!connected) {
        joycon_state_t lost = { .side = side, .present = false };
        xQueueSend(s_jc_queue, &lost, 0);
    }

    /* Persist both addresses once we have a matched pair (FR-3). */
    if (connected && joycon_host_connected(JC_SIDE_LEFT) &&
        joycon_host_connected(JC_SIDE_RIGHT)) {
        bool vl, vr;
        portENTER_CRITICAL(&s_cfg_lock);
        joycon_host_get_addr(JC_SIDE_LEFT,  s_cfg.joycon_left.addr,  &vl);
        joycon_host_get_addr(JC_SIDE_RIGHT, s_cfg.joycon_right.addr, &vr);
        s_cfg.joycon_left.valid = vl;
        s_cfg.joycon_right.valid = vr;
        portEXIT_CRITICAL(&s_cfg_lock);
        config_store_save(&s_cfg);
    }
}

static void on_host_conn(bool connected, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "downstream host %s", connected ? "connected" : "disconnected");
}

/* ------------------------------------------------------------------ */
/* Output loop (core 1)                                                */
/* ------------------------------------------------------------------ */

static void update_led(void)
{
    if (mode_manager_current() == MODE_CONFIG) {
        status_led_set(joycon_host_pairing_enabled() ? LED_PAIRING : LED_CONFIG);
        return;
    }
    const unified_pad_state_t *o = merge_engine_output(&s_merge);
    bool l = joycon_host_connected(JC_SIDE_LEFT);
    bool r = joycon_host_connected(JC_SIDE_RIGHT);
    bool h = ble_xbox_hid_connected();

    if (o->degraded_left || o->degraded_right) status_led_set(LED_DEGRADED);
    else if (l && r && h)                      status_led_set(LED_PLAY_CONNECTED);
    else if (l || r || h)                      status_led_set(LED_PLAY_PARTIAL);
    else                                       status_led_set(LED_PLAY_SEARCHING);
}

static void output_task(void *arg)
{
    (void)arg;
    const TickType_t configured_period = pdMS_TO_TICKS(1000 / CONFIG_JCB_OUTPUT_RATE_HZ);
    const TickType_t period = configured_period > 0 ? configured_period : 1;
    TickType_t next = xTaskGetTickCount();
    uint32_t last_housekeep = 0;
    uint8_t report[XBOX_REPORT_LEN];

    for (;;) {
        joycon_state_t st;
        while (xQueueReceive(s_jc_queue, &st, 0) == pdTRUE) {
            merge_engine_update(&s_merge, &st, now_ms());
        }

        uint32_t t = now_ms();
        merge_engine_tick(&s_merge, t);

        size_t n = xbox_report_pack(merge_engine_output(&s_merge), report, sizeof(report));
        if (n == XBOX_REPORT_LEN) ble_xbox_hid_send_report(report, n);

        if (t - last_housekeep >= 100) {
            last_housekeep = t;
            update_led();

            uint8_t bl = joycon_host_battery(JC_SIDE_LEFT);
            uint8_t br = joycon_host_battery(JC_SIDE_RIGHT);
            uint8_t lo = 100;
            if (bl != 0xFF) lo = bl < lo ? bl : lo;
            if (br != 0xFF) lo = br < lo ? br : lo;
            ble_xbox_hid_set_battery(lo);            /* FR-14 */

            if (mode_manager_current() == MODE_CONFIG) web_server_broadcast();
        }

        vTaskDelayUntil(&next, period);
    }
}

/* ------------------------------------------------------------------ */
/* Web bridge                                                          */
/* ------------------------------------------------------------------ */

static void addr_str(const uint8_t a[6], bool valid, char *out, size_t n)
{
    if (!valid) { snprintf(out, n, "—"); return; }
    snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

static void web_get_state(char *buf, size_t len)
{
    char la[20], ra[20];
    bool vl, vr;
    uint8_t lb[6], rb[6];
    joycon_host_get_addr(JC_SIDE_LEFT, lb, &vl);
    joycon_host_get_addr(JC_SIDE_RIGHT, rb, &vr);
    addr_str(lb, vl, la, sizeof(la));
    addr_str(rb, vr, ra, sizeof(ra));
    const unified_pad_state_t *o = merge_engine_output(&s_merge);

    snprintf(buf, len,
        "{\"left\":{\"connected\":%s,\"addr\":\"%s\",\"battery\":%d},"
        "\"right\":{\"connected\":%s,\"addr\":\"%s\",\"battery\":%d},"
        "\"host\":{\"connected\":%s,\"remembered\":%s},"
        "\"degraded\":%s,\"pairing\":%s}",
        joycon_host_connected(JC_SIDE_LEFT) ? "true" : "false", la,
        joycon_host_battery(JC_SIDE_LEFT) == 0xFF ? -1 : joycon_host_battery(JC_SIDE_LEFT),
        joycon_host_connected(JC_SIDE_RIGHT) ? "true" : "false", ra,
        joycon_host_battery(JC_SIDE_RIGHT) == 0xFF ? -1 : joycon_host_battery(JC_SIDE_RIGHT),
        ble_xbox_hid_connected() ? "true" : "false",
        s_cfg.host.valid ? "true" : "false",
        (o->degraded_left || o->degraded_right) ? "true" : "false",
        joycon_host_pairing_enabled() ? "true" : "false");
}

static void web_get_live(char *buf, size_t len)
{
    const unified_pad_state_t *o = merge_engine_output(&s_merge);
    snprintf(buf, len,
        "{\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d,\"lt\":%u,\"rt\":%u,"
        "\"buttons\":%u,\"degraded_left\":%s,\"degraded_right\":%s,"
        "\"state\":{\"left\":%s,\"right\":%s,\"host\":%s},\"pairing\":%s}",
        o->lx, o->ly, o->rx, o->ry, o->lt, o->rt, (unsigned)o->buttons,
        o->degraded_left ? "true" : "false", o->degraded_right ? "true" : "false",
        joycon_host_connected(JC_SIDE_LEFT) ? "true" : "false",
        joycon_host_connected(JC_SIDE_RIGHT) ? "true" : "false",
        ble_xbox_hid_connected() ? "true" : "false",
        joycon_host_pairing_enabled() ? "true" : "false");
}

static void web_get_mapping(char *buf, size_t len)
{
    portENTER_CRITICAL(&s_cfg_lock);
    merge_config_t m = s_cfg.map;
    portEXIT_CRITICAL(&s_cfg_lock);
    snprintf(buf, len,
        "{\"ab_xbox_position\":%s,\"triggers_full_scale\":%s,\"deadzone\":%u,"
        "\"map_capture\":%u,\"map_left_sl\":%u,\"map_left_sr\":%u,"
        "\"map_right_sl\":%u,\"map_right_sr\":%u}",
        m.ab_xbox_position ? "true" : "false",
        m.triggers_full_scale ? "true" : "false",
        m.deadzone, (unsigned)m.map_capture, (unsigned)m.map_left_sl,
        (unsigned)m.map_left_sr, (unsigned)m.map_right_sl, (unsigned)m.map_right_sr);
}

static uint32_t json_u32(const cJSON *o, const char *k, uint32_t dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? (uint32_t)v->valuedouble : dflt;
}
static bool json_bool(const cJSON *o, const char *k, bool dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : dflt;
}

static esp_err_t web_put_mapping(const char *body, size_t body_len)
{
    (void)body_len;
    cJSON *o = cJSON_Parse(body);
    if (!o) return ESP_ERR_INVALID_ARG;

    portENTER_CRITICAL(&s_cfg_lock);
    merge_config_t *m = &s_cfg.map;
    m->ab_xbox_position    = json_bool(o, "ab_xbox_position", m->ab_xbox_position);
    m->triggers_full_scale = json_bool(o, "triggers_full_scale", m->triggers_full_scale);
    uint32_t dz = json_u32(o, "deadzone", m->deadzone);
    m->deadzone = dz > 32000 ? 32000 : (uint16_t)dz;
    m->map_capture  = json_u32(o, "map_capture",  m->map_capture);
    m->map_left_sl  = json_u32(o, "map_left_sl",  m->map_left_sl);
    m->map_left_sr  = json_u32(o, "map_left_sr",  m->map_left_sr);
    m->map_right_sl = json_u32(o, "map_right_sl", m->map_right_sl);
    m->map_right_sr = json_u32(o, "map_right_sr", m->map_right_sr);
    merge_config_t applied = *m;
    portEXIT_CRITICAL(&s_cfg_lock);

    cJSON_Delete(o);
    merge_engine_set_config(&s_merge, &applied);
    return config_store_save(&s_cfg);
}

static void web_get_version(char *buf, size_t len)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(buf, len, "{\"version\":\"%s\",\"idf\":\"%s\",\"bt_mac\":\"%02X%02X%02X%02X%02X%02X\"}",
             FW_VERSION, esp_get_idf_version(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void web_set_pairing(bool en) { joycon_host_set_pairing(en); }
static void web_request_mode(bool config)
{
    mode_manager_request(config ? MODE_CONFIG : MODE_PLAY);
}

static void web_joycon_forget(int side)
{
    joycon_host_forget(side == 1 ? JC_SIDE_RIGHT : JC_SIDE_LEFT);
    portENTER_CRITICAL(&s_cfg_lock);
    if (side == 1) memset(&s_cfg.joycon_right, 0, sizeof(s_cfg.joycon_right));
    else           memset(&s_cfg.joycon_left, 0, sizeof(s_cfg.joycon_left));
    portEXIT_CRITICAL(&s_cfg_lock);
    config_store_save(&s_cfg);
}

static void web_host_forget(void)
{
    ble_xbox_hid_forget_host();
    portENTER_CRITICAL(&s_cfg_lock);
    memset(&s_cfg.host, 0, sizeof(s_cfg.host));
    portEXIT_CRITICAL(&s_cfg_lock);
    config_store_save(&s_cfg);
}

static void web_reboot(void) { esp_restart(); }

#if CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
static int mode_command(int argc, char **argv)
{
    if (argc != 2 || (strcmp(argv[1], "config") != 0 && strcmp(argv[1], "play") != 0)) {
        printf("usage: mode config|play\n");
        return 1;
    }

    mode_manager_request(strcmp(argv[1], "config") == 0 ? MODE_CONFIG : MODE_PLAY);
    printf("requested %s mode\n", argv[1]);
    return 0;
}

static void register_mode_command(void)
{
    const esp_console_cmd_t command = {
        .command = "mode",
        .help = "switch between Play and Config modes",
        .hint = "config|play",
        .func = mode_command,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&command));
}
#else
static void register_mode_command(void) {}
#endif

static void do_factory_reset(void)
{
    joycon_host_forget_all();
    ble_xbox_hid_forget_host();
    config_store_factory_reset();
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static const web_bridge_t s_web_bridge = {
    .get_state_json   = web_get_state,
    .get_live_json    = web_get_live,
    .get_mapping_json = web_get_mapping,
    .put_mapping_json = web_put_mapping,
    .get_version_json = web_get_version,
    .set_pairing      = web_set_pairing,
    .request_mode     = web_request_mode,
    .joycon_forget    = web_joycon_forget,
    .host_forget      = web_host_forget,
    .reboot           = web_reboot,
    .factory_reset    = do_factory_reset,
};

/* ------------------------------------------------------------------ */
/* Mode transitions                                                    */
/* ------------------------------------------------------------------ */

static void enter_config(void)
{
    /* FR-22 fallback (Q5 = allowed): suspend the host link while the portal
     * is open so Wi-Fi + BT coexistence isn't a problem. */
    ble_xbox_hid_suspend();
    joycon_host_set_pairing(true);
    static const web_wifi_cfg_t wifi = {
        .sta_ssid      = CONFIG_JCB_WIFI_STA_SSID,
        .sta_pass      = CONFIG_JCB_WIFI_STA_PASS,
        .softap_ssid   = CONFIG_JCB_SOFTAP_SSID,
        .softap_pass   = CONFIG_JCB_SOFTAP_PASS,
        .mdns_hostname = "joycon-bridge",
    };
    if (web_server_start(&s_web_bridge, &wifi) != ESP_OK) {
        ESP_LOGE(TAG, "config portal failed to start");
        ble_xbox_hid_resume();
        mode_manager_request(MODE_PLAY);
    }
}

static void enter_play(void)
{
    joycon_host_set_pairing(false);
    web_server_stop();
    ble_xbox_hid_resume();
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

void app_main(void)
{
    ESP_LOGI(TAG, "Joy-Con Bridge %s (idf %s)", FW_VERSION, esp_get_idf_version());

    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_LOGI(TAG, "startup: nvs ready");

#if CONFIG_JCB_STATUS_LED_ENABLE
    status_led_init();
    ESP_LOGI(TAG, "startup: led ready");
    status_led_set(LED_BOOT);
#else
    ESP_LOGI(TAG, "startup: led disabled by build configuration");
#endif

    config_store_init();
    config_store_load(&s_cfg);
    ESP_LOGI(TAG, "startup: config ready");
    merge_engine_init(&s_merge, &s_cfg.map);

    s_jc_queue = xQueueCreate(16, sizeof(joycon_state_t));
    ESP_LOGI(TAG, "startup: queue ready");

    joycon_host_cfg_t jhc = {
        .on_state = on_jc_state,
        .on_link  = on_jc_link,
    };
    if (s_cfg.joycon_left.valid)
        memcpy(jhc.remembered_left, s_cfg.joycon_left.addr, 6);
    if (s_cfg.joycon_right.valid)
        memcpy(jhc.remembered_right, s_cfg.joycon_right.addr, 6);
    ESP_ERROR_CHECK(joycon_host_init(&jhc));
    ESP_LOGI(TAG, "startup: joycon host ready");

    ble_xbox_cfg_t bxc = { .on_conn = on_host_conn, .allow_new_pairing = true };
    if (s_cfg.host.valid) memcpy(bxc.remembered_host, s_cfg.host.addr, 6);
    ESP_ERROR_CHECK(ble_xbox_hid_init(&bxc));
    ESP_LOGI(TAG, "startup: BLE host ready");

    static const mode_manager_cb_t mmc = {
        .enter_config  = enter_config,
        .enter_play    = enter_play,
    };
    mode_manager_init(&mmc);
    register_mode_command();
    if (!s_cfg.joycon_left.valid || !s_cfg.joycon_right.valid) {
        ESP_LOGI(TAG, "startup: fewer than two remembered controllers; entering Config Mode");
        mode_manager_request(MODE_CONFIG);
    }
    ESP_LOGI(TAG, "startup: mode manager ready");

    xTaskCreatePinnedToCore(output_task, "output", 4096, NULL, 6, NULL, 1);

    ESP_LOGI(TAG, "running (heap %u)", (unsigned)esp_get_free_heap_size());
}
