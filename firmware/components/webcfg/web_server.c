/* web_server.c — see web_server.h */

#include "web_server.h"

#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs_flash.h"

static const char *TAG = "web";

/* Embedded SPA assets (see CMakeLists.txt EMBED_FILES). */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");

static struct {
    web_bridge_t bridge;
    web_wifi_cfg_t wifi;
    httpd_handle_t http;
    bool          running;
    int           ws_fds[4];   /* connected /ws client sockets, 0 = free */
} S;

/* ------------------------------------------------------------------ */
/* Wi-Fi                                                               */
/* ------------------------------------------------------------------ */

static bool wifi_start_sta(void)
{
    if (!S.wifi.sta_ssid || S.wifi.sta_ssid[0] == '\0') return false;

    esp_netif_create_default_wifi_sta();
    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, S.wifi.sta_ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, S.wifi.sta_pass ? S.wifi.sta_pass : "",
            sizeof(wc.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_err_t err = esp_wifi_connect();

    /* Wait briefly for a connection; the caller falls back to SoftAP. */
    for (int i = 0; i < 60; i++) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "STA connected to %s", S.wifi.sta_ssid);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    ESP_LOGW(TAG, "STA join failed (%s); falling back to SoftAP", esp_err_to_name(err));
    esp_wifi_stop();
    return false;
}

static void wifi_start_softap(void)
{
    const char *ssid = S.wifi.softap_ssid ? S.wifi.softap_ssid : "joycon-bridge";
    const char *pass = S.wifi.softap_pass ? S.wifi.softap_pass : "";

    esp_netif_create_default_wifi_ap();
    wifi_config_t wc = {0};
    strlcpy((char *)wc.ap.ssid, ssid, sizeof(wc.ap.ssid));
    wc.ap.ssid_len = strlen(ssid);
    strlcpy((char *)wc.ap.password, pass, sizeof(wc.ap.password));
    wc.ap.max_connection = 4;
    wc.ap.authmode = strlen(pass) >= 8 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP '%s' up — UI at http://192.168.4.1/", ssid);
}

static void wifi_up(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    if (!wifi_start_sta()) wifi_start_softap();

    const char *host = S.wifi.mdns_hostname ? S.wifi.mdns_hostname : "joycon-bridge";
    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(host);
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        ESP_LOGI(TAG, "mDNS: http://%s.local/", host);
    }
}

static void wifi_down(void)
{
    mdns_free();
    esp_wifi_stop();
    esp_wifi_deinit();
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                       */
/* ------------------------------------------------------------------ */

static esp_err_t send_asset(httpd_req_t *r, const char *ctype,
                            const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(r, ctype);
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    return httpd_resp_send(r, (const char *)start, end - start);
}

static esp_err_t h_index(httpd_req_t *r)
{ return send_asset(r, "text/html", index_html_start, index_html_end); }
static esp_err_t h_appjs(httpd_req_t *r)
{ return send_asset(r, "application/javascript", app_js_start, app_js_end); }
static esp_err_t h_css(httpd_req_t *r)
{ return send_asset(r, "text/css", style_css_start, style_css_end); }

static esp_err_t send_json(httpd_req_t *r, const char *json)
{
    httpd_resp_set_type(r, "application/json");
    return httpd_resp_sendstr(r, json);
}

static esp_err_t h_state(httpd_req_t *r)
{
    char buf[768];
    S.bridge.get_state_json(buf, sizeof(buf));
    return send_json(r, buf);
}

static esp_err_t h_version(httpd_req_t *r)
{
    char buf[256];
    S.bridge.get_version_json(buf, sizeof(buf));
    return send_json(r, buf);
}

static esp_err_t h_mapping_get(httpd_req_t *r)
{
    char buf[768];
    S.bridge.get_mapping_json(buf, sizeof(buf));
    return send_json(r, buf);
}

static esp_err_t read_body(httpd_req_t *r, char *buf, size_t cap, size_t *out_len)
{
    int total = r->content_len;
    if (total < 0 || (size_t)total >= cap) return ESP_ERR_INVALID_SIZE;
    int off = 0;
    while (off < total) {
        int n = httpd_req_recv(r, buf + off, total - off);
        if (n <= 0) return ESP_FAIL;
        off += n;
    }
    buf[off] = '\0';
    *out_len = off;
    return ESP_OK;
}

static esp_err_t h_mapping_put(httpd_req_t *r)
{
    char buf[768];
    size_t len = 0;
    if (read_body(r, buf, sizeof(buf), &len) != ESP_OK) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad body");
        return ESP_FAIL;
    }
    esp_err_t err = S.bridge.put_mapping_json(buf, len);
    if (err != ESP_OK) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "invalid mapping");
        return ESP_FAIL;
    }
    return httpd_resp_sendstr(r, "{\"ok\":true}");
}

