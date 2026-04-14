# Jellybox Hardware Wiring

Hardware reference for assembling a Jellybox device.
Signal logic runs at **3.3 V**. The TP4056 module provides a regulated **5 V** rail that
powers everything; the ESP32's onboard LDO steps that down to 3.3 V for the PN532 and eInk.

---

## Components

| Component | Part |
|---|---|
| MCU | ESP32 Dev Module (any board with GPIO0 BOOT button) |
| Power | 5 V / 1 A TP4056 with protection + boost converter (dual-function, 4-pad module) |
| Battery | 18650 Li-Ion cell — see battery selection note below |
| NFC reader | PN532 breakout (I2C mode) |
| Display | Waveshare 2.9" V2 B/W eInk (296 × 128) |
| LEDs | WS2812B NeoPixel ring — 12 pixels |
| Reset button | On-board BOOT button (GPIO0) |

---

## Battery Selection

Any genuine 18650 Li-Ion cell. The TP4056 charges at ~1 A, so aim for at least 1500 mAh
(keeping charge rate ≤ 0.66 C). A **2500–3500 mAh** cell is ideal.

| Cell | Capacity | Notes |
|---|---|---|
| Samsung 25R | 2500 mAh | Widely available, reliable |
| LG HG2 | 3000 mAh | Good capacity/current balance |
| Molicel P26A | 2600 mAh | Excellent quality |

Buy from a reputable supplier. Avoid unbranded cells claiming ≥ 5000 mAh — 18650 cells
physically cannot hold more than ~3600 mAh; anything higher is a false rating.

**Estimated runtime** at typical idle (WiFi connected, LED breathing, no active scans):
~120–150 mA average draw → **16–23 hours** from a 2500 mAh cell.

---

## Pin Assignment

| Signal | ESP32 GPIO | Connected to |
|---|---|---|
| I2C SDA | 21 (default) | PN532 SDA |
| I2C SCL | 22 (default) | PN532 SCL |
| PN532 IRQ | 25 | PN532 IRQ |
| PN532 RST | 26 | PN532 RST / RSTO |
| SPI MOSI | 23 (VSPI default) | eInk DIN |
| SPI SCK | 18 (VSPI default) | eInk CLK |
| eInk CS | 5¹ | eInk CS |
| eInk DC | 17 | eInk DC |
| eInk RST | 16 | eInk RST |
| eInk BUSY | 4 | eInk BUSY |
| NeoPixel DATA | 27 | Ring DATA IN (via 470 Ω) |
| TP4056 CHRG | 32 | Red LED cathode on TP4056 module |
| TP4056 STDBY | 33 | Blue LED cathode on TP4056 module |
| Factory reset | 0 | BOOT button (on-board) |

> ¹ GPIO5 is an ESP32 boot-mode strapping pin. The eInk CS line idles HIGH (SPI CS is
> active-low), so it does not affect boot behaviour. If you ever see unexpected boot
> failures, probe GPIO5 at power-on.

---

## 1 — TP4056 Power Module

The module has **4 solder pads** and a single USB port (charging input):

| Pad label | Purpose |
|---|---|
| `OUT+` | 5 V regulated output — connect to ESP32 VIN and NeoPixel PWR |
| `OUT−` | GND — connect to common ground |
| `B+` | Battery positive — connect to 18650 cell + terminal |
| `B−` | Battery negative — connect to 18650 cell − terminal |

```
USB (wall/PC)
     │
 [TP4056 module]
  ┌──┴────────────────┐
  │  B+  ←→  18650 +  │
  │  B−  ←→  18650 −  │
  │                   │
  │  OUT+ ──→ 5V rail │
  │  OUT− ──→ GND     │
  └───────────────────┘
```

**Simultaneous charge and run:** this module supports pass-through — the device can run from
the 5 V output while USB is connected and the battery is charging.

**Current headroom:** total worst-case draw is ~470 mA (see power budget below). The module is
rated 1 A output, leaving ~530 mA headroom. Simultaneous WiFi TX + NFC field + full LED is
unlikely in practice; average draw in normal use is well under 200 mA.

---

## 2 — TP4056 Charging Indicator (optional)

