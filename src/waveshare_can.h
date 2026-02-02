// WaveshareCAN Library for ESP32-S3
// Version: 2.2.0
// Date: 02.02.2026 19:00
// Copyright 2026 p43lz3r
//
// CAN bus communication library for ESP32-S3 Waveshare boards with software-based
// message filtering, interrupt-driven reception, and runtime configuration support.
//
// Hardware tested:
// - ESP32-S3-Touch-LCD-4.3B (RX=GPIO16, TX=GPIO15)
// - ESP32-S3-Touch-LCD-7.0B (RX=GPIO19, TX=GPIO20)
//
// Features:
// - Standard (11-bit) and Extended (29-bit) CAN IDs
// - Polling and Interrupt modes
// - Software-based message filtering (runtime configurable)
// - Hardware acceptance filters
// - Listen-only mode
// - RTR frame support
// - Automatic bus-off recovery
// - Thread-safe FreeRTOS task management

#ifndef PROJECT_WAVESHARE_CAN_H_
#define PROJECT_WAVESHARE_CAN_H_

#include <Arduino.h>
#include "driver/twai.h"
#include "freertos/queue.h"

// Board variant selection
enum BoardType {
  kBoard43b,  // ESP32-S3-Touch-LCD-4.3B (RX=16, TX=15)
  kBoard7b    // ESP32-S3-Touch-LCD-7.0B (RX=19, TX=20)
};

// Software filter modes (v2.2.0)
enum FilterMode {
  kFilterMonitoring = 0,  // Accept all CAN IDs (no filtering)
  kFilterSpecific = 1     // Only accept configured IDs (1-5 IDs)
};

// CAN bus speed presets (50 kbps - 1 Mbps)
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

// WaveshareCAN - Main CAN bus interface class
//
// Provides comprehensive CAN bus communication with software filtering,
// interrupt-driven reception, and production-ready error handling.
//
// Example usage:
//   WaveshareCan can(kBoard43b);
//   can.Begin(kCan500Kbps);
//   
//   // Configure software filter
//   uint32_t ids[] = {0x100, 0x200, 0x300};
//   can.SetAcceptedIds(ids, 3, false);
//   can.SetFilterMode(kFilterSpecific);
//   
//   // Enable interrupt mode
//   can.EnableRxInterrupt(onMessage);
class WaveshareCan {
 public:
  // Constructor
  //
  // Args:
  //   board: Board variant (kBoard43b or kBoard7b)
  //   rx_pin: Custom RX pin (-1 for board default)
  //   tx_pin: Custom TX pin (-1 for board default)
  WaveshareCan(BoardType board = kBoard43b, int rx_pin = -1, int tx_pin = -1);
  
  // Destructor - ensures clean shutdown
  ~WaveshareCan();

  // Non-copyable
  WaveshareCan(const WaveshareCan&) = delete;
  WaveshareCan& operator=(const WaveshareCan&) = delete;

  // Initialize CAN bus with specified speed
  //
  // Args:
  //   speed_config: Timing configuration (use kCan500Kbps constants)
  //
  // Returns:
  //   true if initialization successful, false on error
  //
  // Note: Can be called multiple times to restart with different speed.
  //       Automatically cleans up previous state if already initialized.
  bool Begin(twai_timing_config_t speed_config = kCan500Kbps);

  // Stop CAN bus and release resources
  //
  // Stops all interrupt tasks, uninstalls TWAI driver.
  // Safe to call multiple times or when not initialized.
  void End();

  // Get number of messages waiting in hardware RX queue
  //
  // Returns:
  //   Number of messages ready to read (0 if none)
  int Available();

  // Send CAN message (full control version)
  //
  // Args:
  //   id: CAN identifier (11-bit standard or 29-bit extended)
  //   extended: true for 29-bit extended ID, false for 11-bit standard
  //   data: Pointer to data bytes (NULL for RTR)
  //   length: Number of data bytes (0-8, clamped to 8)
  //   rtr: true for Remote Transmission Request frame
  //
  // Returns:
  //   true if message queued successfully, false on error or in listen-only mode
  bool SendMessage(uint32_t id, bool extended, uint8_t* data, uint8_t length,
                   bool rtr = false);

  // Send CAN message (simplified version for standard IDs)
  //
  // Args:
  //   id: Standard 11-bit CAN identifier
  //   data: Pointer to data bytes
  //   length: Number of data bytes (0-8)
  //
  // Returns:
  //   true if message queued successfully, false on error
  bool SendMessage(uint32_t id, uint8_t* data, uint8_t length);

