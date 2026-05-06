// main.c — wires GPIO inputs to audio + LED feedback.
// Polling task at 10 ms cadence (chosen over ISR: simpler, plenty fast for
// human input). Tone plays first, THEN LED double-blinks — sequential, since
// audio_play_* is blocking on this task. Total worst-case latency to next
// poll on a button event is ~150 ms (tone) + ~400 ms (blink) ≈ 550 ms,
// during which further button/reed edges are simply missed. That's fine for
// the spec: drop-on-busy at the audio layer plus a blocked poller mean
// events never stack.
#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio.h"
#include "pins.h"

static const char *TAG = "main";

#define POLL_PERIOD_MS   10
#define DEBOUNCE_MS      30
#define BLINK_ON_MS      100
#define BLINK_OFF_MS     100

// LED is wired active HIGH: GPIO drives the anode (via current-limit resistor),
// cathode to GND. Driving the pin HIGH lights the LED; LOW turns it off.
#define LED_ON  1
#define LED_OFF 0

// Blocks the input task for ~400 ms. Acceptable per the comment at top.
static void led_double_blink(void) {
    for (int i = 0; i < 2; ++i) {
        gpio_set_level(PIN_LED, LED_ON);
        vTaskDelay(pdMS_TO_TICKS(BLINK_ON_MS));
        gpio_set_level(PIN_LED, LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(BLINK_OFF_MS));
    }
}

static void input_task(void *arg) {
    (void)arg;

    // Both lines idle HIGH (pull-up). Reed: closed=LOW, open=HIGH.
    int prev_button = gpio_get_level(PIN_BUTTON);
    int prev_reed   = gpio_get_level(PIN_REED);

    TickType_t last_button_edge = 0;
    TickType_t last_reed_edge   = 0;

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        int button = gpio_get_level(PIN_BUTTON);
        int reed   = gpio_get_level(PIN_REED);

        // Diagnostic: log every level change pre-debounce so we can see
        // whether the GPIO is moving at all. Remove once everything works.
        if (button != prev_button) {
            ESP_LOGI(TAG, "button level %d -> %d", prev_button, button);
        }
        if (reed != prev_reed) {
            ESP_LOGI(TAG, "reed level %d -> %d", prev_reed, reed);
        }

        // Button: falling edge = press (pull-up -> GND).
        if (prev_button == 1 && button == 0 &&
            (now - last_button_edge) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
            last_button_edge = now;
            ESP_LOGI(TAG, "button pressed");
            audio_play_button_tone();
            led_double_blink();
        }

        // Reed: rising edge = magnet removed (closed->open).
        if (prev_reed == 0 && reed == 1 &&
            (now - last_reed_edge) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
            last_reed_edge = now;
            ESP_LOGI(TAG, "reed opened");
            audio_play_reed_tone();
        }

        prev_button = button;
        prev_reed   = reed;
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

void app_main(void) {
    gpio_config_t out_cfg = {
        .pin_bit_mask = 1ULL << PIN_LED,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_cfg));
    gpio_set_level(PIN_LED, LED_OFF);

    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << PIN_BUTTON) | (1ULL << PIN_REED),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_cfg));

    audio_init();

    ESP_LOGI(TAG, "pins: LED=%d BUTTON=%d REED=%d (GPIO numbers)",
             PIN_LED, PIN_BUTTON, PIN_REED);
    ESP_LOGI(TAG, "initial levels: button=%d reed=%d (1 = idle/pulled-up, 0 = pulled to GND)",
             gpio_get_level(PIN_BUTTON), gpio_get_level(PIN_REED));

    // Boot self-test: 3 quick LED blinks then a short tone. Lets us see whether
    // GPIO6 is toggling and whether the I2S/amp path produces any sound,
    // independent of the button/reed inputs. Remove later once everything works.
    ESP_LOGI(TAG, "self-test: 3 LED blinks then tone");
    for (int i = 0; i < 3; ++i) {
        gpio_set_level(PIN_LED, LED_ON);
        vTaskDelay(pdMS_TO_TICKS(120));
        gpio_set_level(PIN_LED, LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    audio_play_button_tone();
    ESP_LOGI(TAG, "self-test done");

    // 4 KB stack is enough; sinf() and i2s_channel_write don't recurse deeply.
    xTaskCreate(input_task, "input", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "fanda up");
}
