// CAN Configuration Manager
// Version: 2.2.0
// Date: 02.02.2026 19:00
// Copyright 2026 p43lz3r
//
// Manages CAN filter configuration with NVS persistence and JSON protocol.
// Supports runtime configuration via Serial interface with Python CLI tool.
//
// Features:
// - NVS flash storage (28-byte blob)
// - JSON protocol parser (ArduinoJson)
// - Upload window with configurable timeout
// - Complete validation (ID range, duplicates)
// - Integration with WaveshareCAN library

#ifndef CAN_CONFIG_MANAGER_H_
#define CAN_CONFIG_MANAGER_H_

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "waveshare_can.h"

class CanConfigManager {
 public:
  CanConfigManager();
  ~CanConfigManager();

  // Boot sequence
  void LoadFromNVS();
  bool WaitForConfig(uint32_t timeout_ms = 5000);
  void ApplyToCanBus(WaveshareCan* can);

  // Persistence
  void SaveToNVS();
  void ClearNVS();

  // Query
  FilterMode GetMode() const { return mode_; }
  uint8_t GetIdCount() const { return id_count_; }
  const uint32_t* GetIds() const { return ids_; }
  bool IsExtended() const { return extended_; }

  // Status/Debug
  void PrintConfig() const;
  String GetConfigJson() const;

 private:
  FilterMode mode_;
  uint32_t ids_[5];
  uint8_t id_count_;
  bool extended_;

  Preferences nvs_;

  bool ParseJsonConfig(const String& json);
  bool ValidateConfig();
  void SetDefaults();
  
  // Validation constants
  static constexpr uint32_t kMaxStandardId = 0x7FF;      // 11-bit: 2047
  static constexpr uint32_t kMaxExtendedId = 0x1FFFFFFF; // 29-bit: 536870911
  static constexpr uint8_t kMaxIdCount = 5;
};

#endif  // CAN_CONFIG_MANAGER_H_