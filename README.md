# RPW Sentinel — Red Palm Weevil Early Detection System

> ESP32-based acoustic detection network using TKEO burst analysis + LoRa wireless alerts.

Developed at **Sathyabama Institute of Science and Technology**, ECE Dept.  
Guide: Dr. G. Rajalakshmi

---

## What it does

Each sensor node contacts a palm tree via a **27mm PZT-5A piezo disc** bonded to a metal screw driven into the trunk. The ESP32 samples the signal at **8 kHz**, runs a **Teager-Kaiser Energy Operator (TKEO)** pipeline, counts larval chewing bursts over a 30-second window, and classifies the tree as `NORMAL` / `SUSPICIOUS` / `INFESTED`. Results are transmitted over **LoRa (866 MHz)** to a central gateway node.

**Target cost: ~₹252/tree** (4-tree cluster node)

---

## Repository structure

```
rpw-sentinel/
├── sensor_node/
│   └── sensor_node.ino       # Main detection firmware (ESP32 + piezo + Ra-02)
├── gateway/
│   └── gateway.ino           # LoRa receiver + alert output (ESP32 + Ra-02)
├── tools/
│   ├── debug_monitor.ino     # ADC + TKEO live printout — flash first to verify hardware
│   └── lora_range_test.ino   # TX/RX range characterisation tool
├── hardware/
│   └── wiring.md             # Pin tables and circuit description
├── docs/
│   └── SETUP_GUIDE.md        # Full setup: IDE, libraries, flashing order, troubleshooting
└── README.md
```

---

## Quick start

1. **Hardware** — see `docs/SETUP_GUIDE.md` → "Hardware connections"
2. **Verify analog chain** — flash `tools/debug_monitor.ino`, tap the screw, confirm spikes in Serial Monitor
3. **Flash sensor node** — set a unique `NODE_ID` in `sensor_node.ino`, upload
4. **Flash gateway** — second ESP32, `gateway.ino`, no other changes needed
5. **LoRa range test** — use `tools/lora_range_test.ino` to find your link limit before deployment

Full step-by-step in [`docs/SETUP_GUIDE.md`](docs/SETUP_GUIDE.md).

---

## Signal processing pipeline

```
Piezo → LM358 (20 dB) → ESP32 ADC (12-bit, 8 kHz)
      → IIR HPF (fc = 200 Hz, α = 0.9355)
      → TKEO (ψ[n] = x[n]² − x[n+1]·x[n−1])
      → Adaptive threshold (mean + 3σ  or  P95, whichever is higher)
      → Burst grouping (200 ms window, 500 ms inter-burst gap, ≥3 crossings/burst)
      → Decision: 0 bursts → NORMAL | 1–2 → SUSPICIOUS | ≥3 → INFESTED
                  (or 3× consecutive SUSPICIOUS → escalate to INFESTED)
```

---

## Hardware BOM (per sensor node)

| Component | Qty | Notes |
|-----------|-----|-------|
| ESP32-WROOM-32 | 1 | Any 38-pin module |
| 27mm PZT-5A piezo disc | 1 | Bond to M6 screw head with epoxy |
| Ra-02 LoRa module (433/868 MHz) | 1 | **3.3 V only — never 5 V** |
| LM358 op-amp | 1 | Dual supply; powered from 3.3 V |
| LM393 comparator | 1 | Wake interrupt |
| Resistors (10k, 47k, 90k, 100k, 220Ω) | — | See `hardware/wiring.md` |
| 47 nF capacitor | 1 | HPF pole with 10 kΩ → fc ≈ 200 Hz |
| 3-colour LED (R/G/A) | 1 | Or 3 discrete LEDs |
| Passive buzzer | 1 | |
| ANT wire | 1 | 82 mm for 866 MHz, 173 mm for 433 MHz |

---

## LoRa configuration

| Parameter | Value |
|-----------|-------|
| Frequency | 866 MHz (India WPC permitted) |
| Spreading Factor | SF10 |
| Bandwidth | 125 kHz |
| Coding Rate | 4/5 |
| Sync Word | 0xF3 (private network) |
| TX Power | 17 dBm |

---

## Decision thresholds

| Parameter | Value | Source |
|-----------|-------|--------|
| Burst grouping window | 200 ms | Pinhas et al. 2008 (inter-event 100–300 ms) |
| Min inter-burst gap | 500 ms | Pinhas et al. 2008 (rest gap 400–700 ms) |
| Min crossings/burst | 3 | Empirical — rejects mechanical impulses |
| Threshold σ multiplier | 3.0 | Adjustable via `THRESHOLD_SIGMA` |

---

## Dependencies

- **Arduino IDE 2.x**
- **ESP32 board support** by Espressif Systems (via Board Manager)
- **LoRa** by Sandeep Mistry (via Library Manager)

---

## License

MIT
