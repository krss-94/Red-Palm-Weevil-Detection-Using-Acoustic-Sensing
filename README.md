<div align="center">

<img src="https://img.shields.io/badge/Platform-ESP32-red?style=for-the-badge&logo=espressif&logoColor=white"/>
<img src="https://img.shields.io/badge/Protocol-LoRa%20866%20MHz-blueviolet?style=for-the-badge&logo=lora-alliance&logoColor=white"/>
<img src="https://img.shields.io/badge/Algorithm-TKEO%20Burst%20Analysis-orange?style=for-the-badge"/>
<img src="https://img.shields.io/badge/Cost-₹252%20%2F%20tree-brightgreen?style=for-the-badge"/>
<img src="https://img.shields.io/badge/License-MIT-blue?style=for-the-badge"/>
<img src="https://img.shields.io/badge/Build-Arduino%20IDE%202.x-teal?style=for-the-badge&logo=arduino&logoColor=white"/>

<br/><br/>

# 🌴 RPW Sentinel

### Red Palm Weevil Early Detection System

**ESP32 · PZT-5A Piezo · TKEO Signal Processing · LoRa Mesh Alerts**

*Detects* ***Rhynchophorus ferrugineus*** *larval chewing acoustics inside palm trunks — before visible symptoms appear.*

<br/>

> Developed at **Sathyabama Institute of Science and Technology**, ECE Department  
> Faculty Guide: **Dr. G. Rajalakshmi**

</div>

---

## 📖 Table of Contents

