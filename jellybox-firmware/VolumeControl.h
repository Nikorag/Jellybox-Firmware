#pragma once

// ── KY-040 rotary encoder volume dial ─────────────────────────────────────
// Polls the quadrature encoder in the main loop. Each detent steps the
// volume by 1 unit in the 0..VOLUME_MAX range. Level is persisted to NVS
// with a 500 ms idle debounce so we don't thrash flash on a long spin.
//
// The state machine is the classic 4-state Gray-code lookup that filters
// bounce: only +1 or -1 transitions in the table are real detents; all
// other AB pairings are noise and ignored.
//
// SW (press) is read on PIN_ENC_SW but not acted on in v1 — the pin is
// reserved for a future mute toggle.

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

class VolumeControl {
public:
  void begin() {
    pinMode(PIN_ENC_CLK, INPUT);
    pinMode(PIN_ENC_DT,  INPUT);
    pinMode(PIN_ENC_SW,  INPUT);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    _level = prefs.getUChar(NVS_KEY_VOLUME, VOLUME_DEFAULT);
    prefs.end();
    if (_level > VOLUME_MAX) _level = VOLUME_DEFAULT;

    _lastAB  = _readAB();
    _persisted = _level;
    _dirtyAt = 0;
  }

  // Call every loop iteration. Non-blocking, typically < 50 µs.
  void update() {
    uint8_t ab = _readAB();
    if (ab != _lastAB) {
      // Quadrature transition table indexed by (prev<<2 | curr).
      //   0  =  no change / illegal
      //  +1  =  CW detent
      //  -1  =  CCW detent
      static const int8_t kTable[16] = {
        0, -1, +1,  0,
        +1,  0,  0, -1,
        -1,  0,  0, +1,
        0, +1, -1,  0,
      };
      int8_t delta = kTable[(_lastAB << 2) | ab];
      _lastAB = ab;
      if (delta != 0) {
        // KY-040 emits 4 transitions per physical detent; only count
        // when AB returns to the rest position (0b11) for a clean step.
        if (ab == 0b11) {
          if (delta > 0 && _level < VOLUME_MAX) _level++;
          if (delta < 0 && _level > 0)          _level--;
          _dirtyAt = millis();
        }
      }
    }

    // Debounced persist: 500 ms after the last change, write to NVS.
    if (_dirtyAt && (millis() - _dirtyAt) > 500 && _level != _persisted) {
      Preferences prefs;
      prefs.begin(NVS_NAMESPACE, false);
      prefs.putUChar(NVS_KEY_VOLUME, _level);
      prefs.end();
      _persisted = _level;
      _dirtyAt = 0;
    }
  }

  uint8_t level() const { return _level; }

  // 0.0 – 1.0 amplitude scale, ready to hand to AudioFeedback.
  float gain() const {
    return (float)_level / (float)VOLUME_MAX;
  }

private:
  uint8_t _level    = VOLUME_DEFAULT;
  uint8_t _persisted = VOLUME_DEFAULT;
  uint8_t _lastAB    = 0b11;
  uint32_t _dirtyAt   = 0;

  // Returns the 2-bit AB state. Pull-ups are external (on the breakout),
  // so the resting state is 0b11.
  uint8_t _readAB() const {
    uint8_t a = digitalRead(PIN_ENC_CLK) ? 1 : 0;
    uint8_t b = digitalRead(PIN_ENC_DT)  ? 1 : 0;
    return (uint8_t)((a << 1) | b);
  }
};
