// CAN Configuration Manager - Implementation
// Version: 2.3.0
// Date: 03.02.2026 20:00
// Copyright 2026 p43lz3r

#include "can_config_manager.h"

CanConfigManager::CanConfigManager()
    : mode_(kFilterMonitoring),
      id_count_(0),
      extended_(false),
      bitrate_(kDefaultBitrate) {
  // Initialize IDs array
  for (uint8_t i = 0; i < 5; i++) {
    ids_[i] = 0;
  }
}

CanConfigManager::~CanConfigManager() {
  nvs_.end();
}

void CanConfigManager::SetDefaults() {
  mode_ = kFilterMonitoring;
  id_count_ = 0;
  extended_ = false;
  bitrate_ = kDefaultBitrate;
  
  for (uint8_t i = 0; i < 5; i++) {
    ids_[i] = 0;
  }
}

void CanConfigManager::LoadFromNVS() {
  nvs_.begin("can_config", false);
  
  if (!nvs_.isKey("config")) {
    Serial.println("[NVS] No configuration found - using defaults");
    SetDefaults();
    return;
  }
  
  // Load blob (32 bytes - updated from 28 bytes in v2.2.0)
  uint8_t buffer[32];
  size_t len = nvs_.getBytes("config", buffer, 32);
  
  if (len != 32) {
    Serial.println("[NVS] Invalid config size - using defaults");
    SetDefaults();
    return;
  }
  
  // Parse blob structure:
  // [0]     = mode (0=monitoring, 1=specific)
  // [1]     = id_count (1-5)
  // [2-21]  = ids[5] (5 x uint32_t = 20 bytes)
  // [22]    = extended (0=false, 1=true)
  // [23-26] = bitrate (uint32_t, little-endian)
  // [27-31] = reserved
  
  mode_ = (buffer[0] == 0) ? kFilterMonitoring : kFilterSpecific;
  id_count_ = buffer[1];
  
  // Read IDs (little-endian)
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t offset = 2 + (i * 4);
    ids_[i] = buffer[offset] | 
              (buffer[offset + 1] << 8) |
              (buffer[offset + 2] << 16) |
              (buffer[offset + 3] << 24);
  }
  
  extended_ = (buffer[22] == 1);
  
  // Read bitrate (little-endian)
  bitrate_ = buffer[23] | 
             (buffer[24] << 8) |
             (buffer[25] << 16) |
             (buffer[26] << 24);
  
  // Validate bitrate - must be one of supported values
  if (bitrate_ != kBitrate125k && bitrate_ != kBitrate250k && 
      bitrate_ != kBitrate500k && bitrate_ != kBitrate1000k) {
    Serial.printf("[NVS] Invalid bitrate %lu - using default %lu\n", bitrate_, kDefaultBitrate);
    bitrate_ = kDefaultBitrate;
  }
  
  Serial.println("[NVS] Configuration loaded successfully");
}

void CanConfigManager::SaveToNVS() {
  // Build blob (32 bytes - updated from 28 bytes in v2.2.0)
  uint8_t buffer[32] = {0};
  
  buffer[0] = (mode_ == kFilterMonitoring) ? 0 : 1;
  buffer[1] = id_count_;
  
  // Write IDs (little-endian)
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t offset = 2 + (i * 4);
    buffer[offset]     = ids_[i] & 0xFF;
    buffer[offset + 1] = (ids_[i] >> 8) & 0xFF;
    buffer[offset + 2] = (ids_[i] >> 16) & 0xFF;
    buffer[offset + 3] = (ids_[i] >> 24) & 0xFF;
  }
  
  buffer[22] = extended_ ? 1 : 0;
  
  // Write bitrate (little-endian)
  buffer[23] = bitrate_ & 0xFF;
  buffer[24] = (bitrate_ >> 8) & 0xFF;
  buffer[25] = (bitrate_ >> 16) & 0xFF;
  buffer[26] = (bitrate_ >> 24) & 0xFF;
  
  // buffer[27-31] reserved (already zero)
  
  // Write to NVS
  nvs_.putBytes("config", buffer, 32);
  
  Serial.println("[NVS] Configuration saved");
}

void CanConfigManager::ClearNVS() {
  nvs_.remove("config");
  Serial.println("[NVS] Configuration cleared");
  SetDefaults();
}