static esp_err_t h_pairing(httpd_req_t *r)
{
    bool start = strstr(r->uri, "/start") != NULL;
    S.bridge.set_pairing(start);
    return httpd_resp_sendstr(r, "{\"ok\":true}");
}

static esp_err_t h_mode_play(httpd_req_t *r)
{
    httpd_resp_sendstr(r, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(200));
    S.bridge.request_mode(false);
    return ESP_OK;
}

static esp_err_t h_joycon_forget(httpd_req_t *r)
{
    int side = strstr(r->uri, "/R/") != NULL ? 1 : 0;
    S.bridge.joycon_forget(side);
    return httpd_resp_sendstr(r, "{\"ok\":true}");
}

static esp_err_t h_host_forget(httpd_req_t *r)
{
    S.bridge.host_forget();
    return httpd_resp_sendstr(r, "{\"ok\":true}");
}

static esp_err_t h_reboot(httpd_req_t *r)
{
    httpd_resp_sendstr(r, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(200));
    S.bridge.reboot();
    return ESP_OK;
}

static esp_err_t h_factory_reset(httpd_req_t *r)
{
    httpd_resp_sendstr(r, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(200));
    S.bridge.factory_reset();
    return ESP_OK;
}

/* --- WebSocket (status + live merged report, FR-21) --------------- */

static esp_err_t h_ws(httpd_req_t *r)
{
    if (r->method == HTTP_GET) {
        int fd = httpd_req_to_sockfd(r);
        for (size_t i = 0; i < sizeof(S.ws_fds) / sizeof(S.ws_fds[0]); i++) {
            if (S.ws_fds[i] == 0) { S.ws_fds[i] = fd; break; }
        }
        ESP_LOGI(TAG, "ws client %d connected", fd);
        return ESP_OK;
    }
    /* We don't expect inbound frames; drain and ignore. */
    httpd_ws_frame_t f = {0};
    httpd_ws_recv_frame(r, &f, 0);
    return ESP_OK;
}

void web_server_broadcast(void)
{
    if (!S.running || !S.http) return;
    char buf[768];
    S.bridge.get_live_json(buf, sizeof(buf));

    httpd_ws_frame_t f = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)buf,
        .len = strlen(buf),
    };
    for (size_t i = 0; i < sizeof(S.ws_fds) / sizeof(S.ws_fds[0]); i++) {
        int fd = S.ws_fds[i];
        if (fd == 0) continue;
        if (httpd_ws_send_frame_async(S.http, fd, &f) != ESP_OK) {
            S.ws_fds[i] = 0;   /* client gone */
        }
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void reg(httpd_handle_t s, const char *uri, httpd_method_t m,
                esp_err_t (*h)(httpd_req_t *))
{
    httpd_uri_t u = { .uri = uri, .method = m, .handler = h };
    httpd_register_uri_handler(s, &u);
}

esp_err_t web_server_start(const web_bridge_t *bridge, const web_wifi_cfg_t *wifi)
{
    if (S.running) return ESP_ERR_INVALID_STATE;
    if (!bridge || !wifi) return ESP_ERR_INVALID_ARG;
    S.bridge = *bridge;
    S.wifi = *wifi;
    memset(S.ws_fds, 0, sizeof(S.ws_fds));

    wifi_up();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 20;
    cfg.lru_purge_enable = true;
    if (httpd_start(&S.http, &cfg) != ESP_OK) {
        wifi_down();
        return ESP_FAIL;
    }

    reg(S.http, "/",            HTTP_GET, h_index);
    reg(S.http, "/app.js",      HTTP_GET, h_appjs);
    reg(S.http, "/style.css",   HTTP_GET, h_css);
    reg(S.http, "/api/state",   HTTP_GET, h_state);
    reg(S.http, "/api/version", HTTP_GET, h_version);
    reg(S.http, "/api/mapping", HTTP_GET, h_mapping_get);
    reg(S.http, "/api/mapping", HTTP_PUT, h_mapping_put);
    reg(S.http, "/api/pairing/*",      HTTP_POST, h_pairing);
    reg(S.http, "/api/mode/play",     HTTP_POST, h_mode_play);
    reg(S.http, "/api/joycon/*/forget", HTTP_POST, h_joycon_forget);
    reg(S.http, "/api/host/forget",    HTTP_POST, h_host_forget);
    reg(S.http, "/api/reboot",         HTTP_POST, h_reboot);
    reg(S.http, "/api/factory-reset",  HTTP_POST, h_factory_reset);

    httpd_uri_t ws = { .uri = "/ws", .method = HTTP_GET, .handler = h_ws,
                       .is_websocket = true };
    httpd_register_uri_handler(S.http, &ws);

    S.running = true;
    ESP_LOGI(TAG, "config portal started");
    return ESP_OK;
}

void web_server_stop(void)
{
    if (!S.running) return;
    httpd_stop(S.http);
    S.http = NULL;
    wifi_down();
    S.running = false;
    ESP_LOGI(TAG, "config portal stopped");
}
