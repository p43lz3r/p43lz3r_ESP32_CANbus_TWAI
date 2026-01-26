// Copyright 2026 Your Name / Project. All rights reserved.
#include "waveshare_can.h"

#include <esp_io_expander.hpp>

WaveshareCan::WaveshareCan(BoardType board, int rx_pin, int tx_pin)
    : board_type_(board),
      rx_pin_(rx_pin >= 0 ? rx_pin : (board == kBoard43b ? 16 : 19)),
      tx_pin_(tx_pin >= 0 ? tx_pin : (board == kBoard43b ? 15 : 20)),
      initialized_(false),
      expander_initialized_(false),
      listen_only_(false),
      expander_(nullptr),
      alert_callback_(nullptr) {
  timing_config_ = kCan500Kbps;
  filter_config_ = TWAI_FILTER_CONFIG_ACCEPT_ALL();
}

WaveshareCan::~WaveshareCan() {
  End();
}

bool WaveshareCan::InitIoExpander(int scl_pin, int sda_pin, int addr) {
  if (expander_initialized_) return true;

  // Google Style: Use std::unique_ptr for ownership
  expander_ = std::unique_ptr<esp_expander::CH422G>(
      new esp_expander::CH422G(scl_pin, sda_pin, addr));

  if (!expander_ || !expander_->init() || !expander_->begin()) {
    Serial.println("IO Expander init failed - continuing without");
    expander_.reset();
    return false;
  }

  // Basic safe setup
  expander_->enableAllIO_Output();

  uint8_t high_pins = kExpIoTpRst | kExpIoSdCs;
  expander_->multiDigitalWrite(high_pins, HIGH);
  expander_->digitalWrite(kExpIoLcdBl, LOW);

  expander_initialized_ = true;
  Serial.println("CH422G IO Expander initialized (optional)");
  return true;
}

bool WaveshareCan::Begin(twai_timing_config_t speed_config) {
  if (initialized_) return true;

  timing_config_ = speed_config;

  // Use static_cast for type safety
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
  if (initialized_) {
    twai_stop();
    twai_driver_uninstall();
    initialized_ = false;
  }
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

  return (twai_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK);
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

void WaveshareCan::Filter(uint32_t id, uint32_t mask) {
  if (!initialized_) return;

  End();
  filter_config_.acceptance_code = (id << 3);
  filter_config_.acceptance_mask = (~mask) << 3;
  filter_config_.single_filter = true;
  Begin(timing_config_);
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
    Serial.println("BUS-OFF → trying recovery");
    twai_initiate_recovery();
  }
  if (alerts & TWAI_ALERT_BUS_RECOVERED) {
    Serial.println("Bus recovered");
  }
  if (alerts & TWAI_ALERT_ERR_PASS) {
    Serial.println("Error passive state");
  }
  if (alerts & TWAI_ALERT_BUS_ERROR) {
    Serial.printf("Bus error - count: %d\n", status.bus_error_count);
  }
  if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
    Serial.printf("RX queue full - buffered:%d missed:%d overrun:%d\n",
                  status.msgs_to_rx, status.rx_missed_count,
                  status.rx_overrun_count);
  }
  if (alerts & TWAI_ALERT_TX_FAILED) {
    Serial.printf("TX failed - buffered:%d errors:%d failed:%d\n",
                  status.msgs_to_tx, status.tx_error_counter,
                  status.tx_failed_count);
  }
}