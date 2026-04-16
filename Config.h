#pragma once

// ── Pin definitions ───────────────────────────────────────────────────────

// NeoPixel ring
#define PIN_NEOPIXEL    27
#define NEOPIXEL_COUNT  16

// PN532 NFC reader (I2C — SDA=21, SCL=22 are ESP32 defaults)
#define PIN_PN532_IRQ   25
#define PIN_PN532_RST   26

// eInk display (SPI — change to match your wiring)
// Defaults for Waveshare 2.9" V2 on common ESP32 dev boards
#define PIN_EINK_CS     5   // GPIO5 is an ESP32 strapping pin; CS idles HIGH so boot is unaffected
#define PIN_EINK_DC     17
#define PIN_EINK_RST    16
#define PIN_EINK_BUSY   4

// BOOT/GPIO0 button — hold on power-up to factory reset
#define PIN_FACTORY_RESET 0

// TP4056 charging status — solder to LED cathode pads on the TP4056 module
// Both are open-drain, active LOW; use INPUT_PULLUP on the ESP32 side.
#define PIN_CHRG   32   // LOW while battery is charging
#define PIN_STDBY  33   // LOW when charge is complete (battery full)

// ── WiFi / captive portal ─────────────────────────────────────────────────
#define WIFI_AP_NAME        "Jellybox-Setup"
#define WIFI_AP_PASSWORD    ""          // open AP
#define WIFI_TIMEOUT_S      180         // seconds before config portal times out

// ── NVS storage ───────────────────────────────────────────────────────────
#define NVS_NAMESPACE   "jellybox"
#define NVS_KEY_SERVER  "serverUrl"
#define NVS_KEY_APIKEY  "apiKey"

// ── Timing ────────────────────────────────────────────────────────────────
#define BOOTSTRAP_INTERVAL_MS  30000UL  // poll /api/device/me every 30 s
#define SCAN_DEBOUNCE_MS        3000UL  // ignore same UID for 3 s
#define FACTORY_RESET_HOLD_MS   3000UL  // hold BOOT 3 s → factory reset
#define LED_UPDATE_INTERVAL_MS    20UL  // ~50 fps LED refresh

// ── Device config (loaded from NVS) ───────────────────────────────────────
struct DeviceConfig {
  String serverUrl;  // e.g. "https://jellybox.example.com"  (no trailing slash)
  String apiKey;     // e.g. "jb_abc123..."
};
