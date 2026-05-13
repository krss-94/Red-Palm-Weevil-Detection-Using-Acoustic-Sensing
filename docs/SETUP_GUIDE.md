# RPW Detection System — Complete Setup Guide

## What you need to install (do this first)

### 1. Arduino IDE
Download: https://www.arduino.cc/en/software
Version 2.x is recommended.

### 2. ESP32 board support
1. Open Arduino IDE → File → Preferences
2. In "Additional board manager URLs" paste:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Tools → Board → Boards Manager → search "esp32" → Install "esp32 by Espressif Systems"

### 3. LoRa library
Tools → Manage Libraries → search "LoRa" → Install **"LoRa" by Sandeep Mistry**

---

## Hardware connections

### Sensor Node (ESP32 + Piezo + Ra-02)

#### Analog front end (piezo → LM358 → ESP32)

```
PIEZO + ────┬─── 10kΩ ───┬─── LM358 pin 3 (IN+)
            │            │
            └─── 47nF ───┘   (This forms the 200Hz HPF)

PIEZO − ──────────────────── GND

LM358 connections:
  Pin 8 (V+) ─── 3.3V     ← IMPORTANT: use 3.3V not 5V
  Pin 4 (V−) ─── GND
  Pin 2 (IN−) ── feedback from pin 1 via 90kΩ resistor
  Pin 3 (IN+) ── HPF output (from above)
  Between IN− and OUT: 90kΩ (feedback)
  Between IN+ and IN−: 10kΩ (input)
  → Gain ≈ 1 + 90k/10k = 10× = 20 dB

Bias network (centres output at 1.65V for ESP32 ADC):
  LM358 OUT ─── 10kΩ ─── GPIO 34
                      │
                    10kΩ ─── 3.3V
                      │
                     GND
```

#### LM393 comparator (wake interrupt)
```
LM393 IN+ ─── LM358 OUT
LM393 IN− ─── 0.3V ref (voltage divider: 10kΩ from 3.3V, 100kΩ to GND)
LM393 OUT ─── 10kΩ pullup to 3.3V ─── GPIO 35
LM393 V+  ─── 3.3V
LM393 V−  ─── GND
```

#### LEDs and buzzer
```
GPIO 25 ─── 220Ω ─── GREEN LED ─── GND
GPIO 26 ─── 220Ω ─── AMBER LED ─── GND
GPIO 27 ─── 220Ω ─── RED LED   ─── GND
GPIO 32 ─── Buzzer (+) ─── GND
            (passive buzzer: connect directly; active buzzer: add NPN transistor)
```

#### Ra-02 LoRa module → ESP32 SPI
```
Ra-02 Pin   →   ESP32 Pin
─────────────────────────
SCK         →   GPIO 18
MISO        →   GPIO 19
MOSI        →   GPIO 23
NSS (CS)    →   GPIO 5
RESET       →   GPIO 14
DIO0        →   GPIO 2
3.3V        →   3.3V   ← NEVER connect to 5V, will destroy Ra-02
GND         →   GND
ANT         →   82mm wire (for 868MHz) or 173mm (for 433MHz)
```

### Gateway Node
Same LoRa wiring as sensor node.
Same LED + buzzer wiring.
No piezo, no LM358, no LM393.

---

## Flashing the firmware

### Step 1: Flash debug_monitor.ino FIRST
This verifies your analog hardware is working before running real detection.

1. Open `sensor_node/debug_monitor.ino` in Arduino IDE
2. Tools → Board → ESP32 Arduino → **ESP32 Dev Module**
3. Tools → Port → select your ESP32 COM port
4. Tools → Upload Speed → 921600
5. Click Upload (→ arrow)
6. Open Serial Monitor (Tools → Serial Monitor, baud = 115200)
7. **Tap the waveguide screw** — you should see the RAW_ADC column spike above 3000

If you see no spikes:
- Check LM358 power (must be 3.3V)
- Check piezo is bonded to screw head
- Check GPIO 34 bias network (should read ~2048 at rest)

### Step 2: Flash the sensor node
1. Open `sensor_node/sensor_node.ino`
2. Change `#define NODE_ID 1` to a unique number for each tree (1, 2, 3...)
3. Upload
4. Open Serial Monitor — you will see the 30-second window running
5. Tap the screw 4–5 times within 200ms (a burst), repeat 3 times → LED should go RED

### Step 3: Flash the gateway
1. Open `gateway/gateway.ino` on a SECOND ESP32
2. Upload — Serial Monitor should show "Listening on 866 MHz..."
3. Power up the sensor node — after its 30-second window, if state ≠ NORMAL, gateway LED will respond

### Step 4: LoRa range test
1. Open `gateway/lora_range_test.ino`
2. On one ESP32: uncomment `#define TX_MODE`, upload
3. On second ESP32: leave TX_MODE commented, upload
4. Walk TX unit away while watching RX Serial Monitor
5. Note distance where RSSI drops below -115 dBm — that is your link limit

---

## Troubleshooting

| Symptom | Most likely cause | Fix |
|---------|-------------------|-----|
| ADC always reads ~2048 | No signal from piezo | Check piezo bonding and LM358 power |
| ADC reads 4095 always | Op-amp saturating | Reduce gain (increase Rin to 20kΩ) |
| LoRa init FAILED | Wiring error or missing power | Check 3.3V on Ra-02, check SPI pins |
| Gateway never receives | Sync word mismatch | Confirm both nodes use 0xF3 |
| LED always NORMAL even when tapping | Threshold too high | Lower THRESHOLD_SIGMA from 3.0 to 2.0 |
| LED jumps to INFESTED in silence | Threshold too low | Raise THRESHOLD_SIGMA to 4.0 |
| Sketch won't compile | Missing LoRa library | Install "LoRa by Sandeep Mistry" |

---

## Citing burst timing parameters (for paper Fix 3)

The burst grouping values used in this implementation:
- 200ms grouping window — from Pinhas et al. (2008): RPW chewing events cluster in 100–300ms intervals
- 500ms minimum inter-burst gap — from Pinhas et al. (2008): larvae rest 400–700ms between feeding actions
- 3-crossing minimum — empirically tuned to reject single mechanical impulses (footsteps, tool impacts)

Add to Section IV-D of your paper:
"The grouping window (200 ms) and minimum inter-burst gap (500 ms) are derived from the temporal characterisation of RPW chewing reported in [2], which observed inter-event intervals of 100–300 ms within bursts and rest gaps of 400–700 ms between them."
