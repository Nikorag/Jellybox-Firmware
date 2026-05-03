#pragma once

#include <Adafruit_NeoPixel.h>
#include "Config.h"

enum class LEDState {
  OFF,
  IDLE,          // slow blue-white breathing — ready to scan
  CONNECTING,    // spinning cyan — WiFi / HTTP in progress
  BOOTSTRAPPING, // spinning yellow — contacting server
  SCAN_MODE,     // pulsing purple — waiting for NFC tag to register
  SUCCESS,       // flash green — play confirmed / tag captured
  ERROR,         // flash red — something went wrong
  UNPAIRED,      // slow amber breathing — no config / 401 from server
  CHARGING,      // fast white breathing — battery charging
  CHARGED,       // slow green breathing — battery full
  UPDATING,      // slow purple/blue pulse — OTA firmware update in progress
};

class LEDRing {
public:
  LEDRing()
    : _strip(NEOPIXEL_COUNT, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800) {}

  void begin() {
    _strip.begin();
    _strip.setBrightness(80);
    _strip.show();
  }

  // Transient flash states (SUCCESS / ERROR) snap back to baseState after 800 ms.
  // All other states are persistent.
  void setState(LEDState state) {
    bool isFlash = (state == LEDState::SUCCESS || state == LEDState::ERROR);
    if (isFlash) {
      _flashActive = true;
      _flashStart  = millis();
    } else {
      _flashActive  = false;
      _baseState    = state;
    }
    _currentState = state;
    if (!isFlash) {
      _phase = 0;  // reset animation phase on non-flash changes; flash states resume smoothly
    }
  }

  // Sets the state to return to after a flash completes.
  void setBaseState(LEDState state) {
    _baseState = state;
  }

  // Call as often as possible from loop().
  void update() {
    unsigned long now = millis();
    if (now - _lastUpdate < LED_UPDATE_INTERVAL_MS) return;
    _lastUpdate = now;
    _phase++;

    // Expire flash states
    if (_flashActive && (now - _flashStart) > 800) {
      _flashActive  = false;
      _currentState = _baseState;
    }

    switch (_currentState) {
      case LEDState::OFF:          _fill(0, 0, 0);              break;
      case LEDState::IDLE:         _breathe(40, 80, 220);       break;  // blue-white, 4 s
      case LEDState::CONNECTING:   _spin(0, 220, 220);          break;  // cyan
      case LEDState::BOOTSTRAPPING:_spin(220, 180, 0);          break;  // yellow
      case LEDState::SCAN_MODE:    _breathe(160, 0, 240);       break;  // purple, 4 s
      case LEDState::SUCCESS:      _fill(0, 200, 60);           break;  // green flash
      case LEDState::ERROR:        _fill(200, 0, 0);            break;  // red flash
      case LEDState::UNPAIRED:     _breathe(255, 110, 0);       break;  // amber, 4 s
      case LEDState::CHARGING:     _breathe(220, 220, 220, 120);break;  // white, fast (2.4 s)
      case LEDState::CHARGED:      _breathe(0, 200, 60,   300); break;  // green, slow (6 s)
      case LEDState::UPDATING:     _spin(120, 60, 240);         break;  // violet comet
    }

    _strip.show();
  }

private:
  Adafruit_NeoPixel _strip;
  LEDState  _currentState = LEDState::OFF;
  LEDState  _baseState    = LEDState::IDLE;
  bool      _flashActive  = false;
  unsigned long _flashStart  = 0;
  unsigned long _lastUpdate  = 0;
  uint32_t  _phase        = 0;

  void _fill(uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      _strip.setPixelColor(i, r, g, b);
    }
  }

  // Sinusoidal breathing. period = ticks per full cycle (1 tick ≈ 20 ms).
  // Default 200 ticks = ~4 s. Pass 120 for ~2.4 s (faster), 300 for ~6 s (slower).
  void _breathe(uint8_t r, uint8_t g, uint8_t b, uint32_t period = 200) {
    float t = (float)(_phase % period) / (float)period;
    float br = 0.05f + 0.95f * (0.5f - 0.5f * cosf(t * 2.0f * M_PI));
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      _strip.setPixelColor(i,
        (uint8_t)(r * br),
        (uint8_t)(g * br),
        (uint8_t)(b * br));
    }
  }

  // 4-pixel comet sweeping around the ring
  void _spin(uint8_t r, uint8_t g, uint8_t b) {
    _strip.clear();
    int head = (_phase / 2) % NEOPIXEL_COUNT;
    float tails[] = { 1.0f, 0.6f, 0.35f, 0.15f };
    for (int t = 0; t < 4; t++) {
      int idx = (head - t + NEOPIXEL_COUNT) % NEOPIXEL_COUNT;
      float d = tails[t];
      _strip.setPixelColor(idx,
        (uint8_t)(r * d),
        (uint8_t)(g * d),
        (uint8_t)(b * d));
    }
  }
};
