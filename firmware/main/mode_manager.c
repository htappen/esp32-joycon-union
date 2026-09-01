/* mode_manager.c — see mode_manager.h */

#include "mode_manager.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "mode";
#define BTN_GPIO   CONFIG_JCB_MODE_BUTTON_GPIO
#define LONG_MS    CONFIG_JCB_MODE_BUTTON_LONG_MS
#define CHORD_MS   3000

static mode_manager_cb_t s_cb;
static volatile jcb_mode_t s_mode = MODE_PLAY;
static volatile jcb_mode_t s_request = MODE_PLAY;
static volatile bool s_have_request = false;

/* BOOT is active-low with an external pull-up. */
static inline bool btn_down(void) { return gpio_get_level(BTN_GPIO) == 0; }

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

    /* Boot chord: held from reset -> factory reset. */
    if (btn_down()) {
        int64_t t0 = esp_timer_get_time();
        while (btn_down()) {
            if (esp_timer_get_time() - t0 > CHORD_MS * 1000) {
                ESP_LOGW(TAG, "boot chord: factory reset");
                if (s_cb.factory_reset) s_cb.factory_reset();
                vTaskDelay(portMAX_DELAY);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    bool prev = false;
    int64_t press_t = 0;

    for (;;) {
        if (s_have_request) {
            s_have_request = false;
            apply(s_request);
        }

        bool now = btn_down();
        if (now && !prev) {
            press_t = esp_timer_get_time();
        } else if (!now && prev) {
            int64_t held = esp_timer_get_time() - press_t;
            if (held >= LONG_MS * 1000) {
                apply(s_mode == MODE_PLAY ? MODE_CONFIG : MODE_PLAY);
            }
        }
        prev = now;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void mode_manager_init(const mode_manager_cb_t *cb)
{
    if (cb) s_cb = *cb;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);

    /* Config-mode entry starts Wi-Fi and the HTTP server synchronously. */
    xTaskCreatePinnedToCore(mode_task, "mode", 6144, NULL, 4, NULL, 1);
}

jcb_mode_t mode_manager_current(void) { return s_mode; }

void mode_manager_request(jcb_mode_t mode)
{
    s_request = mode;
    s_have_request = true;
}
