# Jellybox Firmware

<p align="center">
  <img src="Jellybox.png" alt="Jellybox" width="120" />
</p>

ESP32 firmware for the Jellybox physical media player. Scan an RFID/NFC tag → instant playback on your Jellyfin server. No apps, no menus, no typing.

This firmware talks to a [Jellybox Server](https://github.com/jamiebarltett/jellybox) instance. You need a server account and a paired device before the firmware does anything useful.

---

## Hardware

| Component | Part |
|---|---|
| MCU | ESP32 Dev Module (any board with GPIO0 BOOT button) |
| NFC reader | PN532 breakout — I2C mode |
| Display | Waveshare 2.9" V2 B/W eInk (296 × 128) |
| LEDs | WS2812B NeoPixel ring — 12 pixels |
| Power | TP4056 with protection + boost converter (4-pad dual-function module) |
| Battery | 18650 Li-Ion cell, 2500–3500 mAh |

See [WIRING.md](WIRING.md) for the full pinout, wiring diagrams, power budget, and component notes.

---

## Required Libraries

Install all of these via **Arduino IDE → Tools → Manage Libraries**:

| Library | Author | Min version |
|---|---|---|
| WiFiManager | tzapu | 2.0.17 |
| Adafruit NeoPixel | Adafruit | 1.12 |
| Adafruit PN532 | Adafruit | 1.3 |
| GxEPD2 | ZinggJM | 1.6 |
| Adafruit GFX Library | Adafruit | 1.11 |
| ArduinoJson | Benoit Blanchon | 7.0 (or 6.x — see `APIClient.h`) |

---

## Building and Flashing

1. Open `jellybox-firmware.ino` in Arduino IDE
2. Set board: **Tools → Board → ESP32 Dev Module**
3. Set partition scheme: **Tools → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)**
   - The default 1.2 MB partition is too small for TLS + all libraries
4. Select the correct port under **Tools → Port**
5. Click **Upload**

If the IDE shows "Connecting…" and stalls, hold the **BOOT button** on the ESP32 board until the upload begins.

> The TP4056 USB port is for battery charging only — it is not connected to the ESP32's UART. Use the ESP32 dev board's own USB port for flashing and serial monitoring.

---

## First-Time Setup

The pairing wizard in the Jellybox Server dashboard handles everything automatically. In brief:

1. Flash the firmware and power the device
2. The Jellybox splash screen appears, then "Connecting…"
3. The device broadcasts a WiFi AP: **`Jellybox-Setup`** (open, no password)
4. Open the Jellybox Server dashboard → **Devices → Pair New Device**
5. Follow the wizard — it detects the device portal automatically and submits your WiFi credentials, server URL, and API key directly to the device
6. The device reboots, connects to your network, and shows the device name on the eInk display

If you prefer to configure manually:
- Connect to the `Jellybox-Setup` network
- Navigate to `192.168.4.1` (captive portal opens automatically on most phones)
- Enter WiFi credentials, server URL, and API key, then submit

The portal times out after **180 seconds**. If it closes before you finish, the device restarts and broadcasts the AP again.

---

## Factory Reset

Hold the **BOOT button for 3 seconds** at any point while the device is running. The device clears all stored WiFi credentials and server config from NVS, then restarts into setup mode.

You can also trigger a factory reset during power-up: hold BOOT while connecting power.

---

## LED States

The 12-pixel NeoPixel ring indicates device state at a glance.

| Colour / pattern | Meaning |
|---|---|
| Blue breathing | Ready to scan — idle, waiting for a tag |
| Cyan spin (comet) | Connecting — WiFi handshake or HTTP request in progress |
| Yellow spin (comet) | Bootstrapping — contacting the server |
| Purple breathing | Scan-capture mode — the next scan registers a tag, not play |
| Green flash | Success — playback started, or tag captured |
| Red flash | Error — check serial monitor for detail |
| Amber breathing | Unpaired — no config, or API key rejected (401) |
| Fast white breathing | Charging — battery is charging via USB |
| Slow green breathing | Charged — battery is full |

Charging states only show when the device is idle (READY). Scan-capture mode, errors, and success flashes take priority.

---

## eInk Display Screens

| Screen | When shown |
|---|---|
| Splash (logo + JELLYBOX) | Once at every boot |
| Connecting… | WiFi connecting with no known network |
| Connecting to `<SSID>` | After WiFi connects — shows the network name |
| Unpaired — connect to Jellybox-Setup | No config stored, or API key rejected |
| `<device name>` / Ready to scan | Idle, waiting for a tag |
| `<device name>` / `[ SCAN MODE ]` | Scan-capture mode active |
| `<device name>` / Now playing: `<title>` | Tag scanned, playback started |
| Error: `<message>` | Something went wrong |

