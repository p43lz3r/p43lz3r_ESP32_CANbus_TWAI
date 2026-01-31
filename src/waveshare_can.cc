// Copyright 2026 by p43lz3r
#include "waveshare_can.h"


WaveshareCan::WaveshareCan(BoardType board, int rx_pin, int tx_pin)
    : board_type_(board),
      rx_pin_(rx_pin >= 0 ? rx_pin : (board == kBoard43b ? 16 : 19)),
      tx_pin_(tx_pin >= 0 ? tx_pin : (board == kBoard43b ? 15 : 20)),
      initialized_(false),
      listen_only_(false),
      alert_interrupt_enabled_(false),
      rx_interrupt_enabled_(false),
      shutdown_(false),
      alert_callback_(nullptr),
      rx_callback_(nullptr),
      alert_task_handle_(nullptr),
      rx_task_handle_(nullptr),
      rx_queue_(nullptr),
      rx_dropped_count_(0),
      tx_failed_count_(0) {
  timing_config_ = kCan500Kbps;
  filter_config_ = TWAI_FILTER_CONFIG_ACCEPT_ALL();
}

WaveshareCan::~WaveshareCan() {
  DisableRxInterrupt();
  DisableAlertInterrupt();
  End();
}

bool WaveshareCan::Begin(twai_timing_config_t speed_config) {
  // Re-init safety: Clean up existing state if already initialized
  if (initialized_) {
    Serial.println("Begin() called while already initialized - cleaning up first");
    End();
  }

  timing_config_ = speed_config;

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
      static_cast<gpio_num_t>(tx_pin_), static_cast<gpio_num_t>(rx_pin_),
      listen_only_ ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
  g_config.rx_queue_len = 32;

  if (twai_driver_install(&g_config, &speed_config, &filter_config_) != ESP_OK) {
    Serial.println("TWAI driver install failed");
    return false;
  }

  if (twai_start() != ESP_OK) {
    Serial.println("TWAI start failed");
    twai_driver_uninstall();
    return false;
  }

  uint32_t alerts_to_enable =
      TWAI_ALERT_RX_DATA | TWAI_ALERT_TX_IDLE | TWAI_ALERT_TX_SUCCESS |
      TWAI_ALERT_TX_FAILED | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR |
      TWAI_ALERT_RX_QUEUE_FULL | TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;

  if (twai_reconfigure_alerts(alerts_to_enable, nullptr) != ESP_OK) {
    Serial.println("Alerts reconfigure failed");
    twai_stop();
    twai_driver_uninstall();
    return false;
  }

  initialized_ = true;
  Serial.printf("CAN started - RX:%d TX:%d - %s mode\n", rx_pin_, tx_pin_,
           listen_only_ ? "listen-only" : "normal");
  return true;
}

void WaveshareCan::End() {
  if (!initialized_) return;

  // Signal tasks to shutdown
  shutdown_ = true;
  
  // Stop interrupt tasks BEFORE driver shutdown
  DisableRxInterrupt();
  DisableAlertInterrupt();
  
  // Now safe to stop TWAI driver
  twai_stop();
  twai_driver_uninstall();
  
  initialized_ = false;
  shutdown_ = false;  // Reset for next Begin()
}

int WaveshareCan::Available() {
  if (!initialized_) return 0;
  twai_status_info_t status;
  if (twai_get_status_info(&status) != ESP_OK) return 0;
  return status.msgs_to_rx;
}

bool WaveshareCan::SendMessage(uint32_t id, bool extended, uint8_t* data,
                               uint8_t length, bool rtr) {
  if (!initialized_ || listen_only_) return false;
  if (length > 8) length = 8;

  twai_message_t message = {};
  message.identifier = id;
  message.extd = extended;
  message.rtr = rtr;
  message.data_length_code = length;

  if (!rtr && data) {
    memcpy(message.data, data, length);
  }

  esp_err_t res = twai_transmit(&message, pdMS_TO_TICKS(1000));
  if (res != ESP_OK) {
    tx_failed_count_++;
    Serial.printf("TX failed: error 0x%X\n", res);
  }
  return (res == ESP_OK);
}

bool WaveshareCan::SendMessage(uint32_t id, uint8_t* data, uint8_t length) {
  return SendMessage(id, false, data, length, false);
}

int WaveshareCan::ReceiveMessage(uint32_t* id, bool* extended, uint8_t* data,
                                 uint8_t* length, bool* rtr) {
  if (!initialized_) return -1;

  twai_message_t message;
  if (twai_receive(&message, 0) != ESP_OK) return -1;

  if (id) *id = message.identifier;
  if (extended) *extended = message.extd;
  if (length) *length = message.data_length_code;
  if (rtr) *rtr = message.rtr;

  if (!message.rtr && data) {
    memcpy(data, message.data, message.data_length_code);
  }

  return message.data_length_code;
}