The firmware uses the NeoPixel ring to indicate charge state. This requires tapping the
TP4056's two status outputs: `CHRG` (active while charging) and `STDBY` (active when full).
Both are open-drain, active-LOW signals.

### Where to solder

Your module has two small indicator LEDs on the PCB (typically red = charging, blue = full).
Solder fine wire to the **cathode** of each LED — that pad connects directly to the TP4056
status pin. The cathode is the shorter leg, or the leg on the flat/marked side of the LED.

Use a fine-tipped iron and thin wire (~28 AWG). The pads are small; work quickly to avoid
lifting them.

```
TP4056 module (LED cathode pads)    ESP32
────────────────────────────────────────────
Red LED cathode  (CHRG)   →   GPIO 32   (INPUT_PULLUP)
Blue LED cathode (STDBY)  →   GPIO 33   (INPUT_PULLUP)
```

### Why no voltage divider is needed

The ESP32's internal pull-up (45 kΩ to 3.3 V) clamps the floating-high state to 3.3 V.
The on-board LED's series resistor (~1 kΩ) + forward voltage (~2 V) prevents the 5 V rail
from forward-biasing the LED in the direction of the GPIO, so no 5 V reaches the pin.
When the TP4056 pulls the output LOW, the GPIO reads 0 V regardless of the pull-up.

### LED behaviour

| Ring animation | Meaning |
|---|---|
| Fast white breathing (~2.4 s) | Battery charging |
| Slow green breathing (~6 s) | Battery full |
| Blue breathing (normal IDLE) | On battery, not connected to charger |

Charging states only show when the device is in READY state. SCAN_MODE, UNPAIRED, and
transient flashes (SUCCESS / ERROR) take priority.

---

## 3 — PN532 NFC Reader (I2C)

### Set I2C mode first

The PN532 supports I2C, SPI, and UART. **You must select I2C before wiring.**

- **Adafruit breakout:** flip the two DIP switches — `SW1 = ON`, `SW2 = OFF`
- **Other breakouts:** consult the board's datasheet for the I2C solder-jumper or switch positions

### Connections

```
PN532 breakout       ESP32
──────────────────────────────
VCC           →   3.3V
GND           →   GND
SDA           →   GPIO 21
SCL           →   GPIO 22
IRQ           →   GPIO 25
RST (RSTO)    →   GPIO 26
```

### Pull-ups

The Adafruit PN532 breakout includes on-board 4.7 kΩ pull-ups on SDA and SCL — no external
resistors needed. If using a bare PN532 module without on-board pull-ups, add 4.7 kΩ resistors
from SDA → 3.3 V and SCL → 3.3 V.

---

## 4 — Waveshare 2.9" V2 eInk Display (SPI)

Pin labels vary by module revision. Common label sets and their mappings:

| Signal | Variant A labels | Variant B labels |
|---|---|---|
| Power | VCC | VCC |
| Ground | GND | GND |
| Data (MOSI) | DIN | SDA |
| Clock (SCK) | CLK | SCL |
| Chip select | CS | CS |
| Data/command | DC | DC |
| Reset | RST | RES |
| Busy | BUSY | BUSY |

MISO is not connected — the panel is write-only.

```
Your module    Signal        ESP32
──────────────────────────────────
VCC        →  3.3V
GND        →  GND
SDA / DIN  →  GPIO 23   (MOSI)
SCL / CLK  →  GPIO 18   (SCK)
CS         →  GPIO  5
DC         →  GPIO 17
RES / RST  →  GPIO 16
BUSY       →  GPIO  4
```

GxEPD2 initialises the SPI bus internally — no `SPI.begin()` call is needed in the sketch.

---

## 5 — WS2812B NeoPixel Ring (12 pixels)

```
NeoPixel ring        Power / ESP32
────────────────────────────────────
PWR (5V)      →   TP4056 OUT+  (5 V rail — not the ESP32 5V pin)
GND           →   GND
DATA IN       →   GPIO 27  (via 300–470 Ω series resistor)
```

**Why TP4056 OUT+ directly?** The ESP32 dev board's 5 V / VBUS pin is only live when USB is
connected. When running on battery alone that pin is dead. Tapping the 5 V rail from the
TP4056 output ensures the ring is powered in both USB and battery modes.