bool CanConfigManager::ParseJsonConfig(const String& json) {
  JsonDocument doc;
  
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.printf("[JSON] Parse error: %s\n", error.c_str());
    return false;
  }
  
  // Parse mode
  const char* mode_str = doc["mode"];
  if (!mode_str) {
    Serial.println("[JSON] Missing 'mode' field");
    return false;
  }
  
  if (strcmp(mode_str, "monitoring") == 0) {
    mode_ = kFilterMonitoring;
    id_count_ = 0;
    // Clear IDs array for monitoring mode
    for (uint8_t i = 0; i < 5; i++) {
      ids_[i] = 0;
    }
  } else if (strcmp(mode_str, "specific") == 0) {
    mode_ = kFilterSpecific;
    
    // Parse IDs array
    JsonArray ids_array = doc["ids"];
    if (!ids_array) {
      Serial.println("[JSON] Missing 'ids' array");
      return false;
    }
    
    id_count_ = 0;
    for (JsonVariant v : ids_array) {
      if (id_count_ >= kMaxIdCount) break;
      
      uint32_t id = v.as<uint32_t>();
      if (id != 0) {  // Skip zero entries
        ids_[id_count_++] = id;
      }
    }
    
    if (id_count_ == 0) {
      Serial.println("[JSON] No valid IDs in specific mode");
      return false;
    }
  } else {
    Serial.printf("[JSON] Invalid mode: %s\n", mode_str);
    return false;
  }
  
  // Parse extended flag
  extended_ = doc["extended"] | false;
  
  // Parse bitrate (optional, defaults to 500k)
  bitrate_ = doc["bitrate"] | kDefaultBitrate;
  
  // Validate
  if (!ValidateConfig()) {
    return false;
  }
  
  return true;
}

bool CanConfigManager::ValidateConfig() {
  Serial.printf("[VALIDATE] Checking: mode=%d, id_count=%d, extended=%d, bitrate=%lu\n",
                mode_, id_count_, extended_, bitrate_);
  
  // Check mode
  if (mode_ != kFilterMonitoring && mode_ != kFilterSpecific) {
    Serial.println("[VALIDATE] Invalid mode");
    return false;
  }
  
  // Validate bitrate
  if (bitrate_ != kBitrate125k && bitrate_ != kBitrate250k && 
      bitrate_ != kBitrate500k && bitrate_ != kBitrate1000k) {
    Serial.printf("[VALIDATE] Invalid bitrate: %lu (must be 125000, 250000, 500000, or 1000000)\n", 
                  bitrate_);
    return false;
  }
  
  // Check ID count
  if (mode_ == kFilterSpecific) {
    if (id_count_ == 0 || id_count_ > kMaxIdCount) {
      Serial.printf("[VALIDATE] Invalid ID count: %d (must be 1-5)\n", id_count_);
      return false;
    }
    
    // Check ID ranges
    uint32_t max_id = extended_ ? kMaxExtendedId : kMaxStandardId;
    
    for (uint8_t i = 0; i < id_count_; i++) {
      if (ids_[i] > max_id) {
        Serial.printf("[VALIDATE] ID %d exceeds maximum for %s IDs: 0x%X > 0x%X\n",
                      i, extended_ ? "extended" : "standard", 
                      ids_[i], max_id);
        return false;
      }
    }
    
    // Check for duplicates
    for (uint8_t i = 0; i < id_count_; i++) {
      for (uint8_t j = i + 1; j < id_count_; j++) {
        if (ids_[i] == ids_[j]) {
          Serial.printf("[VALIDATE] Duplicate ID: 0x%X\n", ids_[i]);
          return false;
        }
      }
    }
  }
  
  return true;
}

twai_timing_config_t CanConfigManager::BitrateToTimingConfig(uint32_t bitrate) const {
  switch (bitrate) {
    case kBitrate125k:
      return kCan125Kbps;
    case kBitrate250k:
      return kCan250Kbps;
    case kBitrate500k:
      return kCan500Kbps;
    case kBitrate1000k:
      return kCan1000Kbps;
    default:
      Serial.printf("[BITRATE] Invalid bitrate %lu, using default %lu\n", 
                    bitrate, kDefaultBitrate);
      return kCan500Kbps;
  }
}

