// Copyright 2026 p43lz3r
#ifndef PROJECT_WAVESHARE_CAN_H_
#define PROJECT_WAVESHARE_CAN_H_

#include <Arduino.h>
#include "driver/twai.h"
#include "freertos/queue.h"

// Board variants
enum BoardType {
  kBoard43b,  // ESP32-S3-Touch-LCD-4.3B (RX=16, TX=15)
  kBoard7b    // 7-inch version (RX=19, TX=20)
};

// CAN speed presets
constexpr twai_timing_config_t kCan5Kbps    = TWAI_TIMING_CONFIG_5KBITS();
constexpr twai_timing_config_t kCan10Kbps   = TWAI_TIMING_CONFIG_10KBITS();
constexpr twai_timing_config_t kCan20Kbps   = TWAI_TIMING_CONFIG_20KBITS();
constexpr twai_timing_config_t kCan50Kbps   = TWAI_TIMING_CONFIG_50KBITS();
constexpr twai_timing_config_t kCan100Kbps  = TWAI_TIMING_CONFIG_100KBITS();
constexpr twai_timing_config_t kCan125Kbps  = TWAI_TIMING_CONFIG_125KBITS();
constexpr twai_timing_config_t kCan250Kbps  = TWAI_TIMING_CONFIG_250KBITS();
constexpr twai_timing_config_t kCan500Kbps  = TWAI_TIMING_CONFIG_500KBITS();
constexpr twai_timing_config_t kCan800Kbps  = TWAI_TIMING_CONFIG_800KBITS();
constexpr twai_timing_config_t kCan1000Kbps = TWAI_TIMING_CONFIG_1MBITS();

class WaveshareCan {
 public:
  WaveshareCan(BoardType board = kBoard43b, int rx_pin = -1, int tx_pin = -1);
  ~WaveshareCan();

  WaveshareCan(const WaveshareCan&) = delete;
  WaveshareCan& operator=(const WaveshareCan&) = delete;

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

  // Set callback for CAN alerts.
  //
  // WARNING: Callback is invoked from FreeRTOS task context (high priority)!
  // Keep execution time < 1ms. NO blocking calls, Serial.print, or allocations.
  void OnAlert(void (*callback)(uint32_t));

  // Enable interrupt-driven alert handling (starts background task)
  bool EnableAlertInterrupt(void (*callback)(uint32_t) = nullptr);

  // Disable alert interrupt and stop background task
  void DisableAlertInterrupt();

  // Set callback for received messages.
  //
  // WARNING: Callback is invoked from FreeRTOS task context (high priority)!
  // Requirements:
  //   - Keep execution time < 1ms
  //   - NO blocking calls (delay, Serial.print, file I/O, mutex locks)
  //   - NO memory allocations (malloc, new)
  //   - Use only ISR-safe FreeRTOS primitives
  //
  // Safe actions:
  //   - Set flags / volatile variables
  //   - Increment counters
  //   - Toggle GPIO pins
  //
  // For heavy processing, use ReceiveFromQueue() in main loop instead.
  void OnReceive(void (*callback)(const twai_message_t& msg));

  // Enable interrupt-driven RX handling (starts background task)
  bool EnableRxInterrupt(void (*callback)(const twai_message_t& msg) = nullptr);

  // Disable RX interrupt and stop background task
  void DisableRxInterrupt();

  // Get number of messages in interrupt queue (when using EnableRxInterrupt)
  int QueuedMessages();

  // Receive from interrupt queue (non-blocking, returns bytes read or -1)
  int ReceiveFromQueue(uint32_t* id, bool* extended, uint8_t* data,
                       uint8_t* length, bool* rtr = nullptr);

  // Statistics and monitoring
  struct TaskStats {
    uint32_t rx_stack_free;      // Stack words remaining for RX task
    uint32_t alert_stack_free;   // Stack words remaining for Alert task
    uint32_t rx_stack_size;      // Total RX task stack size in bytes
    uint32_t alert_stack_size;   // Total Alert task stack size in bytes
  };

  TaskStats GetTaskStats() const;
  uint32_t GetDroppedRxCount() const { return rx_dropped_count_; }
  uint32_t GetTxFailedCount() const { return tx_failed_count_; }
  void ResetCounters();

 private:
  void HandleAlerts(uint32_t alerts);
  static void AlertTaskWrapper(void* arg);
  void AlertTask();
  static void RxTaskWrapper(void* arg);
  void RxTask();

  // Task stack sizes (in WORDS for xTaskCreate)
  static constexpr uint32_t kRxTaskStackSize = 2048;     // 2048 words = 8KB
  static constexpr uint32_t kAlertTaskStackSize = 1024;  // 1024 words = 4KB

  BoardType board_type_;
  int rx_pin_;
  int tx_pin_;
  bool initialized_;
  bool listen_only_;
  bool alert_interrupt_enabled_;
  bool rx_interrupt_enabled_;
  volatile bool shutdown_;  // Shutdown flag for clean task termination
  void (*alert_callback_)(uint32_t);
  void (*rx_callback_)(const twai_message_t&);
  TaskHandle_t alert_task_handle_;
  TaskHandle_t rx_task_handle_;
  QueueHandle_t rx_queue_;

  // Statistics (volatile for thread-safety on single increments)
  volatile uint32_t rx_dropped_count_;
  volatile uint32_t tx_failed_count_;

  twai_timing_config_t timing_config_;
  twai_filter_config_t filter_config_;
};

#endif  // PROJECT_WAVESHARE_CAN_H_