// Copyright 2026 p43lz3r
#ifndef PROJECT_WAVESHARE_CAN_H_
#define PROJECT_WAVESHARE_CAN_H_

#include <Arduino.h>
#include <memory>
#include "driver/twai.h"

// Forward declaration for the IO expander
namespace esp_expander {
class CH422G;
}

// Board variants
enum BoardType {
  kBoard43b,  // ESP32-S3-Touch-LCD-4.3B (RX=16, TX=15)
  kBoard7b    // 7-inch version (RX=19, TX=20)
};

// I2C defaults - converted to k-prefixed constexpr
constexpr uint8_t kCanExpanderI2cAddr = 0x20;
constexpr int kCanExpanderI2cSdaPin = 8;
constexpr int kCanExpanderI2cSclPin = 9;

// Expander pin assignments
constexpr uint8_t kExpIoTpRst = 1;   // EXIO1 - Touch reset
constexpr uint8_t kExpIoLcdBl = 2;   // EXIO2 - Backlight (DISP)
constexpr uint8_t kExpIoSdCs = 4;    // EXIO4 - SD card CS (active low)

// CAN speed presets
constexpr twai_timing_config_t kCan5Kbps = TWAI_TIMING_CONFIG_5KBITS();
constexpr twai_timing_config_t kCan10Kbps = TWAI_TIMING_CONFIG_10KBITS();
constexpr twai_timing_config_t kCan20Kbps = TWAI_TIMING_CONFIG_20KBITS();
constexpr twai_timing_config_t kCan50Kbps = TWAI_TIMING_CONFIG_50KBITS();
constexpr twai_timing_config_t kCan100Kbps = TWAI_TIMING_CONFIG_100KBITS();
constexpr twai_timing_config_t kCan125Kbps = TWAI_TIMING_CONFIG_125KBITS();
constexpr twai_timing_config_t kCan250Kbps = TWAI_TIMING_CONFIG_250KBITS();
constexpr twai_timing_config_t kCan500Kbps = TWAI_TIMING_CONFIG_500KBITS();
constexpr twai_timing_config_t kCan800Kbps = TWAI_TIMING_CONFIG_800KBITS();
constexpr twai_timing_config_t kCan1000Kbps = TWAI_TIMING_CONFIG_1MBITS();

class WaveshareCan {
 public:
  WaveshareCan(BoardType board = kBoard43b, int rx_pin = -1, int tx_pin = -1);
  ~WaveshareCan();

  WaveshareCan(const WaveshareCan&) = delete;
  WaveshareCan& operator=(const WaveshareCan&) = delete;

  // Initialize IO expander (optional for pure CAN usage)
  bool InitIoExpander(int scl_pin = kCanExpanderI2cSclPin,
                      int sda_pin = kCanExpanderI2cSdaPin,
                      int addr = kCanExpanderI2cAddr);

  // Start CAN with selected speed (default 500 kbps)
  bool Begin(twai_timing_config_t speed_config = kCan500Kbps);

  // Stop and uninstall driver
  void End();

  // Number of received messages waiting
  int Available();

  // Send message - full version
  bool SendMessage(uint32_t id, bool extended, uint8_t* data, uint8_t length,
                   bool rtr = false);

  // Simple version (standard ID, no RTR)
  bool SendMessage(uint32_t id, uint8_t* data, uint8_t length);

  // Receive one message (non-blocking, returns bytes read or -1)
  int ReceiveMessage(uint32_t* id, bool* extended, uint8_t* data,
                     uint8_t* length, bool* rtr = nullptr);

  // Set acceptance filter (re-initializes driver)
  void Filter(uint32_t id, uint32_t mask = 0);

  // Get TWAI status
  bool GetStatus(twai_status_info_t* status);

  // Switch between normal and listen-only mode
  bool SetListenOnly(bool listen_only);

  // Check & process alerts (call regularly)
  bool ProcessAlerts(uint32_t* alerts_triggered = nullptr);

  // Set callback for alerts
  void OnAlert(void (*callback)(uint32_t));

 private:
  void HandleAlerts(uint32_t alerts);

  BoardType board_type_;
  int rx_pin_;
  int tx_pin_;
  bool initialized_;
  bool expander_initialized_;
  bool listen_only_;
  std::unique_ptr<esp_expander::CH422G> expander_;  // Smart pointer
  void (*alert_callback_)(uint32_t);

  twai_timing_config_t timing_config_;
  twai_filter_config_t filter_config_;
};

#endif  // PROJECT_WAVESHARE_CAN_H_