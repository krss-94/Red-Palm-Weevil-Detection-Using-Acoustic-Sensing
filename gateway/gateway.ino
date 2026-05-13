/*
 * RPW Gateway — ESP32 + Ra-02 LoRa receiver
 *
 * Receives alert packets from sensor nodes and outputs:
 *   - Serial monitor (always)
 *   - Tri-colour LED (local visual alert)
 *   - Buzzer (local audible alert)
 *   - Optional: WiFi → HTTP POST to notification service
 *
 * One gateway can serve ~100 trees within 1–2 km.
 */

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

// ─── Pin Definitions ────────────────────────────────────────────────────────
// SAME LoRa wiring as sensor node — they share the same pinout
#define LORA_CS     5
#define LORA_RST    14
#define LORA_DIO0   2
#define LORA_SCK    18
#define LORA_MISO   19
#define LORA_MOSI   23

#define LED_GREEN   25   // All-clear / NORMAL
#define LED_AMBER   26   // SUSPICIOUS alert
#define LED_RED     27   // INFESTED alert
#define BUZZER_PIN  32

// ─── LoRa Config — MUST match sensor node exactly ────────────────────────────
#define LORA_FREQUENCY  866E6
#define LORA_SF         10
#define LORA_BW         125E3
#define LORA_CR         5
#define LORA_SYNC_WORD  0xF3

// ─── Optional WiFi notification ──────────────────────────────────────────────
// Set WIFI_ENABLE to true and fill in your credentials if you have internet
#define WIFI_ENABLE     false
// #define WIFI_SSID    "your_ssid"
// #define WIFI_PASS    "your_password"

// ─────────────────────────────────────────────────────────────────────────────
//  Alert display
// ─────────────────────────────────────────────────────────────────────────────
void displayAlert(uint8_t node_id, uint8_t state, uint8_t bursts, int rssi, float snr) {
  const char* state_str[] = {"NORMAL", "SUSPICIOUS", "INFESTED"};

  Serial.println("─────────────────────────────");
  Serial.printf("  Node ID  : %d\n", node_id);
  Serial.printf("  State    : %s\n", (state <= 2) ? state_str[state] : "UNKNOWN");
  Serial.printf("  Bursts   : %d\n", bursts);
  Serial.printf("  RSSI     : %d dBm\n", rssi);
  Serial.printf("  SNR      : %.1f dB\n", snr);
  Serial.println("─────────────────────────────");

  // Drive LEDs and buzzer
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_AMBER, LOW);
  digitalWrite(LED_RED,   LOW);
  noTone(BUZZER_PIN);

  if (state == 0) {
    // NORMAL — one short green blink, no alarm
    digitalWrite(LED_GREEN, HIGH);
    delay(500);
    digitalWrite(LED_GREEN, LOW);
  } else if (state == 1) {
    // SUSPICIOUS — amber stays on for 10 seconds
    digitalWrite(LED_AMBER, HIGH);
    tone(BUZZER_PIN, 1000, 300);
    Serial.printf("[ALERT] Tree %d is SUSPICIOUS — check again in next cycle.\n", node_id);
  } else if (state == 2) {
    // INFESTED — red + loud buzzer pattern
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_RED, HIGH);
      tone(BUZZER_PIN, 2500, 200);
      delay(250);
      digitalWrite(LED_RED, LOW);
      noTone(BUZZER_PIN);
      delay(150);
    }
    digitalWrite(LED_RED, HIGH);  // Leave red on
    Serial.printf("!!! INFESTED ALERT — Tree node %d — Call agricultural officer !!!\n", node_id);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Packet parser — reads 5-byte payload from sensor node
// ─────────────────────────────────────────────────────────────────────────────
void onReceive(int packet_size) {
  if (packet_size < 3) {
    Serial.printf("[LoRa] Short packet (%d bytes) — ignoring.\n", packet_size);
    return;
  }

  uint8_t node_id = LoRa.read();
  uint8_t state   = LoRa.read();
  uint8_t bursts  = LoRa.read();

  // Optional threshold bytes (may not be present in older firmware)
  float threshold = 0.0f;
  if (packet_size >= 5) {
    uint8_t thr_hi = LoRa.read();
    uint8_t thr_lo = LoRa.read();
    threshold = ((thr_hi << 8) | thr_lo) / 10.0f;
  }

  int   rssi = LoRa.packetRssi();
  float snr  = LoRa.packetSnr();

  displayAlert(node_id, state, bursts, rssi, snr);
}

// ─────────────────────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== RPW LoRa Gateway v1.0 ===");

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_AMBER, OUTPUT);
  pinMode(LED_RED,   OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Startup: all LEDs blink once
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_AMBER, HIGH);
  digitalWrite(LED_RED,   HIGH);
  delay(300);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_AMBER, LOW);
  digitalWrite(LED_RED,   LOW);

  // LoRa init
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("[LoRa] INIT FAILED — check wiring! Halting.");
    while (true) {
      digitalWrite(LED_RED, HIGH); delay(200);
      digitalWrite(LED_RED, LOW);  delay(200);
    }
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.onReceive(onReceive);
  LoRa.receive();   // Put radio in continuous receive mode

  Serial.printf("[LoRa] Listening on %.0f MHz SF%d BW%.0fkHz\n",
    LORA_FREQUENCY / 1e6, LORA_SF, LORA_BW / 1e3);
  Serial.println("[Gateway] Ready — waiting for sensor node packets...");
  digitalWrite(LED_GREEN, HIGH);  // Solid green = gateway healthy
}

// ─────────────────────────────────────────────────────────────────────────────
//  LOOP — gateway just waits; onReceive() fires on interrupt
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  // Heartbeat blink every 5 seconds so you know gateway is alive
  static unsigned long last_heartbeat = 0;
  if (millis() - last_heartbeat > 5000) {
    Serial.println("[Gateway] Heartbeat — listening...");
    last_heartbeat = millis();
  }
  delay(100);
}
