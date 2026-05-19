#pragma once

// ── Audio feedback via MAX98357A I2S amplifier ────────────────────────────
// Plays exactly three short cues, generated procedurally at 16 kHz mono:
//
//   playSuccess(gain)   — rising two-tone, NFC scan resolved to a play
//   playImported(gain)  — cheerful triad, NFC scan registered a new tag
//   playError(gain)     — descending two-tone, NFC scan rejected
//
// `gain` is a 0.0–1.0 amplitude scale supplied by the caller (typically
// VolumeControl::gain()). The module deliberately knows nothing about
// the volume source so it can be tested or driven from a fixed level.
//
// Each call is synchronous and blocks for ≤ 240 ms. That's intentional:
// every trigger point is already a discrete user-feedback moment that
// overlaps with the LED flash and TFT redraw. The cooperative scheduler
// in jellybox-firmware.ino can absorb it.

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include "Config.h"

class AudioFeedback {
public:
  void begin() {
    i2s_config_t cfg = {};
    cfg.mode               = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate        = I2S_SAMPLE_RATE;
    cfg.bits_per_sample    = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format     = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags   = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count      = 4;
    cfg.dma_buf_len        = 256;
    cfg.use_apll           = false;
    cfg.tx_desc_auto_clear = true;

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = PIN_I2S_BCLK;
    pins.ws_io_num    = PIN_I2S_LRC;
    pins.data_out_num = PIN_I2S_DIN;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    i2s_set_pin(I2S_NUM_0, &pins);
    i2s_zero_dma_buffer(I2S_NUM_0);
  }

  // Rising two-tone: 880 Hz × 80 ms → 1320 Hz × 120 ms
  void playSuccess(float gain) {
    _tone(880,  80,  gain);
    _tone(1320, 120, gain);
  }

  // Cheerful triad: 660 → 880 → 1100 Hz, 80 ms each
  void playImported(float gain) {
    _tone(660,  80, gain);
    _tone(880,  80, gain);
    _tone(1100, 80, gain);
  }

  // Descending two-tone: 440 Hz × 100 ms → 220 Hz × 200 ms
  void playError(float gain) {
    _tone(440, 100, gain);
    _tone(220, 200, gain);
  }

private:
  // Synthesises a single sine tone and streams it through I2S. Linear
  // amplitude envelope (5 ms attack, 5 ms release) prevents clicks.
  void _tone(float frequencyHz, uint32_t durationMs, float gain) {
    if (gain <= 0.0f) {
      delay(durationMs);
      return;
    }
    if (gain > 1.0f) gain = 1.0f;

    const uint32_t totalSamples   = (I2S_SAMPLE_RATE * durationMs) / 1000;
    const uint32_t attackSamples  = I2S_SAMPLE_RATE * 5 / 1000;
    const uint32_t releaseSamples = attackSamples;
    const float phaseInc = 2.0f * (float)M_PI * frequencyHz / (float)I2S_SAMPLE_RATE;

    constexpr size_t CHUNK = 128;
    int16_t buf[CHUNK];
    float phase = 0.0f;

    for (uint32_t i = 0; i < totalSamples; i += CHUNK) {
      size_t count = (totalSamples - i) < CHUNK ? (totalSamples - i) : CHUNK;
      for (size_t k = 0; k < count; ++k) {
        uint32_t s = i + k;
        float envelope = 1.0f;
        if (s < attackSamples) {
          envelope = (float)s / (float)attackSamples;
        } else if (s > totalSamples - releaseSamples) {
          envelope = (float)(totalSamples - s) / (float)releaseSamples;
        }
        float sample = sinf(phase) * envelope * gain;
        phase += phaseInc;
        if (phase > 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        // Headroom: peak at ~0x6900 (≈ -1.5 dBFS) to avoid clipping.
        int32_t pcm = (int32_t)(sample * 27000.0f);
        if (pcm > 32767)  pcm = 32767;
        if (pcm < -32768) pcm = -32768;
        buf[k] = (int16_t)pcm;
      }
      size_t written = 0;
      i2s_write(I2S_NUM_0, buf, count * sizeof(int16_t), &written, portMAX_DELAY);
    }
  }
};