  // Receive one message from hardware queue (non-blocking)
  //
  // Args:
  //   id: Pointer to store message ID (can be NULL)
  //   extended: Pointer to store extended flag (can be NULL)
  //   data: Pointer to buffer for data bytes (can be NULL)
  //   length: Pointer to store data length (can be NULL)
  //   rtr: Pointer to store RTR flag (can be NULL)
  //
  // Returns:
  //   Number of data bytes received, or -1 if no message available
  int ReceiveMessage(uint32_t* id, bool* extended, uint8_t* data,
                     uint8_t* length, bool* rtr = nullptr);

  // Set hardware acceptance filter (requires driver restart)
  //
  // Args:
  //   id: CAN ID to accept
  //   mask: Acceptance mask (0 = must match, bits set = don't care)
  //   extended: true for 29-bit IDs, false for 11-bit IDs
  //
  // Returns:
  //   true if filter applied successfully, false on error
  //
  // Note: Calls End() then Begin() internally. Use SetAcceptedIds() for
  //       runtime filtering without bus restart.
  bool Filter(uint32_t id, uint32_t mask = 0, bool extended = false);

  // Get current TWAI driver status
  //
  // Args:
  //   status: Pointer to status structure to fill
  //
  // Returns:
  //   true if status retrieved successfully, false if not initialized
  bool GetStatus(twai_status_info_t* status);

  // Enable or disable listen-only mode
  //
  // Args:
  //   listen_only: true for listen-only, false for normal mode
  //
  // Returns:
  //   true if mode changed successfully
  //
  // Note: Requires driver restart if already initialized.
  bool SetListenOnly(bool listen_only);

  // Check and process CAN alerts (call regularly in polling mode)
  //
  // Args:
  //   alerts_triggered: Pointer to store alert flags (can be NULL)
  //
  // Returns:
  //   true if any alerts were processed, false if none or not initialized
  bool ProcessAlerts(uint32_t* alerts_triggered = nullptr);

  // Set callback for CAN alert events
  //
  // Args:
  //   callback: Function pointer to alert handler, or NULL to disable
  //
  // Callback signature: void callback(uint32_t alerts)
  // Alert flags: TWAI_ALERT_BUS_OFF, TWAI_ALERT_ERR_PASS, etc.
  void OnAlert(void (*callback)(uint32_t));

  // Enable interrupt-driven alert handling (starts background task)
  //
  // Args:
  //   callback: Optional callback for alerts (can be NULL)
  //
  // Returns:
  //   true if task started successfully, false on error
  //
  // Note: Callback executes in FreeRTOS task context (8KB stack).
  //       Keep execution time minimal (<1ms).
  bool EnableAlertInterrupt(void (*callback)(uint32_t) = nullptr);

  // Disable alert interrupt and stop background task
  //
  // Waits for task to exit cleanly (max 500ms).
  void DisableAlertInterrupt();

  // Set callback for received CAN messages
  //
  // Args:
  //   callback: Function pointer to message handler
  //
  // Callback signature: void callback(const twai_message_t& msg)
  //
  // WARNING: Callback executes in FreeRTOS task context!
  // Requirements:
  //   - Execution time < 1ms
  //   - NO blocking calls (delay, Serial.print, file I/O)
  //   - NO memory allocation
  //   - Use only ISR-safe FreeRTOS primitives
  //
  // For complex processing, use ReceiveFromQueue() in main loop instead.
  void OnReceive(void (*callback)(const twai_message_t& msg));

  // Enable interrupt-driven RX handling (starts background task)
  //
  // Args:
  //   callback: Optional callback for received messages (can be NULL)
  //
  // Returns:
  //   true if task started successfully, false on error
  //
  // Note: Messages are buffered in 16-deep queue and passed to callback.
  //       Software filtering applied before callback/queue (if configured).
  bool EnableRxInterrupt(void (*callback)(const twai_message_t& msg) = nullptr);

  // Disable RX interrupt and stop background task
  //
  // Waits for task to exit cleanly (max 500ms).
  // Queue is deleted and any buffered messages are lost.
  void DisableRxInterrupt();

  // Get number of messages in interrupt queue
  //
  // Returns:
  //   Number of messages buffered (0-16), or 0 if interrupt mode not enabled
  int QueuedMessages();

  // Receive message from interrupt queue (non-blocking)
  //
  // Args:
  //   id: Pointer to store message ID (can be NULL)
  //   extended: Pointer to store extended flag (can be NULL)
  //   data: Pointer to buffer for data bytes (can be NULL)
  //   length: Pointer to store data length (can be NULL)
  //   rtr: Pointer to store RTR flag (can be NULL)
  //
  // Returns:
  //   Number of data bytes received, or -1 if queue empty
  int ReceiveFromQueue(uint32_t* id, bool* extended, uint8_t* data,
                       uint8_t* length, bool* rtr = nullptr);

