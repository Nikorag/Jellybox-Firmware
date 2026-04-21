# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build / Flash

Arduino IDE project — there is no CLI build script in the repo. Open `jellybox-firmware.ino`, select **ESP32 Dev Module** and partition scheme **Huge APP (3MB No OTA/1MB SPIFFS)** (the default 1.2 MB partition is too small for TLS + the libraries), then Upload. If the board stalls at "Connecting…", hold the GPIO0 BOOT button until the upload starts.

Flash and serial monitor (115200 baud) use the ESP32 dev board's own USB — **not** the TP4056 port, which is power-only.

Required libraries (Library Manager): WiFiManager (tzapu), Adafruit NeoPixel, Adafruit PN532, GxEPD2 (ZinggJM), Adafruit GFX, ArduinoJson 7.x (the code uses v7 `JsonDocument` — downgrading to v6 requires changing `APIClient.h`).

No tests, no linter.

## Architecture

Single-file `.ino` sketch driving four hardware peripherals through header-only wrappers. Each `*.h` defines one class with inline implementation — there are no `.cpp` files.

**Runtime model.** `loop()` is a cooperative, non-blocking scheduler. Every subsystem (LED animation, NFC polling, charge-pin read, 30 s server poll, factory-reset hold detection) must return within ~100 ms so the others stay responsive. `NFCReader::readUID()` blocks for up to 100 ms by design; `LEDRing::update()` advances animation phase by one tick per call and relies on being called ≥50 Hz for smooth motion. Long-running blocks (like the 3 s error display in `handleScan`) must still pump `led.update()` and `checkFactoryReset()` inside their wait loops — see `jellybox-firmware.ino:203-204` for the pattern.

**State machine.** `AppState` in `jellybox-firmware.ino` has five states: `UNCONFIGURED` → `CONNECTING` → `BOOTSTRAPPING` → `READY` ⇄ `SCAN_MODE`. Transitions are driven by:
- WiFiManager autoConnect result in `setup()`
- `BootstrapResult` from `GET /api/device/me` every `BOOTSTRAP_INTERVAL_MS` (30 s) — a `401` drops back to `UNCONFIGURED` and shows the unpaired screen; `scanMode: true` in the response puts the device into `SCAN_MODE`.
- NFC scans in `READY`/`SCAN_MODE` trigger `POST /api/play`; the server decides whether the response is a play (`success`+`content`) or a capture (`captured: true`).

The server is authoritative for scan-mode vs. play-mode — the firmware never decides locally, it just re-polls and follows `res.scanMode`.

**LED state vs. base state.** `LEDRing` distinguishes the transient flash states (`SUCCESS`, `ERROR`, 800 ms) from persistent states. `setState()` on a flash auto-reverts to `_baseState` when it expires; `setBaseState()` sets the post-flash target separately. When changing app state, set both (see `doBootstrap` at `jellybox-firmware.ino:154-155`).

**TLS is intentionally unverified** (`client.setInsecure()` in `APIClient.h`). The API key is the shared secret; a battery device pointing at a self-hosted server can't realistically carry a CA bundle. Don't "fix" this without the context in README.md §API Interactions.

**Config lifecycle.** `DeviceConfig` (`serverUrl`, `apiKey`) lives in NVS under the `jellybox` namespace. WiFi creds live separately in the ESP32 WiFi stack. `factoryReset()` clears both (`prefs.clear()` + `WiFi.disconnect(true, true)`) and reboots. The captive portal's `onPortalSave` callback writes to NVS — `loadConfig()` is called again after `startWiFi()` to pick up values set by the portal.

**Config portal / captive-portal detection.** `startWiFi()` registers handlers for `/hotspot-detect.html` (iOS/macOS) and `/generate_204` (Android) so phones auto-open the portal. `PORTAL_CSS` is a dark-theme stylesheet injected via `setCustomHeadElement` — match its palette if you add new portal pages.

**eInk panel swap.** To change display, edit the include and `using Panel = ...` alias at the top of `EInkDisplay.h`, then adjust `EINK_W`, `EINK_H`, `EINK_MARGIN_R` (the right-side dead zone varies per panel). All draw methods use full-window paged refresh; switching `showLastPlayed` to `setPartialWindow` would remove the full-screen flash on every scan (noted in the file).

**Charge detection.** `PIN_CHRG`/`PIN_STDBY` are `INPUT_PULLUP` reading the TP4056 LED cathode pads (active-LOW, open-drain). `checkCharging()` only updates the LED when `appState == READY` so it never overrides `SCAN_MODE` / flash states. Ticked every 5 s from `loop()`.

## Gotchas

- **PN532 jumper.** I2C mode needs SW1=ON, SW2=OFF on the breakout. `NFCReader::begin()` retries `getFirmwareVersion()` 5×; a persistent failure is almost always the jumper.
- **GPIO5 as `PIN_EINK_CS`** is a strapping pin — CS idling HIGH keeps boot unaffected. Don't reassign without re-checking boot mode.
- **`http.setTimeout()`** in `APIClient.h` is tuned (6 s / 8 s) to stay under the default task WDT window including TLS handshake. Don't raise blindly.