---

## File Structure

| File | Purpose |
|---|---|
| `jellybox-firmware.ino` | `setup()` / `loop()` — state machine, WiFiManager, app entry point |
| `Config.h` | Pin definitions, NVS keys, timing constants |
| `LEDRing.h` | Non-blocking NeoPixel animations (breathing, spin, flash) |
| `EInkDisplay.h` | GxEPD2 screen layouts — all display states in one class |
| `JellyboxLogo.h` | 72×72 px PROGMEM bitmap for the splash / ready screens |
| `NFCReader.h` | PN532 I2C wrapper with UID debounce |
| `APIClient.h` | HTTPS client for `/api/device/me` and `/api/play` |
| `WIRING.md` | Full hardware wiring guide, pinout, and power budget |

---

## State Machine

```
                       ┌─────────────┐
          power on     │ UNCONFIGURED│ ◄── 401 from server
         ─────────────►│             │
                       └──────┬──────┘
                              │ WiFi + config saved
                              ▼
                       ┌─────────────┐
                       │ CONNECTING  │ WiFiManager autoConnect
                       └──────┬──────┘
                              │ connected
                              ▼
                       ┌──────────────┐
             ┌────────►│ BOOTSTRAPPING│ GET /api/device/me (every 30 s)
             │         └──────┬───────┘
             │                │ ok
             │       ┌────────┴────────┐
             │       │                 │
             │       ▼                 ▼
             │  ┌─────────┐     ┌───────────┐
             │  │  READY  │     │ SCAN_MODE │
             │  └────┬────┘     └─────┬─────┘
             │       │ NFC scan        │ NFC scan
             │       └────────┬────────┘
             │                │ POST /api/play
             └────────────────┘ (re-bootstrap every 30 s)
```

---

## API Interactions

The firmware makes two HTTPS calls to the server:

**`GET /api/device/me`** — called at boot and every 30 seconds  
Authenticated with `Authorization: Bearer <apiKey>`. Returns:
```json
{ "name": "Living Room Box", "scanMode": false }
```
A `401` response sets the device to UNPAIRED state. Other errors are retried on the next interval.

**`POST /api/play`** — called when a tag is scanned  
```json
{ "tagId": "A1B2C3D4" }
```
Returns either:
```json
{ "success": true, "content": "The Dark Knight" }
{ "captured": true }
{ "error": "Tag not assigned" }
```

TLS certificates are not verified on-device (`setInsecure()`). The API key is the shared secret; verifying the server cert would require bundling a CA store, which is impractical for a battery-powered device pointing at a self-hosted server.

---

## Customisation

### Changing the eInk panel

Edit the top of `EInkDisplay.h`. The include and class alias are the only two lines to change:

```cpp
// Example: Waveshare 2.13" V3 (250×122)
#include <epd/GxEPD2_213_BN.h>
using Panel = GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT>;
```

Adjust `EINK_W`, `EINK_H`, and `EINK_MARGIN_R` to match the new panel's pixel dimensions and dead zone.

### Changing the NeoPixel count or pin

Edit `Config.h`:
```cpp
#define PIN_NEOPIXEL   27
#define NEOPIXEL_COUNT 12
```

### Adjusting poll intervals

Also in `Config.h`:
```cpp
#define BOOTSTRAP_INTERVAL_MS  30000UL  // server poll — every 30 s
#define SCAN_DEBOUNCE_MS        3000UL  // ignore same UID for 3 s
#define FACTORY_RESET_HOLD_MS   3000UL  // hold BOOT 3 s to reset
```

---

## Stored Configuration (NVS)

Config is stored in the ESP32's Non-Volatile Storage under the `jellybox` namespace. The partition scheme change (`Huge APP`) does not affect NVS — it is a separate partition and survives reflashing.

| Key | Value |
|---|---|
| `serverUrl` | Full server URL, e.g. `https://jellybox.example.com` |
| `apiKey` | Device API key, e.g. `jb_abc123…` |

WiFi credentials are stored separately by the ESP32 WiFi stack and erased by `WiFi.disconnect(true, true)` during a factory reset.

---

## Serial Monitor

Connect at **115200 baud**. All state transitions, NFC scans, API calls, and errors are logged with prefixed tags:

```
[NFC]    Tag: A1B2C3D4
[API]    POST /api/play → 200
[Play]   Playing: The Dark Knight
[Boot]   Device: Living Room Box scanMode=0
[Charge] charging
[Reset]  BOOT held...
```

---

## License

MIT