  // Task statistics for monitoring
  struct TaskStats {
    uint32_t rx_stack_free;      // RX task stack words remaining
    uint32_t alert_stack_free;   // Alert task stack words remaining
    uint32_t rx_stack_size;      // Total RX task stack size (bytes)
    uint32_t alert_stack_size;   // Total Alert task stack size (bytes)
  };

  // Get task stack usage statistics
  //
  // Returns:
  //   TaskStats structure with current stack usage
  //
  // Use to monitor for stack overflow (free should stay > 512 words).
  TaskStats GetTaskStats() const;

  // Get count of dropped RX messages (queue full)
  //
  // Returns:
  //   Number of messages dropped since last reset
  uint32_t GetDroppedRxCount() const { return rx_dropped_count_; }

  // Get count of failed TX attempts
  //
  // Returns:
  //   Number of transmissions that failed
  uint32_t GetTxFailedCount() const { return tx_failed_count_; }

  // Reset drop and TX fail counters
  void ResetCounters();

  // ──────────────────────────────────────────────────────────
  // Software-based message filtering (v2.2.0)
  // ──────────────────────────────────────────────────────────

  // Set filter mode (monitoring or specific)
  //
  // Args:
  //   mode: kFilterMonitoring (accept all) or kFilterSpecific (filter by ID)
  //
  // Note: Thread-safe when called in setup() before EnableRxInterrupt().
  //       For runtime changes while task is running, ensure proper synchronization.
  void SetFilterMode(FilterMode mode);

  // Configure accepted CAN IDs for specific mode
  //
  // Args:
  //   ids: Pointer to array of CAN IDs (up to 5)
  //   count: Number of IDs in array (1-5, clamped to 5)
  //   extended: true for 29-bit extended IDs, false for 11-bit standard
  //
  // Note: IDs are copied to internal array. Safe to pass stack variables.
  void SetAcceptedIds(const uint32_t* ids, uint8_t count, bool extended = false);

  // Get current filter mode
  //
  // Returns:
  //   kFilterMonitoring or kFilterSpecific
  FilterMode GetFilterMode() const { return filter_mode_; }

  // Get number of configured accepted IDs
  //
  // Returns:
  //   Number of IDs (0-5)
  uint8_t GetAcceptedIdCount() const { return accepted_id_count_; }

  // Get pointer to accepted IDs array
  //
  // Returns:
  //   Pointer to internal array (read-only)
  const uint32_t* GetAcceptedIds() const { return accepted_ids_; }

  // Check if using extended ID filtering
  //
  // Returns:
  //   true if extended (29-bit), false if standard (11-bit)
  bool IsExtendedFilter() const { return extended_filter_; }

 private:
  // Internal alert and RX handlers
  void HandleAlerts(uint32_t alerts);
  static void AlertTaskWrapper(void* arg);
  void AlertTask();
  static void RxTaskWrapper(void* arg);
  void RxTask();

  // Software filter check (returns true if message should be accepted)
  //
  // Args:
  //   msg: CAN message to check
  //
  // Returns:
  //   true if message passes filter, false if filtered out
  bool IsMessageAccepted(const twai_message_t& msg) const;

  // Task stack sizes (in WORDS for xTaskCreate, multiply by 4 for bytes)
  static constexpr uint32_t kRxTaskStackSize = 2048;     // 8KB
  static constexpr uint32_t kAlertTaskStackSize = 2048;  // 8KB

  // Board and pin configuration
  BoardType board_type_;
  int rx_pin_;
  int tx_pin_;

  // State flags
  bool initialized_;
  bool listen_only_;
  bool alert_interrupt_enabled_;
  bool rx_interrupt_enabled_;
  volatile bool shutdown_;  // Shutdown flag for clean task termination

  // Callbacks
  void (*alert_callback_)(uint32_t);
  void (*rx_callback_)(const twai_message_t&);

  // FreeRTOS task handles
  TaskHandle_t alert_task_handle_;
  TaskHandle_t rx_task_handle_;
  QueueHandle_t rx_queue_;

  // Software-based message filtering (v2.2.0)
  FilterMode filter_mode_;
  uint32_t accepted_ids_[5];
  uint8_t accepted_id_count_;
  bool extended_filter_;

  // Statistics (volatile for thread-safety on simple increments)
  volatile uint32_t rx_dropped_count_;
  volatile uint32_t tx_failed_count_;

  // TWAI driver configuration
  twai_timing_config_t timing_config_;
  twai_filter_config_t filter_config_;
};

#endif  // PROJECT_WAVESHARE_CAN_H_