- [Why This Exists](#-why-this-exists)
- [How It Works](#-how-it-works)
- [Signal Processing Pipeline](#-signal-processing-pipeline)
- [System Architecture](#-system-architecture)
- [Repository Structure](#-repository-structure)
- [Quick Start](#-quick-start)
- [Hardware BOM](#-hardware-bom)
- [LoRa Configuration](#-lora-configuration)
- [Decision Logic](#-decision-logic)
- [Dependencies](#-dependencies)
- [License](#-license)

---

## 🪲 Why This Exists

The Red Palm Weevil (*Rhynchophorus ferrugineus*) is one of the most destructive invasive pests on the planet. By the time external symptoms (wilting fronds, entry holes, oozing sap) are visible, the internal damage is already catastrophic and the tree is usually unsalvageable.

**RPW Sentinel catches it at the larval stage** — when larvae are actively chewing inside the trunk — using bioacoustic sensing. No chemicals. No drills. No destruction.

| Detection Method | Detects at | Cost | Non-invasive |
|---|---|---|---|
| Visual inspection | Late stage | Low | ✅ |
| Pheromone traps | Adult stage | Medium | ✅ |
| Endoscope probe | Mid stage | High | ❌ |
| X-Ray / CT | Mid stage | Very High | ✅ |
| **RPW Sentinel** | **Early larval stage** | **~₹252/tree** | ✅ |

---

## ⚙️ How It Works

Each sensor node is bonded to a palm tree via a **27mm PZT-5A piezoelectric disc** epoxied to an M6 screw driven into the trunk. This creates direct mechanical coupling to the wood, capturing the micro-vibrations produced by larval chewing — sounds inaudible to the human ear at distance but structurally transmitted through the trunk.

```
                  PALM TRUNK
                ┌───────────────┐
                │               │
                │   🐛 Larva    │  ← chewing vibration
                │   chewing     │
                │               │
                └───────┬───────┘
                        │ vibration travels through wood
                   [M6 screw]
                        │
                  [PZT-5A disc]  ← converts vibration → voltage
                        │
                   [LM358 amp]   ← 20 dB gain
                        │
                  [ESP32 ADC]    ← 12-bit, 8 kHz sample rate
                        │
               [TKEO + Burst Logic]
                        │
                  [Ra-02 LoRa]   ← 866 MHz
                        │
              ~~~~~~~~~~~~~~~~~~~~~
                        │
                  [Gateway ESP32]
                        │
                   NORMAL / SUSPICIOUS / INFESTED
```

The ESP32 runs the full TKEO pipeline locally, classifies the tree every **30 seconds**, and transmits the result wirelessly. No cloud required. No internet required. Works in remote farms.

---

## 🔬 Signal Processing Pipeline

```
Piezo disc (raw vibration)
    │
    ▼
LM358 Op-Amp ──── 20 dB gain ──────────────────────────────────────┐
    │                                                                │
    ▼                                                                │
ESP32 ADC ──── 12-bit resolution @ 8 kHz sample rate               │
    │                                                                │
    ▼                                                           [Analog Chain]
IIR High-Pass Filter
    fc = 200 Hz  │  α = 0.9355
    Removes DC drift + low-freq mechanical noise
    │
    ▼
Teager-Kaiser Energy Operator (TKEO)
    ψ[n] = x[n]² − x[n+1] · x[n−1]
    Amplifies transient energy bursts, suppresses sustained tones
    │
    ▼
Adaptive Threshold
    T = max(mean + 3σ,  P95)
    Recalculated each window — self-calibrating to ambient noise floor
    │
    ▼
Burst Grouping
    Window:           200 ms
    Inter-burst gap:  500 ms minimum
    Min crossings:    ≥ 3 per burst  (rejects single mechanical impulses)
    │
    ▼
Decision (per 30-second window)
    ┌─────────────────┬───────────────┐
    │   Burst Count   │   Decision    │
    ├─────────────────┼───────────────┤
    │        0        │   NORMAL  🟢  │
    │       1–2       │ SUSPICIOUS 🟡 │
    │       ≥ 3       │  INFESTED 🔴  │
    └─────────────────┴───────────────┘
    Note: 3× consecutive SUSPICIOUS → escalates to INFESTED
    │
    ▼
LoRa Packet → Gateway
```

> **Why TKEO?**  
> Unlike FFT-based methods, TKEO is a time-domain operator with near-zero latency and minimal compute overhead. It's specifically sensitive to the amplitude-modulated, impulsive energy signature of insect mandible strikes — making it ideal for embedded deployment on a microcontroller with no FPU.

---

## 🏗️ System Architecture

```
         FIELD DEPLOYMENT (per 4-tree cluster)
┌──────────────────────────────────────────────┐
│                                              │
│  [Node A]   [Node B]   [Node C]   [Node D]  │
│  ESP32+LoRa ESP32+LoRa ESP32+LoRa ESP32+LoRa│
│     🌴          🌴          🌴          🌴   │
│                                              │
│         All transmit on 866 MHz              │
│              SF10 · BW125                    │
└──────────────────────┬───────────────────────┘
                       │ LoRa RF
                       ▼
              ┌─────────────────┐
              │  Gateway Node   │
              │  ESP32 + Ra-02  │
              │  + LED / Buzzer │
              └────────┬────────┘
                       │ Serial / GPIO
                       ▼
              Alert display / logging
              (Serial Monitor, SD, or
               future MQTT expansion)
```

**Network config:**  
- Sync Word `0xF3` — private network, won't collide with public LoRaWAN
- Up to ~5 km range (open field, SF10)
- Each node has a unique `NODE_ID` set at flash time

---

## 📁 Repository Structure

```
rpw-sentinel/
│
├── sensor_node/
│   └── sensor_node.ino       # Core firmware — ESP32 + piezo + Ra-02
│                               TKEO pipeline, burst counting, LoRa TX
│
├── gateway/
│   └── gateway.ino           # Gateway firmware — LoRa RX + alert output
│
├── tools/
│   ├── debug_monitor.ino     # ADC + TKEO live serial printout
│   │                           ⚠️ Flash this FIRST to verify your analog chain
│   └── lora_range_test.ino   # TX/RX range characterisation utility
│
├── hardware/
│   └── wiring.md             # Full pin tables + circuit description
│
├── docs/
│   └── SETUP_GUIDE.md        # IDE setup · library install · flash order · troubleshooting
│
└── README.md
```

---

## 🚀 Quick Start

### Prerequisites
- Arduino IDE 2.x
- Two ESP32-WROOM-32 modules (sensor node + gateway)
- Hardware assembled per `hardware/wiring.md`

### Step-by-step

**1 — Wire the hardware**
```
See hardware/wiring.md for full pin tables.
Key things to get right:
  • Ra-02 runs on 3.3 V ONLY — 5 V will kill it
  • ANT wire: 82 mm for 866 MHz  |  173 mm for 433 MHz
  • LM358 powered from 3.3 V rail
```

**2 — Verify your analog chain first**
```
Flash: tools/debug_monitor.ino
Action: tap the screw head physically
Expect: clear spikes in Serial Monitor output
If nothing: check LM358 wiring and piezo bond
```

**3 — Flash the sensor node**
```cpp
// In sensor_node.ino — set before upload:
#define NODE_ID  1    // unique per node, 1–255
```
```
Upload → sensor_node.ino
```

**4 — Flash the gateway**
```
Second ESP32 → gateway.ino
No changes needed — it listens for all NODE_IDs
```

**5 — Range test before deployment**
```
Flash lora_range_test.ino on both units
Walk the field boundary
Note the max reliable distance → place gateway accordingly
```

**6 — Deploy**
```
Drive M6 screw into trunk at ~30–50 cm height
Epoxy PZT disc to screw head, flat face toward wood
Cable-tie ESP32 enclosure to trunk, weatherproofed
Power from solar + 18650 cell (recommended)
```

Full walkthrough → [`docs/SETUP_GUIDE.md`](docs/SETUP_GUIDE.md)

---

## 🛒 Hardware BOM

*Per sensor node (covers 1 tree). A 4-tree cluster shares one node at ~₹252/tree.*

| Component | Qty | Spec / Note |
|---|---|---|
| ESP32-WROOM-32 | 1 | Any 38-pin module |
| 27mm PZT-5A piezo disc | 1 | Epoxy to M6 screw head, flat face to wood |
| Ra-02 LoRa module | 1 | 433 or 868 MHz variant — **3.3 V only, never 5 V** |
| LM358 op-amp | 1 | Dual-supply; powered from 3.3 V |
| LM393 comparator | 1 | Wake interrupt trigger |
| 10 kΩ resistor | — | See `hardware/wiring.md` |
| 47 kΩ resistor | — | |
| 90 kΩ resistor | — | |
| 100 kΩ resistor | — | |
| 220 Ω resistor | — | LED current limiting |
| 47 nF capacitor | 1 | HPF pole: 10kΩ + 47nF → fc ≈ 200 Hz |
| 3-colour LED (R/G/A) | 1 | Or 3 discrete LEDs (Red/Green/Amber) |
| Passive buzzer | 1 | Gateway alert |
| ANT wire | 1 | 82 mm bare wire for 866 MHz / 173 mm for 433 MHz |
| M6 screw (~50 mm) | 1 | Drive into trunk as acoustic coupler |

> 💡 **Total BOM cost: ~₹1008 per 4-tree node cluster = ~₹252/tree**

---

## 📡 LoRa Configuration

| Parameter | Value | Reason |
|---|---|---|
| Frequency | **866 MHz** | India WPC band-permitted ISM |
| Spreading Factor | **SF10** | ~5 km range, trades speed for link budget |
| Bandwidth | **125 kHz** | Standard narrow-band config |
| Coding Rate | **4/5** | Minimal FEC overhead |
| Sync Word | **0xF3** | Private network — won't collide with LoRaWAN |
| TX Power | **17 dBm** | Max legal for Ra-02 in India |

> ⚠️ **Frequency note:** If deploying outside India, verify local ISM band regulations. Change `LORA_FREQ` in firmware accordingly. Use 173 mm antenna for 433 MHz variant.

---

## 🧠 Decision Logic

### Burst detection thresholds

| Parameter | Value | Reference |
|---|---|---|
| Burst grouping window | 200 ms | Pinhas et al. 2008 — inter-event gap 100–300 ms |
| Minimum inter-burst gap | 500 ms | Pinhas et al. 2008 — rest gap 400–700 ms |
| Minimum crossings per burst | 3 | Empirical — rejects single mechanical impulses |
| Threshold σ multiplier | 3.0 | Tunable via `THRESHOLD_SIGMA` in firmware |

### State machine

```
Per 30-second window:

  bursts == 0  →  🟢 NORMAL
  bursts 1–2   →  🟡 SUSPICIOUS
  bursts ≥ 3   →  🔴 INFESTED

  Special rule:
  3× consecutive SUSPICIOUS windows → escalate to 🔴 INFESTED
  (catches slow/early-stage infestations with low burst rate)
```

### LED status indicators

| LED State | Meaning |
|---|---|
| 🟢 Green solid | NORMAL — no activity detected |
| 🟡 Amber blink | SUSPICIOUS — low burst count, monitor closely |
| 🔴 Red solid + buzzer | INFESTED — immediate action recommended |

---

## 📦 Dependencies

Install via Arduino IDE Board Manager / Library Manager:

| Dependency | Source | Purpose |
|---|---|---|
| **ESP32 board support** | Espressif Systems — Board Manager | ESP32 core |
| **LoRa** by Sandeep Mistry | Library Manager | Ra-02 SPI driver |

No other external libraries required. TKEO, IIR filter, and burst logic are implemented directly in firmware.

---

## 🔧 Tuning & Customisation

| Parameter | Location | Default | Effect |
|---|---|---|---|
| `NODE_ID` | `sensor_node.ino` | `1` | Unique node identifier sent in LoRa packet |
| `THRESHOLD_SIGMA` | `sensor_node.ino` | `3.0` | Higher = fewer false positives, may miss weak signals |
| `BURST_WINDOW_MS` | `sensor_node.ino` | `200` | Burst grouping window duration |
| `MIN_INTER_BURST_MS` | `sensor_node.ino` | `500` | Minimum gap between bursts |
| `MIN_CROSSINGS` | `sensor_node.ino` | `3` | Threshold crossings needed to count as a burst |
| `LORA_FREQ` | Both `.ino` files | `866E6` | LoRa carrier frequency in Hz |
| `LORA_SF` | Both `.ino` files | `10` | Spreading factor (7–12) |

---

## 📄 License

MIT License — see [`LICENSE`](LICENSE) for full text.

Free to use, modify, and deploy. Attribution appreciated but not required.

---

<div align="center">

**Built for the palms. Built for the farmers.**  
*Sathyabama Institute of Science and Technology — ECE Department*

</div>
