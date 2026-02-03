// CAN Configuration Example
// Demonstrates complete integration of CanConfigManager with WaveshareCAN
// Copyright 2026 p43lz3r

#include <Arduino.h>
#include "waveshare_can.h"
#include "can_config_manager.h"

WaveshareCan can(kBoard43b);
CanConfigManager config;
volatile uint32_t msg_count = 0;

void onMessage(const twai_message_t& msg) {
  msg_count++;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n╔════════════════════════════════════════════════════════╗");
  Serial.println("║ WaveshareCAN Configuration System Demo                ║");
  Serial.println("╚════════════════════════════════════════════════════════╝\n");
  
  // ────────────────────────────────────────────────────────
  // Step 1: Load configuration from NVS
  // ────────────────────────────────────────────────────────
  Serial.println("[Step 1] Loading configuration from NVS...");
  config.LoadFromNVS();
  config.PrintConfig();
  
  // ────────────────────────────────────────────────────────
  // Step 2: Wait for configuration upload (15 seconds for testing)
  // ────────────────────────────────────────────────────────
  Serial.println("[Step 2] Upload window...");
  
  bool new_config = config.WaitForConfig(15000);  // 15s for testing, 5s for production
  
  if (new_config) {
    Serial.println("\n[Step 2] New configuration uploaded!");
    config.PrintConfig();
  } else {
    Serial.println("[Step 2] Using stored configuration");
  }
  
  // ────────────────────────────────────────────────────────
  // Step 3: Apply configuration (starts CAN with bitrate)
  // ────────────────────────────────────────────────────────
  Serial.println("[Step 3] Applying configuration to CAN bus...");
  config.ApplyToCanBus(&can);  // Starts CAN with configured bitrate
  
  // ────────────────────────────────────────────────────────
  // Step 4: Enable RX interrupt
  // ────────────────────────────────────────────────────────
  Serial.println("[Step 4] Enabling RX interrupt...");
  
  if (!can.EnableRxInterrupt(onMessage)) {
    Serial.println("✗ Failed to enable RX interrupt!");
    while(1) delay(1000);
  }
  
  Serial.println("✓ RX interrupt enabled\n");
  
  // ────────────────────────────────────────────────────────
  // Ready for operation
  // ────────────────────────────────────────────────────────
  Serial.println("╔════════════════════════════════════════════════════════╗");
  Serial.println("║ System Ready - Receiving CAN Messages                 ║");
  Serial.println("╚════════════════════════════════════════════════════════╝\n");
  
  Serial.println("Current filter configuration:");
  Serial.printf("  Mode: %s\n", 
                (config.GetMode() == kFilterMonitoring) ? "Monitoring (accept all)" : "Specific (filtered)");
  
  if (config.GetMode() == kFilterSpecific) {
    Serial.printf("  Accepted IDs (%d): ", config.GetIdCount());
    const uint32_t* ids = config.GetIds();
    for (uint8_t i = 0; i < config.GetIdCount(); i++) {
      Serial.printf("0x%X ", ids[i]);
    }
    Serial.println();
  }
  
  Serial.println("\nSend CAN messages from Raspberry Pi to test filtering.");
  Serial.println("Example: cansend can0 100#0102030405060708\n");
}

void loop() {
  static unsigned long last_print = 0;
  
  // Print statistics every 2 seconds
  if (millis() - last_print > 2000) {
    Serial.printf("[Runtime] Messages: %lu, Queued: %d, Dropped: %lu\n",
                  msg_count,
                  can.QueuedMessages(),
                  can.GetDroppedRxCount());
    last_print = millis();
  }
  
  // Process queued messages
  while (can.QueuedMessages() > 0) {
    uint32_t id;
    uint8_t data[8], len;
    bool extended;
    
    if (can.ReceiveFromQueue(&id, &extended, data, &len) >= 0) {
      Serial.printf("  RX: ID=0x%X %s Len=%d Data=", 
                    id, extended ? "EXT" : "STD", len);
      for (uint8_t i = 0; i < len; i++) {
        Serial.printf("%02X ", data[i]);
      }
      Serial.println();
    }
  }
  
  delay(10);
}

/*
 * USAGE INSTRUCTIONS:
 * 
 * 1. Flash this sketch to ESP32
 * 
 * 2. Open Serial Monitor (115200 baud)
 * 
 * 3. Within 15 seconds, upload config using Python tool:
 *    python can_config.py --port /dev/ttyUSB0 --mode specific --ids 0x100 0x200 0x300
 * 
 * 4. Or wait for timeout to use stored config
 * 
 * 5. Send test messages from Raspberry Pi:
 *    cansend can0 100#0102030405060708
 *    cansend can0 200#1112131415161718
 *    cansend can0 FFF#2122232425262728
 * 
 * 6. Observe filtering in action:
 *    - Monitoring mode: All messages received
 *    - Specific mode: Only configured IDs received
 * 
 * 7. To change config at runtime:
 *    - Press reset button on ESP32
 *    - Within 15 seconds, send new config via Python tool
 *    - New config is saved to NVS and applied immediately
 */