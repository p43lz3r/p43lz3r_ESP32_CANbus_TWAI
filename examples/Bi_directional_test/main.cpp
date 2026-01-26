// Copyright 2026 p43lz3r
// Bidirectional test: RX all messages + TX test frame every 1s.
// For Waveshare ESP32-S3-Touch-LCD-4.3B (pins auto-set to 16/15).
// Normal mode → sends ACKs to Pi cansend.

#include <Arduino.h>
#include "waveshare_can.h"

WaveshareCan can(kBoard43b);  // kBoard43b = RX16 / TX15

// Transmit timing & counter
unsigned long last_tx = 0;
constexpr unsigned long kTxInterval = 1000;  // 1 second
uint32_t tx_count = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== WaveshareCAN Bidirectional Test (4.3B) ===");
  Serial.println("  - RX: prints ALL incoming messages");
  Serial.println("  - TX: sends ID 0x321 every 1s (counter + pattern)");
  Serial.println("  - Mode: NORMAL (ACK enabled)\n");

  // Skip expander for pure CAN test.
  // can.InitIoExpander();

  if (!can.Begin(kCan500Kbps)) {
    Serial.println("!!! CAN init FAILED !!! Check wiring, termination, power");
    while (true) {
      delay(1000);
      Serial.println("CAN init failed - halting");
    }
  }

  can.SetListenOnly(false);  // Ensure NORMAL mode (ACKs sent).
  Serial.println("CAN ready – 500 kbps – NORMAL mode (ACK on)\n");
  Serial.println("Try: sudo cansend can0 123#AABBCCDD on Pi → should print");
  Serial.println("Board will TX every second → watch for ID 0x321\n");
}

void loop() {
  // -----------------------------------------------------------
  // 1. Receive & print any message
  // -----------------------------------------------------------
  uint32_t id;
  bool ext;
  bool rtr;
  uint8_t data[8];
  uint8_t len = 0;

  int rx_bytes = can.ReceiveMessage(&id, &ext, data, &len, &rtr);

  if (rx_bytes >= 0) {
    Serial.printf("%lu ms | %s ID:0x%08lX  DLC:%d  %s  ", millis(),
                  ext ? "EXT" : "STD", id, len, rtr ? "RTR" : "DATA");

    if (rtr) {
      Serial.println("(remote request)");
    } else {
      for (uint8_t i = 0; i < len; i++) {
        Serial.printf("%02X ", data[i]);
      }
      Serial.println();
    }
  }

  // -----------------------------------------------------------
  // 2. Transmit test message every 1s
  // -----------------------------------------------------------
  unsigned long now = millis();
  if (now - last_tx >= kTxInterval) {
    last_tx = now;
    tx_count++;

    uint8_t payload[8] = {
        static_cast<uint8_t>(tx_count >> 24),
        static_cast<uint8_t>(tx_count >> 16),
        static_cast<uint8_t>(tx_count >> 8),
        static_cast<uint8_t>(tx_count),
        0xAA,
        0xBB,
        0xCC,
        0xDD  // fixed bytes for easy spotting
    };

    bool tx_ok = can.SendMessage(0x321,   // test ID
                                 false,   // standard frame
                                 payload, 8,
                                 false  // not RTR
    );

    Serial.printf("%lu ms | TX %s   ID:0x321  DLC:8  %08lX AA BB CC DD\n", now,
                  tx_ok ? "OK " : "FAILED", tx_count);
  }

  // Process any alerts (bus-off, errors, recovery…).
  can.ProcessAlerts();

  delay(5);  // Small yield for background tasks.
}