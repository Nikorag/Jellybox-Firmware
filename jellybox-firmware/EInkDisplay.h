#pragma once

// ── eInk panel selection ──────────────────────────────────────────────────
// Default: Waveshare 2.9" V2 B/W (296×128).
// Change the include and the class alias below to match your panel.
// Other common options:
//   <epd/GxEPD2_154_D67.h>  — Waveshare 1.54" V2 (200×200)
//   <epd/GxEPD2_213_BN.h>   — Waveshare 2.13" V3 (250×122)
//   <epd/GxEPD2_270_T91.h>  — Waveshare 2.7"     (264×176)

#include <GxEPD2_BW.h>
#include <epd/GxEPD2_290_BS.h>       // Waveshare 2.9" V2
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include "Config.h"
#include "JellyboxLogo.h"

// Landscape orientation: 296 px wide, 128 px tall
#define EINK_W 296
#define EINK_H 128

// Some panels have a hardware dead zone where the glass extends beyond the
// active pixel area. Adjust EINK_MARGIN_R (pixels) if centred text appears
// shifted right. 35px ≈ 8 mm on the Waveshare 2.9" V2.
#define EINK_MARGIN_L  0
#define EINK_MARGIN_R  35

using Panel = GxEPD2_BW<GxEPD2_290_BS, GxEPD2_290_BS::HEIGHT>;

class EInkDisplay {
public:
  EInkDisplay()
    : _display(GxEPD2_290_BS(PIN_EINK_CS, PIN_EINK_DC, PIN_EINK_RST, PIN_EINK_BUSY))
  {}

  void begin() {
    _display.init(115200, true, 2, false);
    _display.setRotation(1);         // landscape: width=296, height=128
    _display.setTextWrap(false);
    _display.setTextColor(GxEPD_BLACK);
    _clear();
  }

  // Startup splash — logo centred with "JELLYBOX" title below.
  // Shown once at boot before the connecting/config flow begins.
  void showSplash() {
    _draw([&]() {
      _clear();
      int16_t logoX = EINK_MARGIN_L + (EINK_W - EINK_MARGIN_L - EINK_MARGIN_R - JELLYBOX_LOGO_W) / 2;
      _display.drawBitmap(logoX, 14, JELLYBOX_LOGO, JELLYBOX_LOGO_W, JELLYBOX_LOGO_H, GxEPD_BLACK);
      _centre("JELLYBOX", &FreeMonoBold9pt7b, 110);
    });
  }

  void showUnpaired() {
    _draw([&]() {
      _clear();
      _title("JELLYBOX");
      _divider(36);
      _centre("Unpaired - connect to:", nullptr, 62);
      _centre(WIFI_AP_NAME, &FreeMonoBold9pt7b, 90);
      _centre("to configure", nullptr, 112);
    });
  }

  // Pass ssid="" for a generic "Connecting..." screen, or the network name
  // to show "Connecting to <ssid>" (e.g. after WiFi credentials are known).
  void showConnecting(const String& ssid = "") {
    _draw([&]() {
      _clear();
      _title("JELLYBOX");
      _divider(36);
      if (ssid.length() > 0) {
        _centre("Connecting to:", nullptr, 62);
        _centre(_trunc(ssid, 22).c_str(), &FreeMonoBold9pt7b, 90);
      } else {
        _centre("Connecting...", &FreeMono9pt7b, 75);
      }
      _centre((String("fw ") + FIRMWARE_VERSION).c_str(), nullptr, 122);
    });
  }

  // eInk persists after power-off, so the idle screen deliberately avoids
  // any "ready"/"waiting" wording that would be misleading on a dead device.
  // Shows the centred logo with the device name beneath it — visually
  // indistinguishable between "powered and idle" and "powered off".
  void showReady(const String& name) {
    _draw([&]() {
      _clear();
      int16_t logoX = EINK_MARGIN_L + (EINK_W - EINK_MARGIN_L - EINK_MARGIN_R - JELLYBOX_LOGO_W) / 2;
      _display.drawBitmap(logoX, 14, JELLYBOX_LOGO, JELLYBOX_LOGO_W, JELLYBOX_LOGO_H, GxEPD_BLACK);
      _centre(_trunc(name, 22).c_str(), &FreeMonoBold9pt7b, 110);
    });
  }

  void showScanMode(const String& name) {
    _draw([&]() {
      _clear();
      _title("JELLYBOX");
      _divider(36);
      _centre(_trunc(name, 22).c_str(), &FreeMonoBold9pt7b, 60);
      _centre("[ SCAN MODE ]", &FreeMonoBold9pt7b, 82);
      _centre("Scan a tag to register", nullptr, 110);
    });
  }

