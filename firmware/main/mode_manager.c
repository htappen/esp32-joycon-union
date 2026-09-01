/* mode_manager.c — see mode_manager.h */

#include "mode_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mode";
static mode_manager_cb_t s_cb;
static volatile jcb_mode_t s_mode = MODE_PLAY;
static volatile jcb_mode_t s_request = MODE_PLAY;
static volatile bool s_have_request = false;

/* BOOT is active-low with an external pull-up. */
static void apply(jcb_mode_t m)
{
    if (m == s_mode) return;
    s_mode = m;
    if (m == MODE_CONFIG) {
        ESP_LOGI(TAG, "-> Config Mode");
        if (s_cb.enter_config) s_cb.enter_config();
    } else {
        ESP_LOGI(TAG, "-> Play Mode");
        if (s_cb.enter_play) s_cb.enter_play();
    }
}

static void mode_task(void *arg)
{
    (void)arg;

    for (;;) {
        if (s_have_request) {
            s_have_request = false;
            apply(s_request);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void mode_manager_init(const mode_manager_cb_t *cb)
{
    if (cb) s_cb = *cb;

    /* Config-mode entry starts Wi-Fi and the HTTP server synchronously. */
    xTaskCreatePinnedToCore(mode_task, "mode", 6144, NULL, 4, NULL, 1);
}

jcb_mode_t mode_manager_current(void) { return s_mode; }

void mode_manager_request(jcb_mode_t mode)
{
    s_request = mode;
    s_have_request = true;
}
