/*
 * LoRa Range Test — Flash this to BOTH ESP32s to test LoRa link quality
 *
 * Transmitter: uncomment #define TX_MODE
 * Receiver:    leave TX_MODE commented out
 *
 * Walk the TX unit away from RX. Watch Serial on the RX for RSSI values.
 * Target: RSSI > -115 dBm for reliable link.
 */

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>

// ─── Uncomment for transmitter, leave commented for receiver ─────────────────
// #define TX_MODE

#define LORA_CS         5
#define LORA_RST        14
#define LORA_DIO0       2
#define LORA_SCK        18
#define LORA_MISO       19
#define LORA_MOSI       23
#define LED_GREEN       25

#define LORA_FREQUENCY  866E6
#define LORA_SF         10
#define LORA_BW         125E3

uint32_t tx_count = 0;
uint32_t rx_count = 0;
uint32_t rx_good  = 0;  // RSSI better than -115 dBm

void setup() {
  Serial.begin(115200);
  pinMode(LED_GREEN, OUTPUT);
  delay(500);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init FAILED"); while (true);
  }
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setSyncWord(0xF3);
  LoRa.setTxPower(17);

#ifdef TX_MODE
  Serial.println("=== LoRa Range Test — TRANSMITTER ===");
  Serial.println("Sending 1 packet per second...");
#else
  Serial.println("=== LoRa Range Test — RECEIVER ===");
  Serial.println("Packet#\tRSSI(dBm)\tSNR(dB)\tLink");
  LoRa.receive();
#endif
}

void loop() {
#ifdef TX_MODE
  tx_count++;
  LoRa.beginPacket();
  LoRa.print("RPW_RANGE_TEST:");
  LoRa.print(tx_count);
  LoRa.endPacket(true);
  Serial.printf("TX #%lu\n", tx_count);
  digitalWrite(LED_GREEN, HIGH); delay(50);
  digitalWrite(LED_GREEN, LOW);
  delay(950);

#else
  // Receiver
  int pkt = LoRa.parsePacket();
  if (pkt > 0) {
    String msg = "";
    while (LoRa.available()) msg += (char)LoRa.read();
    int   rssi = LoRa.packetRssi();
    float snr  = LoRa.packetSnr();
    rx_count++;
    if (rssi > -115) rx_good++;

    const char* link = (rssi > -100) ? "EXCELLENT" :
                       (rssi > -110) ? "GOOD" :
                       (rssi > -115) ? "MARGINAL" : "POOR";
    Serial.printf("%lu\t%d\t\t%.1f\t%s\n", rx_count, rssi, snr, link);
    Serial.printf("   Msg: %s\n", msg.c_str());
    Serial.printf("   Good link rate: %lu/%lu (%.0f%%)\n",
      rx_good, rx_count, 100.0f * rx_good / rx_count);

    digitalWrite(LED_GREEN, HIGH); delay(80);
    digitalWrite(LED_GREEN, LOW);
  }
#endif
}