  // GxEPD2_290_BS supports partial refresh (setPartialWindow + firstPage/nextPage).
  // Switching to partial refresh here would eliminate the full-panel flash on each
  // "Now Playing" update — worth doing if the flash becomes annoying in daily use.
  void showLastPlayed(const String& name, const String& title) {
    _draw([&]() {
      _clear();
      _title("JELLYBOX");
      _divider(36);
      _centre(_trunc(name, 22).c_str(), &FreeMono9pt7b, 60);
      _centre("Now playing:", nullptr, 80);
      _centre(_trunc(title, 28).c_str(), &FreeMonoBold9pt7b, 105);
    });
  }

  void showError(const String& msg) {
    _draw([&]() {
      _clear();
      _title("JELLYBOX");
      _divider(36);
      _centre("Error", &FreeMonoBold9pt7b, 68);
      _centre(_trunc(msg, 30).c_str(), nullptr, 96);
    });
  }

  // Initial OTA screen — shown once when the update starts.
  void showUpdating(const String& fromVersion, const String& toVersion) {
    _draw([&]() {
      _clear();
      _title("JELLYBOX");
      _divider(36);
      _centre("Updating firmware", &FreeMonoBold9pt7b, 60);
      String arrow = _trunc(fromVersion, 10) + " -> " + _trunc(toVersion, 10);
      _centre(arrow.c_str(), nullptr, 85);
      _centre("Do not unplug", nullptr, 115);
    });
  }

  // Progress bar update. eInk full refresh is ~1–2 s with a visible flash —
  // callers must throttle (e.g. every 10 % or every few seconds), not call
  // this on every byte received from httpUpdate.
  void showUpdateProgress(int percent) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    _draw([&]() {
      _clear();
      _title("JELLYBOX");
      _divider(36);
      _centre("Updating firmware", &FreeMonoBold9pt7b, 60);
      int16_t barX = EINK_MARGIN_L + 20;
      int16_t barW = EINK_W - EINK_MARGIN_L - EINK_MARGIN_R - 40;
      int16_t barY = 80;
      int16_t barH = 14;
      _display.drawRect(barX, barY, barW, barH, GxEPD_BLACK);
      int16_t fillW = (int16_t)((int32_t)(barW - 4) * percent / 100);
      _display.fillRect(barX + 2, barY + 2, fillW, barH - 4, GxEPD_BLACK);
      String pct = String(percent) + "%";
      _centre(pct.c_str(), nullptr, 115);
    });
  }

  void showUpdateFailed(const String& msg) {
    _draw([&]() {
      _clear();
      _title("JELLYBOX");
      _divider(36);
      _centre("Update failed", &FreeMonoBold9pt7b, 68);
      _centre(_trunc(msg, 30).c_str(), nullptr, 96);
    });
  }

private:
  Panel _display;

  // Full-window paged draw — lambda is called once per page.
  template <typename Fn>
  void _draw(Fn fn) {
    _display.setFullWindow();
    _display.firstPage();
    do { fn(); } while (_display.nextPage());
  }

  void _clear() {
    _display.fillScreen(GxEPD_WHITE);
  }

  // Bold centred title at y=26
  void _title(const char* text) {
    _centre(text, &FreeMonoBold9pt7b, 26);
  }

  // Horizontal rule
  void _divider(int16_t y) {
    _display.drawFastHLine(12, y, EINK_W - 24, GxEPD_BLACK);
  }

  // Centre text within a specific horizontal region [rx, rx+rw).
  void _centreIn(const char* text, const GFXfont* font, int16_t y, int16_t rx, int16_t rw) {
    _display.setFont(font);
    int16_t x1, y1;
    uint16_t w, h;
    _display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    int16_t x = rx + (rw - (int16_t)w) / 2 - x1;
    if (x < rx) x = rx;
    _display.setCursor(x, y);
    _display.print(text);
  }

  // Centre text horizontally. y is the text baseline.
  void _centre(const char* text, const GFXfont* font, int16_t y) {
    _display.setFont(font);
    int16_t x1, y1;
    uint16_t w, h;
    // Measure with cursor at (0, y). x1 is the left edge of the pixel bounding
    // box — it differs from 0 when the font has a left side bearing. Subtracting
    // x1 from the naive centred position corrects for that offset.
    _display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    int16_t usable = EINK_W - EINK_MARGIN_L - EINK_MARGIN_R;
    int16_t x = EINK_MARGIN_L + (usable - (int16_t)w) / 2 - x1;
    if (x < 4) x = 4;
    _display.setCursor(x, y);
    _display.print(text);
  }

  // Truncate with ellipsis if over maxLen characters
  String _trunc(const String& s, size_t maxLen) {
    if (s.length() <= maxLen) return s;
    return s.substring(0, maxLen - 3) + "...";
  }
};