bool CanConfigManager::WaitForConfig(uint32_t timeout_ms) {
  Serial.println("\n╔════════════════════════════════════════════════════════╗");
  Serial.println("║ CAN Configuration Upload Window                       ║");
  Serial.println("╚════════════════════════════════════════════════════════╝");
  Serial.printf("Waiting %d seconds for JSON config via Serial...\n", timeout_ms / 1000);
  Serial.println("Send JSON now or window will close automatically.\n");
  
  unsigned long start = millis();
  String json_buffer = "";
  bool config_received = false;
  
  while (millis() - start < timeout_ms && !config_received) {
    if (Serial.available() > 0) {
      char c = Serial.read();
      
      if (c == '\n' || c == '\r') {
        if (json_buffer.length() > 0) {
          Serial.println("\n[RX] Received JSON config");
          
          if (ParseJsonConfig(json_buffer)) {
            SaveToNVS();
            
            // Send success response
            JsonDocument response;
            response["status"] = "ok";
            response["mode"] = (mode_ == kFilterMonitoring) ? "monitoring" : "specific";
            response["active_ids"] = id_count_;
            response["bitrate"] = bitrate_;
            
            String response_str;
            serializeJson(response, response_str);
            Serial.println(response_str);
            
            Serial.println("\n✓ Configuration uploaded and saved!");
            config_received = true;
          } else {
            // Send error response
            JsonDocument response;
            response["status"] = "error";
            response["message"] = "Validation failed";
            
            String response_str;
            serializeJson(response, response_str);
            Serial.println(response_str);
            
            Serial.println("\n✗ Configuration rejected (validation failed)");
          }
        }
        json_buffer = "";
      } else {
        json_buffer += c;
      }
    }
    
    delay(10);
  }
  
  if (!config_received) {
    Serial.println("[TIMEOUT] No configuration received");
    Serial.println("Using stored configuration from NVS\n");
  }
  
  return config_received;
}

void CanConfigManager::ApplyToCanBus(WaveshareCan* can) {
  if (!can) {
    Serial.println("[APPLY] Error: CAN instance is null");
    return;
  }
  
  Serial.println("[APPLY] Applying configuration to CAN bus...");
  
  // Convert bitrate to timing config and restart CAN with new bitrate
  twai_timing_config_t timing = BitrateToTimingConfig(bitrate_);
  Serial.printf("[APPLY] Bitrate: %lu bps\n", bitrate_);
  
  // Restart CAN with configured bitrate
  can->End();
  if (!can->Begin(timing)) {
    Serial.println("[APPLY] ERROR: Failed to start CAN with configured bitrate!");
    return;
  }
  
  // Apply filter mode
  can->SetFilterMode(mode_);
  
  if (mode_ == kFilterSpecific && id_count_ > 0) {
    can->SetAcceptedIds(ids_, id_count_, extended_);
  }
  
  Serial.printf("[APPLY] Filter mode: %s\n", 
                (mode_ == kFilterMonitoring) ? "Monitoring" : "Specific");
  
  if (mode_ == kFilterSpecific) {
    Serial.printf("[APPLY] Accepted IDs: %d\n", id_count_);
    Serial.print("[APPLY] IDs: ");
    for (uint8_t i = 0; i < id_count_; i++) {
      Serial.printf("0x%X ", ids_[i]);
    }
    Serial.println();
  }
  
  Serial.println("[APPLY] Configuration applied successfully\n");
}

void CanConfigManager::PrintConfig() const {
  Serial.println("\n═══════════════════════════════════════");
  Serial.println("       Stored Configuration");
  Serial.println("═══════════════════════════════════════");
  
  Serial.printf("Mode: %s\n", 
                (mode_ == kFilterMonitoring) ? "Monitoring" : "Specific");
  Serial.printf("Bitrate: %lu bps\n", bitrate_);
  Serial.printf("Extended: %s\n", extended_ ? "Yes" : "No");
  
  if (mode_ == kFilterSpecific) {
    Serial.printf("Active IDs: %d\n", id_count_);
    
    if (id_count_ > 0) {
      Serial.print("IDs: ");
      for (uint8_t i = 0; i < id_count_; i++) {
        Serial.printf("%lu (0x%X) ", ids_[i], ids_[i]);
      }
      Serial.println();
    }
  }
  
  Serial.println("═══════════════════════════════════════\n");
}

String CanConfigManager::GetConfigJson() const {
  JsonDocument doc;
  
  doc["mode"] = (mode_ == kFilterMonitoring) ? "monitoring" : "specific";
  doc["extended"] = extended_;
  doc["bitrate"] = bitrate_;
  
  JsonArray ids_array = doc["ids"].to<JsonArray>();
  for (uint8_t i = 0; i < 5; i++) {
    ids_array.add(ids_[i]);
  }
  
  String json_str;
  serializeJson(doc, json_str);
  return json_str;
}