bool WaveshareCan::Filter(uint32_t id, uint32_t mask, bool extended) {
  if (!initialized_) return false;

  End();
  
  if (extended) {
    // Extended 29-bit IDs: use bits as-is
    filter_config_.acceptance_code = (id << 3);
    filter_config_.acceptance_mask = ~(mask << 3);
  } else {
    // Standard 11-bit IDs: shift to upper bits of 29-bit field
    filter_config_.acceptance_code = (id << 21);
    filter_config_.acceptance_mask = ~(mask << 21);
  }
  
  filter_config_.single_filter = true;
  return Begin(timing_config_);
}

bool WaveshareCan::GetStatus(twai_status_info_t* status) {
  if (!initialized_ || !status) return false;
  return (twai_get_status_info(status) == ESP_OK);
}

bool WaveshareCan::SetListenOnly(bool listen_only) {
  if (initialized_ && listen_only_ != listen_only) {
    End();
    listen_only_ = listen_only;
    return Begin(timing_config_);
  }
  listen_only_ = listen_only;
  return true;
}

bool WaveshareCan::ProcessAlerts(uint32_t* alerts_triggered) {
  if (!initialized_) return false;

  uint32_t alerts = 0;
  if (twai_read_alerts(&alerts, pdMS_TO_TICKS(0)) != ESP_OK) return false;
  if (alerts == 0) return false;

  if (alerts_triggered) *alerts_triggered = alerts;

  HandleAlerts(alerts);
  if (alert_callback_) alert_callback_(alerts);

  return true;
}

void WaveshareCan::OnAlert(void (*callback)(uint32_t)) {
  alert_callback_ = callback;
}

void WaveshareCan::HandleAlerts(uint32_t alerts) {
  twai_status_info_t status;
  twai_get_status_info(&status);

  if (alerts & TWAI_ALERT_BUS_OFF) {
    Serial.println("BUS-OFF -> trying recovery");
    twai_initiate_recovery();
  }
  if (alerts & TWAI_ALERT_BUS_RECOVERED) {
    Serial.println("Bus recovered");
  }
  if (alerts & TWAI_ALERT_ERR_PASS) {
    Serial.println("Error passive state");
  }
  if (alerts & TWAI_ALERT_BUS_ERROR) {
    Serial.printf("Bus error - count: %d", status.bus_error_count);
  }
  if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
    Serial.printf("RX queue full - buffered:%d missed:%d overrun:%d",
             status.msgs_to_rx, status.rx_missed_count, status.rx_overrun_count);
  }
  if (alerts & TWAI_ALERT_TX_FAILED) {
    Serial.printf("TX failed - buffered:%d errors:%d failed:%d",
             status.msgs_to_tx, status.tx_error_counter, status.tx_failed_count);
  }
}

bool WaveshareCan::EnableAlertInterrupt(void (*callback)(uint32_t)) {
  if (!initialized_) {
    Serial.println("Cannot enable alert interrupt: CAN not initialized");
    return false;
  }

  if (alert_interrupt_enabled_) {
    Serial.println("Alert interrupt already enabled");
    return true;
  }

  if (callback) {
    alert_callback_ = callback;
  }

  // CRITICAL: Set flag BEFORE creating task to avoid race condition
  alert_interrupt_enabled_ = true;

  BaseType_t result = xTaskCreate(
      AlertTaskWrapper,
      "can_alert_task",
      kAlertTaskStackSize,  // Already in words
      this,
      4,
      &alert_task_handle_);

  if (result != pdPASS) {
    Serial.println("Failed to create alert task");
    alert_task_handle_ = nullptr;
    alert_interrupt_enabled_ = false;  // Rollback
    return false;
  }

  Serial.println("Alert interrupt enabled");
  return true;
}

void WaveshareCan::DisableAlertInterrupt() {
  if (!alert_interrupt_enabled_) return;

  alert_interrupt_enabled_ = false;

  if (alert_task_handle_ != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(50));  // Let task exit loop
    vTaskDelete(alert_task_handle_);
    alert_task_handle_ = nullptr;
  }

  Serial.println("Alert interrupt disabled");
}

void WaveshareCan::AlertTaskWrapper(void* arg) {
  WaveshareCan* instance = static_cast<WaveshareCan*>(arg);
  instance->AlertTask();
}

void WaveshareCan::AlertTask() {
  // Note: No Serial - causes stack overflow
  uint32_t alerts = 0;
  uint32_t check_counter = 0;

  while (alert_interrupt_enabled_ && initialized_ && !shutdown_) {
    esp_err_t err = twai_read_alerts(&alerts, pdMS_TO_TICKS(100));
    
    if (err == ESP_OK && alerts != 0) {
      // Only call user callback - NO HandleAlerts (has Serial.printf)
      if (alert_callback_) {
        alert_callback_(alerts);
      }
    } else if (err == ESP_ERR_TIMEOUT) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Periodic stack monitoring (no Serial)
    if (++check_counter % 1000 == 0) {
      uint32_t free_stack = uxTaskGetStackHighWaterMark(NULL);
      (void)free_stack;  // Suppress unused warning
    }
  }
}

