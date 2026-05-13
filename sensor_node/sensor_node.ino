/*
 * RPW Sensor Node — ESP32 + Piezo + TKEO + LoRa
 * Red Palm Weevil Early Detection System
 *
 * Hardware:
 *   - ESP32-WROOM-32
 *   - 27mm PZT-5A piezo disc (via LM358 amplifier)
 *   - LM393 comparator (wake interrupt)
 *   - Ra-02 LoRa module (SPI)
 *   - Tri-colour LED + buzzer
 *
 * Pin assignments are defined below — match your wiring exactly.
 */

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>          // Install: "LoRa" by Sandeep Mistry via Library Manager
#include <driver/adc.h>
#include <esp_sleep.h>
#include <math.h>

// ─── Pin Definitions ────────────────────────────────────────────────────────
#define ADC_INPUT_PIN     34   // GPIO34  — Piezo signal (ADC1 CH6)
#define INTERRUPT_PIN     35   // GPIO35  — LM393 comparator output (wake)
#define LED_GREEN         25   // GPIO25  — NORMAL state
#define LED_AMBER         26   // GPIO26  — SUSPICIOUS state
#define LED_RED           27   // GPIO27  — INFESTED state
#define BUZZER_PIN        32   // GPIO32  — Piezo buzzer

// LoRa (Ra-02) SPI pins
#define LORA_CS           5    // NSS / Chip Select
#define LORA_RST          14   // RESET
#define LORA_DIO0         2    // DIO0 (TX/RX done IRQ)
#define LORA_SCK          18   // SPI Clock
#define LORA_MISO         19   // SPI MISO
#define LORA_MOSI         23   // SPI MOSI

// ─── Sampling Parameters ─────────────────────────────────────────────────────
#define SAMPLE_RATE_HZ    8000          // 8 kSPS
#define WINDOW_SECONDS    30
#define WINDOW_SAMPLES    (SAMPLE_RATE_HZ * WINDOW_SECONDS)  // 240,000

// ─── DSP Parameters ──────────────────────────────────────────────────────────
// HPF: first-order IIR, fc = 200 Hz at fs = 8000 Hz
// alpha = 1 / (1 + 2*pi*fc/fs) ≈ 0.9355 (bilinear approx)
#define HPF_ALPHA         0.9355f

// Adaptive threshold: T = max(mean + N*sigma, P95)
#define THRESHOLD_SIGMA   3.0f

// Burst grouping: two crossings within this ms belong to the same burst
#define BURST_GROUP_MS    200
// Minimum crossings per valid burst (filters out single footstep impacts)
#define MIN_CROSSINGS_PER_BURST  3
// Minimum gap between bursts (ms) — reflects larval rest period
#define MIN_INTER_BURST_MS  500

// ─── Decision Logic ───────────────────────────────────────────────────────────
#define BURSTS_SUSPICIOUS  1   // ≥1 burst → SUSPICIOUS
#define BURSTS_INFESTED    3   // ≥3 bursts → INFESTED
#define PERSISTENCE_CYCLES 3   // 3× SUSPICIOUS in a row → escalate to INFESTED

// ─── LoRa Config ─────────────────────────────────────────────────────────────
#define LORA_FREQUENCY    866E6   // 866 MHz — India WPC permitted band
#define LORA_SF           10      // Spreading Factor 10 (range vs speed trade-off)
#define LORA_BW           125E3   // 125 kHz bandwidth
#define LORA_CR           5       // Coding rate 4/5
#define NODE_ID           1       // Change this (0–255) for each sensor node

// ─── Deep Sleep ──────────────────────────────────────────────────────────────
// Wake every 5 minutes (30s window + 4.5 min sleep)
#define SLEEP_DURATION_US  (270ULL * 1000000ULL)  // 4.5 minutes in microseconds

// ─── State Machine ───────────────────────────────────────────────────────────
enum DetectionState { NORMAL = 0, SUSPICIOUS = 1, INFESTED = 2 };

