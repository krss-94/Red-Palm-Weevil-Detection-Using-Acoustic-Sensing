# Hardware Wiring Reference

All circuits run on **3.3 V**. Never connect Ra-02 or LM358 to 5 V.

---

## Sensor Node — complete pin table

### ESP32 → Ra-02 LoRa (SPI)

| Ra-02 Pin | ESP32 GPIO |
|-----------|-----------|
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| NSS (CS) | 5 |
| RESET | 14 |
| DIO0 | 2 |
| 3.3V | 3.3V |
| GND | GND |
| ANT | 82 mm wire (866 MHz) or 173 mm (433 MHz) |

### ESP32 GPIO assignments

| GPIO | Function |
|------|----------|
| 34 | ADC input (piezo signal via LM358 bias network) |
| 35 | Interrupt input (LM393 comparator output) |
| 25 | Green LED (NORMAL) |
| 26 | Amber LED (SUSPICIOUS) |
| 27 | Red LED (INFESTED) |
| 32 | Buzzer |

---

## Analog front end

### Piezo → LM358 HPF + amplifier

```
PIEZO(+) ──┬── 10 kΩ ──┬── LM358 IN+(pin 3)
           │           │
           └── 47 nF ──┘       fc = 1/(2π·10k·47n) ≈ 339 Hz
                                (with HPF_ALPHA=0.9355 software HPF at 200 Hz as backup)

PIEZO(−) ────────────────── GND

LM358 pin 8 (V+)  ── 3.3 V
LM358 pin 4 (V−)  ── GND
LM358 pin 2 (IN−) ── 90 kΩ feedback from OUT (pin 1)
Input resistor (IN+ to IN−): 10 kΩ
Gain = 1 + 90k/10k = 10× (20 dB)
```

### Bias network (centres output at 1.65 V for ESP32 ADC)

```
LM358 OUT ── 10 kΩ ── GPIO 34
                  │
                10 kΩ ── 3.3 V
                  │
                 GND
```

### LM393 comparator (wake interrupt)

```
LM393 IN+ ── LM358 OUT
LM393 IN− ── voltage divider: 10 kΩ from 3.3 V, 100 kΩ to GND → ~0.3 V ref
LM393 OUT ── 10 kΩ pullup to 3.3 V ── GPIO 35
LM393 V+  ── 3.3 V
LM393 V−  ── GND
```

### LEDs and buzzer

```
GPIO 25 ── 220 Ω ── Green LED ── GND
GPIO 26 ── 220 Ω ── Amber LED ── GND
GPIO 27 ── 220 Ω ── Red LED   ── GND
GPIO 32 ── Buzzer(+) ── GND   (passive buzzer; active buzzer: add NPN transistor)
```

---

## Gateway Node

Same LoRa wiring as sensor node.  
Same LED + buzzer wiring.  
No piezo, no LM358, no LM393 — LoRa RX only.