void WaveshareCan::OnReceive(void (*callback)(const twai_message_t& msg)) {
  rx_callback_ = callback;
}

bool WaveshareCan::EnableRxInterrupt(void (*callback)(const twai_message_t& msg)) {
  if (!initialized_) {
    Serial.println("Cannot enable RX interrupt: CAN not initialized");
    return false;
  }

  if (rx_interrupt_enabled_) {
    Serial.println("RX interrupt already enabled");
    return true;
  }

  if (callback) {
    rx_callback_ = callback;
  }

  shutdown_ = false;

  rx_queue_ = xQueueCreate(16, sizeof(twai_message_t));
  
  if (rx_queue_ == nullptr) {
    Serial.println("Failed to create RX queue");
    return false;
  }

  rx_interrupt_enabled_ = true;

  BaseType_t result = xTaskCreate(
      RxTaskWrapper,
      "can_rx_task",
      kRxTaskStackSize,
      this,
      5,
      &rx_task_handle_);

  if (result != pdPASS) {
    Serial.println("Failed to create RX task");
    vQueueDelete(rx_queue_);
    rx_queue_ = nullptr;
    rx_task_handle_ = nullptr;
    rx_interrupt_enabled_ = false;
    return false;
  }

  Serial.println("RX interrupt enabled");
  return true;
}

void WaveshareCan::DisableRxInterrupt() {
  if (!rx_interrupt_enabled_) return;

  rx_interrupt_enabled_ = false;

  if (rx_task_handle_ != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(50));  // Let task exit loop
    vTaskDelete(rx_task_handle_);
    rx_task_handle_ = nullptr;
  }

  if (rx_queue_ != nullptr) {
    vQueueDelete(rx_queue_);
    rx_queue_ = nullptr;
  }

  Serial.println("RX interrupt disabled");
}

void WaveshareCan::RxTaskWrapper(void* arg) {
  WaveshareCan* instance = static_cast<WaveshareCan*>(arg);
  instance->RxTask();
}

void WaveshareCan::RxTask() {
  twai_message_t message;
  uint32_t check_counter = 0;

  while (rx_interrupt_enabled_ && initialized_ && !shutdown_) {
    // Use timeout instead of infinite block for clean shutdown
    esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(100));
    
    if (err == ESP_OK) {
      // Process first message
      if (rx_callback_) {
        rx_callback_(message);
      }

      // Try to queue message
      if (xQueueSend(rx_queue_, &message, 0) != pdTRUE) {
        rx_dropped_count_++;
        // Note: Serial removed - causes stack overflow
      }

      // DRAIN: Get all remaining messages immediately (burst handling)
      while (twai_receive(&message, 0) == ESP_OK) {
        if (rx_callback_) {
          rx_callback_(message);
        }

        if (xQueueSend(rx_queue_, &message, 0) != pdTRUE) {
          rx_dropped_count_++;
        }
      }
      
    } else if (err == ESP_ERR_TIMEOUT) {
      // Normal - no messages, continue
      vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Periodic stack monitoring (no Serial - causes overflow)
    if (++check_counter % 1000 == 0) {
      uint32_t free_stack = uxTaskGetStackHighWaterMark(NULL);
      if (free_stack < 512) {
        // Stack low - increment counter for external monitoring
        rx_dropped_count_++;  // Reuse as warning indicator
      }
    }
  }

  // Task exits cleanly (no Serial)
}

int WaveshareCan::QueuedMessages() {
  if (!rx_interrupt_enabled_ || rx_queue_ == nullptr) return 0;
  return uxQueueMessagesWaiting(rx_queue_);
}

int WaveshareCan::ReceiveFromQueue(uint32_t* id, bool* extended, uint8_t* data,
                                   uint8_t* length, bool* rtr) {
  if (!rx_interrupt_enabled_ || rx_queue_ == nullptr) return -1;

  twai_message_t message;
  if (xQueueReceive(rx_queue_, &message, 0) != pdTRUE) {
    return -1;  // No message available
  }

  if (id) *id = message.identifier;
  if (extended) *extended = message.extd;
  if (length) *length = message.data_length_code;
  if (rtr) *rtr = message.rtr;

  if (!message.rtr && data) {
    memcpy(data, message.data, message.data_length_code);
  }

  return message.data_length_code;
}

WaveshareCan::TaskStats WaveshareCan::GetTaskStats() const {
  TaskStats stats = {0, 0, kRxTaskStackSize * 4, kAlertTaskStackSize * 4};  // Convert words to bytes
  
  if (rx_task_handle_ != nullptr) {
    stats.rx_stack_free = uxTaskGetStackHighWaterMark(rx_task_handle_);
  }
  if (alert_task_handle_ != nullptr) {
    stats.alert_stack_free = uxTaskGetStackHighWaterMark(alert_task_handle_);
  }
  
  return stats;
}

void WaveshareCan::ResetCounters() {
  rx_dropped_count_ = 0;
  tx_failed_count_ = 0;
}