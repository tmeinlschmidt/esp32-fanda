// audio.c — sine tone synth + embedded-WAV playback over the new I2S std
// driver. Tones: phase-accumulated sinf() streamed in CHUNK_FRAMES blocks.
// WAV: low_sonic.wav (16-bit mono PCM, 44.1 kHz) is linked into the binary
// via EMBED_FILES; we parse the RIFF/WAVE chunks at playback time and stream
// the data chunk to I2S, duplicating each mono sample to both stereo slots.
#include "audio.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "pins.h"

static const char *TAG = "audio";

// Sample rate matches the embedded WAV (44.1 kHz). The synth tone math is
// rate-independent. Amplitude leaves ~6 dB headroom — MAX98357A + a cheap
// speaker can clip/buzz nastily near full scale.
#define SAMPLE_RATE_HZ      44100
#define CHUNK_FRAMES        256
#define DMA_DESC_NUM        2
#define DMA_FRAME_NUM       256
#define AMPLITUDE           0.5f

#define REED_FREQ_HZ        500.0f
#define REED_DURATION_MS    400

// Symbols injected by EMBED_FILES "../low_sonic.wav" in main/CMakeLists.txt.
extern const uint8_t _binary_low_sonic_wav_start[];
extern const uint8_t _binary_low_sonic_wav_end[];

static i2s_chan_handle_t s_tx_chan;
static SemaphoreHandle_t s_busy_mutex;  // try-take guarantees drop-on-busy.

void audio_init(void) {
    s_busy_mutex = xSemaphoreCreateMutex();
    configASSERT(s_busy_mutex);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = DMA_DESC_NUM;
    chan_cfg.dma_frame_num = DMA_FRAME_NUM;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        // 16-bit data, stereo slot. MAX98357A is mono and picks L/R via SD pin;
        // we duplicate the sample to both slots so either channel works.
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_LRC,
            .dout = PIN_I2S_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {0},
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));
    ESP_LOGI(TAG, "i2s ready: %d Hz, 16-bit stereo, BCLK=%d LRC=%d DIN=%d",
             SAMPLE_RATE_HZ, PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
}

// Synthesise `duration_ms` of a sine at `freq_hz` and stream it out.
static void play_tone(float freq_hz, uint32_t duration_ms) {
    // Drop-on-busy: try-take with zero timeout. If another tone is in flight,
    // skip this one entirely rather than queueing.
    if (xSemaphoreTake(s_busy_mutex, 0) != pdTRUE) {
        ESP_LOGI(TAG, "busy, dropping tone %.0f Hz", freq_hz);
        return;
    }

    const uint32_t total_frames = (SAMPLE_RATE_HZ * duration_ms) / 1000;
    const float phase_step = 2.0f * (float)M_PI * freq_hz / (float)SAMPLE_RATE_HZ;
    float phase = 0.0f;

    int16_t buf[CHUNK_FRAMES * 2];  // stereo interleaved
    uint32_t frames_left = total_frames;

    while (frames_left > 0) {
        uint32_t n = frames_left > CHUNK_FRAMES ? CHUNK_FRAMES : frames_left;
        for (uint32_t i = 0; i < n; ++i) {
            int16_t s = (int16_t)(sinf(phase) * AMPLITUDE * 32767.0f);
            buf[2 * i + 0] = s;
            buf[2 * i + 1] = s;
            phase += phase_step;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        size_t written = 0;
        // Blocks until DMA accepts the chunk; portMAX_DELAY is fine here.
        esp_err_t err = i2s_channel_write(s_tx_chan, buf, n * 2 * sizeof(int16_t),
                                          &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_write: %s", esp_err_to_name(err));
            break;
        }
        frames_left -= n;
    }

    xSemaphoreGive(s_busy_mutex);
}

// Locate the "data" chunk inside a RIFF/WAVE PCM blob. The file isn't
// guaranteed to put `data` immediately after `fmt ` — low_sonic.wav has a
// LIST/INFO chunk in between — so we walk the chunk list until we find it.
static const uint8_t *wav_find_data(const uint8_t *buf, size_t buf_len,
                                    size_t *out_len) {
    if (buf_len < 12) return NULL;
    if (memcmp(buf, "RIFF", 4) != 0) return NULL;
    if (memcmp(buf + 8, "WAVE", 4) != 0) return NULL;

    size_t pos = 12;
    while (pos + 8 <= buf_len) {
        const uint8_t *id = buf + pos;
        uint32_t sz = (uint32_t)buf[pos + 4]
                    | ((uint32_t)buf[pos + 5] << 8)
                    | ((uint32_t)buf[pos + 6] << 16)
                    | ((uint32_t)buf[pos + 7] << 24);
        pos += 8;
        if (memcmp(id, "data", 4) == 0) {
            if (pos + sz > buf_len) sz = (uint32_t)(buf_len - pos);
            *out_len = sz;
            return buf + pos;
        }
        pos += sz;
        if (sz & 1u) pos++;  // RIFF chunks are word-aligned.
    }
    return NULL;
}

// Stream 16-bit little-endian mono PCM to I2S, duplicating each sample to
// both stereo slots. Caller must ensure the PCM rate matches SAMPLE_RATE_HZ.
static void play_pcm16_mono(const uint8_t *bytes, size_t byte_len) {
    if (xSemaphoreTake(s_busy_mutex, 0) != pdTRUE) {
        ESP_LOGI(TAG, "busy, dropping wav");
        return;
    }

    int16_t buf[CHUNK_FRAMES * 2];
    const size_t total_frames = byte_len / 2;
    size_t pos = 0;

    while (pos < total_frames) {
        size_t n = total_frames - pos;
        if (n > CHUNK_FRAMES) n = CHUNK_FRAMES;
        for (size_t i = 0; i < n; ++i) {
            // Byte-wise read avoids unaligned int16 access on RISC-V.
            const uint8_t *p = bytes + (pos + i) * 2;
            int16_t s = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            buf[2 * i + 0] = s;
            buf[2 * i + 1] = s;
        }
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx_chan, buf,
                                          n * 2 * sizeof(int16_t),
                                          &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_write: %s", esp_err_to_name(err));
            break;
        }
        pos += n;
    }

    xSemaphoreGive(s_busy_mutex);
}

void audio_play_button_tone(void) {
    ESP_LOGI(TAG, "button wav");
    size_t data_len = 0;
    const uint8_t *pcm = wav_find_data(
        _binary_low_sonic_wav_start,
        (size_t)(_binary_low_sonic_wav_end - _binary_low_sonic_wav_start),
        &data_len);
    if (!pcm) {
        ESP_LOGE(TAG, "low_sonic.wav: data chunk not found");
        return;
    }
    play_pcm16_mono(pcm, data_len);
}

void audio_play_reed_tone(void) {
    ESP_LOGI(TAG, "reed tone");
    play_tone(REED_FREQ_HZ, REED_DURATION_MS);
}
