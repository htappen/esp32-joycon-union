/* status_led.c — see status_led.h */

#include "status_led.h"

#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "led";
static led_strip_handle_t s_strip;
static volatile led_pattern_t s_pat = LED_BOOT;

typedef struct { uint8_t r, g, b; uint16_t period_ms; uint8_t mode; } style_t;
/* mode: 0 solid, 1 pulse (sine), 2 blink (square) */
static const style_t STYLES[] = {
    [LED_BOOT]            = {60, 60, 60,    0, 0},
    [LED_PLAY_SEARCHING]  = { 0,  0, 90, 1800, 1},
    [LED_PLAY_CONNECTED]  = { 0, 80,  0,    0, 0},
    [LED_PLAY_PARTIAL]    = { 0, 80,  0, 1400, 1},
    [LED_DEGRADED]        = {90, 45,  0,  400, 2},
    [LED_CONFIG]          = { 0, 70, 70, 2600, 1},
    [LED_PAIRING]         = {90,  0, 90,  220, 2},
    [LED_ERROR]           = {90,  0,  0,  160, 2},
};

static void render(uint32_t t_ms)
{
    style_t s = STYLES[s_pat];
    float k = 1.0f;
    if (s.mode == 1) {
        k = 0.15f + 0.85f * (0.5f + 0.5f * sinf(2.0f * (float)M_PI *
              (float)(t_ms % s.period_ms) / (float)s.period_ms));
    } else if (s.mode == 2) {
        k = ((t_ms % s.period_ms) < s.period_ms / 2) ? 1.0f : 0.05f;
    }
    led_strip_set_pixel(s_strip, 0, (uint8_t)(s.r * k), (uint8_t)(s.g * k),
                        (uint8_t)(s.b * k));
    led_strip_refresh(s_strip);
}

static void led_task(void *arg)
{
    (void)arg;
    uint32_t t = 0;
    for (;;) {
        render(t);
        t += 40;
        vTaskDelay(pdMS_TO_TICKS(40));
    }
}

void status_led_init(void)
{
    led_strip_config_t sc = {
        .strip_gpio_num = CONFIG_JCB_STATUS_LED_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rc = { .resolution_hz = 10 * 1000 * 1000 };
    if (led_strip_new_rmt_device(&sc, &rc, &s_strip) != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 init failed on GPIO %d", CONFIG_JCB_STATUS_LED_GPIO);
        return;
    }
    xTaskCreatePinnedToCore(led_task, "led", 2560, NULL, 2, NULL, 1);
}

void status_led_set(led_pattern_t p) { s_pat = p; }