**Series resistor:** place a 300–470 Ω resistor on the data line between GPIO27 and the
ring's DATA IN pad. It suppresses ringing and protects the first pixel's input driver.

---

## 6 — Factory Reset Button

The BOOT button is already wired to GPIO0 on most ESP32 dev boards with an on-board pull-up.
No additional wiring is needed.

If building a custom PCB, wire a momentary pushbutton between GPIO0 and GND. The firmware
configures `INPUT_PULLUP` in software, so no external pull-up is required.

**To factory reset:** hold the BOOT button for 3 seconds on power-up. The device clears its
NVS config and stored WiFi credentials, then restarts into setup mode.

---

## 7 — Power Budget

| Subsystem | Typical draw |
|---|---|
| ESP32 active + WiFi TX | ~200 mA peak |
| WS2812B ring (brightness 80/255) | ~150 mA |
| PN532 during RF field | ~100 mA peak |
| eInk during refresh | ~20 mA |
| **Total worst-case** | **~470 mA** |
| **TP4056 output rating** | **1000 mA** |
| **Headroom** | **~530 mA** |

All peaks are brief and non-overlapping in normal use. Average idle draw (WiFi connected,
LED breathing, waiting for a scan) is approximately **120–150 mA**.

The TP4056 module's onboard LDO is not in this path — the boost converter output feeds
ESP32 VIN directly. The ESP32's onboard AMS1117 (3.3 V, 800 mA) is fed clean 5 V and
supplies the PN532 and eInk from its 3V3 pin.

---

## 8 — Full Wiring Diagram

```
  [USB charger]                    [18650 cell]
       │                            │       │
       │          ┌─────────────────┤ B+  B−├──┐
       └──────────┤ USB    TP4056              │ │
                  │  5V boost module           │ │
                  │         OUT+ ──────────────┼─┼──── 5V rail
                  │         OUT− ──────────────┼─┼──── GND rail
                  └────────────────────────────┘ │
                                                  │ (battery ground)
                                                  ▼

  5V rail ────────────────────────────────────────────────┐
                                                          │
                 ┌──────────────────────────────────────┐ │
    NeoPixel PWR─┤ VIN (→ AMS1117 → 3V3)               │ │
                 │                                       │ │
 [NeoPixel]──470Ω│ GPIO27              GPIO21 ───────────│─┼── SDA ──[PN532]
 [eInk RES/RST]──│ GPIO16              GPIO22 ───────────│─┼── SCL ──[PN532]
 [eInk DC]───────│ GPIO17              GPIO25 ───────────│─┼── IRQ ──[PN532]
 [eInk BUSY]─────│ GPIO 4              GPIO26 ───────────│─┼── RST ──[PN532]
 [eInk CS]───────│ GPIO 5                                │ │
 [eInk SCL/SCK]──│ GPIO18              GPIO 0 ───────────│─┼── BOOT button
 [eInk SDA/DIN]──│ GPIO23                                │ │
                 │                          3V3 ─────────│─┼── PN532 VCC
                 │                          3V3 ─────────│─┼── eInk VCC
                 │                          GND ─────────│─┼── all GND
                 └──────────────────────────────────────┘ │
                                                          │
  GND rail ────────────────────────────────────────────── ┘
```

---

## 9 — First Boot

1. Flash the firmware via Arduino IDE (**Board: ESP32 Dev Module**) with USB connected
2. Disconnect USB. The device now runs from the battery (LED will light)
3. On first boot it broadcasts a WiFi AP: **`Jellybox-Setup`** (open, no password)
4. Connect to it — a captive portal opens automatically (or navigate to `192.168.4.1`)
5. Enter your WiFi credentials, the server URL, and the device API key from the dashboard
6. Submit — the device reboots, connects, and shows the device name on the eInk display

If the portal times out (180 s default) without a submission, the device restarts and
broadcasts the AP again.

> **Flashing note:** flashing via Arduino IDE requires USB. The TP4056's USB port is for
> charging only — it is not connected to the ESP32's USB/UART. Use the ESP32 dev board's own
> USB port for flashing and serial monitoring.
