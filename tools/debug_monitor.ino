/*
 * RPW Debug Monitor — run this on a THIRD ESP32 (or just use Serial Monitor)
 *
 * This sketch does nothing except print raw ADC values and TKEO output
 * so you can verify your hardware is working BEFORE running the full detection.
 *
 * Flash this first. Tap the screw. See spikes. Then flash sensor_node.ino.
 */

#include <Arduino.h>
#include <math.h>

#define ADC_INPUT_PIN   34
#define SAMPLE_RATE_HZ  8000
#define HPF_ALPHA       0.9355f

float hpf_prev_in = 0, hpf_prev_out = 0;

float hpf(float x) {
  float y = HPF_ALPHA * (hpf_prev_out + x - hpf_prev_in);
  hpf_prev_in = x; hpf_prev_out = y;
  return y;
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  analogSetPinAttenuation(ADC_INPUT_PIN, ADC_11db);
  Serial.println("=== RPW Debug Monitor ===");
  Serial.println("RAW_ADC\tHPF\tTKEO");
  delay(500);
}

float x_prev = 0, x_curr = 0;
unsigned long t_next = 0;
uint32_t interval_us = 1000000UL / SAMPLE_RATE_HZ;

void loop() {
  if (micros() >= t_next) {
    t_next += interval_us;

    float raw = (float)analogRead(ADC_INPUT_PIN) - 2048.0f;
    float xf  = hpf(raw);

    float tkeo_val = x_curr * x_curr - xf * x_prev;
    if (tkeo_val < 0) tkeo_val = 0;

    x_prev = x_curr;
    x_curr = xf;

    // Print every 8th sample to keep Serial from overflowing (= 1kHz output)
    static int skip = 0;
    if (++skip >= 8) {
      skip = 0;
      Serial.printf("%.1f\t%.1f\t%.2f\n", raw + 2048.0f, xf, tkeo_val);
    }
  }
}
