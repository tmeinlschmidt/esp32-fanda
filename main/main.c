// main.c — wires GPIO inputs to audio + LED feedback.
// Polling task at 10 ms cadence (chosen over ISR: simpler, plenty fast for
// human input). On a button press the input task spawns a background blinker
// task and then synchronously plays the WAV (blocks ~3.2 s); the blinker
// flashes the LED for the whole duration of playback and exits when stopped.
// Further button/reed edges during those ~3.2 s are missed — drop-on-busy at
// the audio layer plus a blocked poller mean events never stack.
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

// Blinker task: toggles the LED at ~5 Hz while s_blinker_run is true. The
// button handler starts it before WAV playback and stops it after, so the
// LED flashes for the whole duration of the sound (~3.2 s).
static volatile bool s_blinker_run    = false;
static TaskHandle_t  s_blinker_handle = NULL;

static void blinker_task(void *arg) {
    (void)arg;
    while (s_blinker_run) {
        gpio_set_level(PIN_LED, LED_ON);
        vTaskDelay(pdMS_TO_TICKS(BLINK_ON_MS));
        gpio_set_level(PIN_LED, LED_OFF);
        vTaskDelay(pdMS_TO_TICKS(BLINK_OFF_MS));
    }
    gpio_set_level(PIN_LED, LED_OFF);
    s_blinker_handle = NULL;
    vTaskDelete(NULL);
}

static void start_blinking(void) {
    if (s_blinker_handle) return;
    s_blinker_run = true;
    xTaskCreate(blinker_task, "blink", 2048, NULL, 4, &s_blinker_handle);
}

static void stop_blinking(void) {
    s_blinker_run = false;
    // Task observes the flag at its next vTaskDelay boundary (≤ ~200 ms),
    // exits the loop, and leaves the LED in the OFF state.
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
            start_blinking();
            audio_play_button_tone();
            stop_blinking();
        }

        // Reed: rising edge = magnet removed (closed->open). Detected and
        // logged for visibility, but no action is performed.
        if (prev_reed == 0 && reed == 1 &&
            (now - last_reed_edge) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
            last_reed_edge = now;
            ESP_LOGI(TAG, "reed opened (no action)");
        }

        prev_button = button;
        prev_reed   = reed;
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
    }
}

void app_main(void) {
    // LED output. Mirror Arduino's pinMode(OUTPUT): reset_pin first (detaches
    // any latched peripheral / sleep-gpio config), then INPUT_OUTPUT so we can
    // read the pin back as a diagnostic.
    gpio_reset_pin(PIN_LED);
    ESP_ERROR_CHECK(gpio_set_direction(PIN_LED, GPIO_MODE_INPUT_OUTPUT));
    gpio_set_level(PIN_LED, LED_OFF);
    ESP_LOGI(TAG, "LED after gpio_set_level(LED_OFF=%d): readback=%d",
             LED_OFF, gpio_get_level(PIN_LED));
    gpio_set_level(PIN_LED, LED_ON);
    ESP_LOGI(TAG, "LED after gpio_set_level(LED_ON=%d): readback=%d",
             LED_ON, gpio_get_level(PIN_LED));
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(PIN_LED, LED_OFF);
    ESP_LOGI(TAG, "LED after gpio_set_level(LED_OFF=%d): readback=%d",
             LED_OFF, gpio_get_level(PIN_LED));

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