// ─── Globals ─────────────────────────────────────────────────────────────────
static float  hpf_prev_in  = 0.0f;
static float  hpf_prev_out = 0.0f;
static int    consecutive_suspicious = 0;
static bool   lora_available = false;

// ─────────────────────────────────────────────────────────────────────────────
//  DSP Functions
// ─────────────────────────────────────────────────────────────────────────────

// First-order IIR high-pass filter
// y[n] = alpha * (y[n-1] + x[n] - x[n-1])
inline float hpf(float x_new) {
  float y = HPF_ALPHA * (hpf_prev_out + x_new - hpf_prev_in);
  hpf_prev_in  = x_new;
  hpf_prev_out = y;
  return y;
}

// Teager-Kaiser Energy Operator
// psi[n] = x[n]^2 - x[n+1] * x[n-1]
// Called with x_prev1 = x[n-1], x_curr = x[n], x_next = x[n+1]
inline float tkeo(float x_prev1, float x_curr, float x_next) {
  return (x_curr * x_curr) - (x_next * x_prev1);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sample collection + TKEO computation in one pass (memory-efficient)
//  Instead of storing 240k raw samples, we process on-the-fly and
//  store only the TKEO energy values.
// ─────────────────────────────────────────────────────────────────────────────
void collectAndProcessWindow(float* tkeo_out, uint32_t n_samples) {
  // We need x[n-1], x[n], x[n+1] for TKEO — use a 3-sample ring
  float ring[3] = {0, 0, 0};
  uint32_t sample_interval_us = 1000000UL / SAMPLE_RATE_HZ;  // 125 µs at 8kHz
  uint32_t t_next = micros();

  // Prime the ring with two samples before processing
  for (int i = 0; i < 2; i++) {
    while (micros() < t_next) { /* busy-wait */ }
    t_next += sample_interval_us;
    float raw = (float)analogRead(ADC_INPUT_PIN) - 2048.0f;  // centre on zero
    ring[i] = hpf(raw);
  }

  for (uint32_t n = 0; n < n_samples; n++) {
    while (micros() < t_next) { /* busy-wait */ }
    t_next += sample_interval_us;

    float raw = (float)analogRead(ADC_INPUT_PIN) - 2048.0f;
    float x_new = hpf(raw);

    // Shift ring: ring[0]=x[n-1], ring[1]=x[n], ring[2]=x[n+1]
    ring[0] = ring[1];
    ring[1] = ring[2];
    ring[2] = x_new;

    // Compute TKEO for ring[1] (the "current" sample)
    float e = tkeo(ring[0], ring[1], ring[2]);
    tkeo_out[n] = (e < 0.0f) ? 0.0f : e;  // TKEO can be slightly negative due to noise; clamp
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Adaptive threshold: T = max(mean + 3σ, P95)
// ─────────────────────────────────────────────────────────────────────────────
float computeAdaptiveThreshold(const float* buf, uint32_t n) {
  // Compute mean and variance in one pass (Welford's method)
  double mean = 0.0, M2 = 0.0;
  for (uint32_t i = 0; i < n; i++) {
    double delta = buf[i] - mean;
    mean += delta / (i + 1);
    M2   += delta * (buf[i] - mean);
  }
  float sigma = (n > 1) ? sqrtf((float)(M2 / (n - 1))) : 0.0f;
  float t_gaussian = (float)mean + THRESHOLD_SIGMA * sigma;

  // P95 via partial sort of a 2048-sample subset (avoid sorting 240k items)
  const uint32_t SUBSET = 2048;
  float subset[SUBSET];
  uint32_t step = n / SUBSET;
  for (uint32_t i = 0; i < SUBSET; i++) subset[i] = buf[i * step];

  // Simple selection for P95 index
  uint32_t p95_idx = (uint32_t)(0.95f * SUBSET);
  // Partial nth_element via quick-select (simple insertion sort on small array is fine)
  for (uint32_t i = 0; i <= p95_idx; i++) {
    uint32_t min_j = i;
    for (uint32_t j = i + 1; j < SUBSET; j++) {
      if (subset[j] < subset[min_j]) min_j = j;
    }
    float tmp = subset[i]; subset[i] = subset[min_j]; subset[min_j] = tmp;
  }
  float t_p95 = subset[p95_idx];

  return (t_gaussian > t_p95) ? t_gaussian : t_p95;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Burst counting
// ─────────────────────────────────────────────────────────────────────────────
int countBursts(const float* tkeo_buf, uint32_t n, float threshold) {
  int   burst_count     = 0;
  int   crossing_count  = 0;
  bool  in_burst        = false;
  uint32_t burst_start_sample  = 0;
  uint32_t last_crossing_sample = 0;
  uint32_t last_burst_end_sample = 0;

  uint32_t group_samples     = (uint32_t)((BURST_GROUP_MS / 1000.0f) * SAMPLE_RATE_HZ);
  uint32_t inter_burst_samples = (uint32_t)((MIN_INTER_BURST_MS / 1000.0f) * SAMPLE_RATE_HZ);

  for (uint32_t i = 0; i < n; i++) {
    if (tkeo_buf[i] > threshold) {
      // Threshold crossing detected
      if (!in_burst) {
        // Check minimum inter-burst gap from last burst end
        if (burst_count == 0 || (i - last_burst_end_sample) >= inter_burst_samples) {
          in_burst           = true;
          burst_start_sample = i;
          crossing_count     = 1;
        }
      } else {
        // Check if still within grouping window
        if ((i - last_crossing_sample) <= group_samples) {
          crossing_count++;
        } else {
          // Gap exceeded — close current burst and start fresh
          if (crossing_count >= MIN_CROSSINGS_PER_BURST) {
            burst_count++;
            last_burst_end_sample = last_crossing_sample;
          }
          in_burst       = false;
          crossing_count = 0;
        }
      }
      last_crossing_sample = i;
    } else {
      // Below threshold — check if burst window has expired
      if (in_burst && (i - last_crossing_sample) > group_samples) {
        if (crossing_count >= MIN_CROSSINGS_PER_BURST) {
          burst_count++;
          last_burst_end_sample = last_crossing_sample;
        }
        in_burst       = false;
        crossing_count = 0;
      }
    }
  }

  // Handle burst that reaches end of window
  if (in_burst && crossing_count >= MIN_CROSSINGS_PER_BURST) {
    burst_count++;
  }

  return burst_count;
}

// ─────────────────────────────────────────────────────────────────────────────
//  LED + Buzzer output
// ─────────────────────────────────────────────────────────────────────────────
void setLED(DetectionState state) {
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_AMBER, LOW);
  digitalWrite(LED_RED,   LOW);

  switch (state) {
    case NORMAL:     digitalWrite(LED_GREEN, HIGH); break;
    case SUSPICIOUS: digitalWrite(LED_AMBER, HIGH); break;
    case INFESTED:
      digitalWrite(LED_RED, HIGH);
      // Three short beeps
      for (int i = 0; i < 3; i++) {
        tone(BUZZER_PIN, 2000, 200);
        delay(300);
      }
      break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  LoRa transmission
// ─────────────────────────────────────────────────────────────────────────────
void loraTransmit(DetectionState state, int burst_count, float threshold) {
  if (!lora_available) return;

  LoRa.beginPacket();
  LoRa.write((uint8_t)NODE_ID);
  LoRa.write((uint8_t)state);
  LoRa.write((uint8_t)min(burst_count, 255));

  // Pack threshold as 2-byte fixed point (threshold * 10, capped at 65535)
  uint16_t thr_packed = (uint16_t)min((int)(threshold * 10.0f), 65535);
  LoRa.write((uint8_t)(thr_packed >> 8));
  LoRa.write((uint8_t)(thr_packed & 0xFF));

  LoRa.endPacket(true);  // true = async (non-blocking)
  Serial.printf("[LoRa] Sent — Node:%d State:%d Bursts:%d\n", NODE_ID, state, burst_count);
}

// ─────────────────────────────────────────────────────────────────────────────
//  LoRa initialisation
// ─────────────────────────────────────────────────────────────────────────────
bool initLoRa() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[LoRa] Init FAILED — check wiring. Continuing without LoRa.");
    return false;
  }
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setSyncWord(0xF3);          // Private network sync word
  LoRa.setTxPower(17);             // 17 dBm (Ra-02 max is 20, stay conservative)
  Serial.printf("[LoRa] Ready — %.0f MHz SF%d BW%.0fkHz\n",
    LORA_FREQUENCY / 1e6, LORA_SF, LORA_BW / 1e3);
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== RPW Sensor Node v1.0 ===");

  // GPIO setup
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_AMBER, OUTPUT);
  pinMode(LED_RED,   OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // ADC setup — 12-bit, 11dB attenuation (0.1V–3.1V input range)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  analogSetPinAttenuation(ADC_INPUT_PIN, ADC_11db);

  // LoRa init (non-fatal — system works without it)
  lora_available = initLoRa();

  // Startup blink
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_GREEN, HIGH); delay(100);
    digitalWrite(LED_GREEN, LOW);  delay(100);
  }

  Serial.println("[Sensor] Starting 30-second measurement window...");
  Serial.printf("[Sensor] Samples to collect: %u\n", WINDOW_SAMPLES);
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP — runs once per wake cycle, then sleeps
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  // Allocate TKEO buffer in PSRAM if available, else heap
  float* tkeo_buf = (float*)malloc(WINDOW_SAMPLES * sizeof(float));
  if (!tkeo_buf) {
    Serial.println("[ERROR] malloc failed for TKEO buffer! Rebooting.");
    delay(1000);
    ESP.restart();
  }

  // Step 1: Collect samples and compute TKEO
  hpf_prev_in = 0; hpf_prev_out = 0;  // Reset HPF state each window
  collectAndProcessWindow(tkeo_buf, WINDOW_SAMPLES);
  Serial.println("[DSP] Window collected and TKEO computed.");

  // Step 2: Compute adaptive threshold
  float threshold = computeAdaptiveThreshold(tkeo_buf, WINDOW_SAMPLES);
  Serial.printf("[DSP] Adaptive threshold: %.2f\n", threshold);

  // Step 3: Count bursts
  int bursts = countBursts(tkeo_buf, WINDOW_SAMPLES, threshold);
  Serial.printf("[DSP] Bursts detected: %d\n", bursts);
  free(tkeo_buf);

  // Step 4: Determine state
  DetectionState state;
  if (bursts == 0) {
    state = NORMAL;
    consecutive_suspicious = 0;
  } else if (bursts < BURSTS_INFESTED) {
    state = SUSPICIOUS;
    consecutive_suspicious++;
    // Persistence escalation: 3 consecutive SUSPICIOUS → INFESTED
    if (consecutive_suspicious >= PERSISTENCE_CYCLES) {
      state = INFESTED;
      Serial.println("[Decision] Persistence escalation: SUSPICIOUS×3 → INFESTED");
    }
  } else {
    state = INFESTED;
    consecutive_suspicious = 0;
  }

  const char* state_names[] = {"NORMAL", "SUSPICIOUS", "INFESTED"};
  Serial.printf("[Decision] State: %s | Consecutive suspicious: %d\n",
    state_names[state], consecutive_suspicious);

  // Step 5: Output
  setLED(state);
  loraTransmit(state, bursts, threshold);

  // Step 6: Sleep
  Serial.printf("[Power] Going to sleep for %.1f minutes...\n",
    SLEEP_DURATION_US / 60000000.0f);
  Serial.flush();
  delay(200);

  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);
  esp_light_sleep_start();

  // After wake-up, loop() runs again automatically
  Serial.println("[Power] Woke up — starting next window.");
}
