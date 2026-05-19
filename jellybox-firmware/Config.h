#pragma once

// ── Firmware version ──────────────────────────────────────────────────────
// Injected at build time via -DFIRMWARE_VERSION="vX.Y.Z" by the release
// workflow. Falls back to "dev" for local Arduino IDE builds.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// ── Hardware SKU ──────────────────────────────────────────────────────────
// Identifies the hardware variant this build targets. Injected at build time
// via -DJELLYBOX_SKU="jb-...-vN" by the release workflow's matrix. Devices
// report this to the server so it can offer the right OTA build and tell
// hardware variants apart on the dashboard. See apps/server/src/lib/skus.ts
// for the canonical registry.
#ifndef JELLYBOX_SKU
#define JELLYBOX_SKU "jb-eink-v1"
#endif

// ── Per-SKU capability flags ──────────────────────────────────────────────
// JELLYBOX_SKU is a string at compile time, which can't be used in #if
// directly. CI passes a matching numeric guard (e.g. -DJELLYBOX_SKU_TFT_V1)
// alongside the SKU string; this block expands that into capability flags
// the rest of the firmware #ifdefs on. Adding a future SKU means adding a
// branch here, not sprinkling SKU strings through the codebase.
#if defined(JELLYBOX_SKU_TFT_V1)
  #define JELLYBOX_DISPLAY_TFT      1
  #define JELLYBOX_HAS_AUDIO        1
  #define JELLYBOX_HAS_VOLUME_DIAL  1
#else
  // Default: jb-eink-v1
  #define JELLYBOX_DISPLAY_EINK     1
#endif

// ── Pin definitions ───────────────────────────────────────────────────────

// NeoPixel ring
#define PIN_NEOPIXEL    27
#define NEOPIXEL_COUNT  16

// PN532 NFC reader (I2C — SDA=21, SCL=22 are ESP32 defaults)
#define PIN_PN532_IRQ   25
#define PIN_PN532_RST   26

#ifdef JELLYBOX_DISPLAY_EINK
// eInk display (SPI — change to match your wiring)
// Defaults for Waveshare 2.9" V2 on common ESP32 dev boards
#define PIN_EINK_CS     5   // GPIO5 is an ESP32 strapping pin; CS idles HIGH so boot is unaffected
#define PIN_EINK_DC     17
#define PIN_EINK_RST    16
#define PIN_EINK_BUSY   4
#endif

#ifdef JELLYBOX_DISPLAY_TFT
// ST7789 colour TFT (SPI — shared VSPI MOSI=23/SCK=18). Reuses the freed
// eInk GPIO assignments so the same board layout works for both SKUs.
#define PIN_TFT_CS      5
#define PIN_TFT_DC      17
#define PIN_TFT_RST     16
#define TFT_W           320
#define TFT_H           240
#endif

#ifdef JELLYBOX_HAS_AUDIO
// MAX98357A I2S amplifier. Non-strapping pins; BCLK and LRC must be
// PWM-capable outputs, DIN is a regular GPIO.
#define PIN_I2S_BCLK    14
#define PIN_I2S_LRC     33
#define PIN_I2S_DIN     32
#define I2S_SAMPLE_RATE 16000   // 16 kHz mono — plenty for short cues
#endif

#ifdef JELLYBOX_HAS_VOLUME_DIAL
// KY-040 rotary encoder. ESP32 input-only pins (34/35/39) are fine — the
// encoder breakout has onboard pull-ups, so internal pull-ups aren't needed.
#define PIN_ENC_CLK     35
#define PIN_ENC_DT      34
#define PIN_ENC_SW      39      // press-button (reserved; not wired in v1)
#define NVS_KEY_VOLUME  "volume"
#define VOLUME_MAX      16      // step scale (matches NEOPIXEL_COUNT)
#define VOLUME_DEFAULT  12
#endif

// BOOT/GPIO0 button — hold on power-up to factory reset
#define PIN_FACTORY_RESET 0

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
#define SCAN_DEBOUNCE_MS       15000UL  // ignore same UID for 15 s
#define FACTORY_RESET_HOLD_MS   3000UL  // hold BOOT 3 s → factory reset
#define LED_UPDATE_INTERVAL_MS    20UL  // ~50 fps LED refresh
#define HEARTBEAT_INTERVAL_MS   5000UL  // UDP diagnostic heartbeat cadence

// ── UDP debug logging ─────────────────────────────────────────────────────
// Fire-and-forget logs to any machine on the LAN. Listen with:
//   nc -u -l 5514
//   socat -u UDP-LISTEN:5514,fork - | ts   (preferred — adds timestamps)
// Override UDP_LOG_HOST to a specific IP if your AP filters broadcast.
#define UDP_LOG_HOST   "255.255.255.255"
#define UDP_LOG_PORT   5514

// ── Device config (loaded from NVS) ───────────────────────────────────────
struct DeviceConfig {
  String serverUrl;  // e.g. "https://jellybox.example.com"  (no trailing slash)
  String apiKey;     // e.g. "jb_abc123..."
};
