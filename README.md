# WaveshareCAN Library for ESP32-S3

A CAN bus library for ESP32-S3 Waveshare boards. Built for reliability, designed for real hardware.

## Hardware the library was successfully tested with

- **ESP32-S3-Touch-LCD-4.3B** (default: RX=GPIO16, TX=GPIO15)
- **ESP32-S3-Touch-LCD-7.0B** (default: RX=GPIO19, TX=GPIO20)

Custom pins? No problem - configure them in the constructor.

## Features

### Core Functionality
- **Standard (11-bit) and Extended (29-bit) CAN IDs** - Full support, properly implemented
- **Polling and Interrupt Modes** - Your choice. Polling for simple stuff, interrupts for performance
- **Listen-Only Mode** - Monitor the bus without ACKing. Perfect for sniffing
- **Acceptance Filters** - Hardware filtering by ID. Don't waste CPU on messages you don't care about
- **RTR Frame Support** - Remote transmission requests, because sometimes you need them

### Production-Ready Features
- **Thread-Safe Operations** - FreeRTOS tasks, mutexes, the works. No race conditions
- **Background Message Buffering** - 16-deep queue so you don't lose messages during processing
- **Burst Handling** - Drains multiple messages per interrupt. Tested with 50+ message bursts
- **Automatic Bus-Off Recovery** - Hardware error? Library handles it. Back online automatically
- **Drop Counters** - Know exactly how many messages you missed and why
- **Stack Monitoring** - Real-time task health. Know before things break
- **Clean Shutdown** - Tasks exit gracefully. No orphaned resources, no corruption

### Monitoring & Diagnostics
- **Alert System** - Bus errors, queue full, TX failures - you know immediately
- **Status Reporting** - Error counters, queue depth, bus state - complete visibility
- **Task Statistics** - Stack usage per task. Catch issues before they become crashes

## Installation

1. Copy `waveshare_can.h` and `waveshare_can.cc` to your Arduino libraries folder
2. Include in your sketch: `#include "waveshare_can.h"`
3. Done

## Quick Start

### Basic Example - Polling Mode

```cpp
#include "waveshare_can.h"

WaveshareCan can(kBoard43b);  // 4.3" board

void setup() {
  Serial.begin(115200);
  
  if (!can.Begin(kCan500Kbps)) {
    Serial.println("CAN init failed!");
    while(1);
  }
  
  Serial.println("CAN running at 500 kbps");
}

void loop() {
  // Send
  uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  can.SendMessage(0x123, data, 4);
  
  // Receive
  if (can.Available() > 0) {
    uint32_t id;
    uint8_t rxData[8], len;
    
    if (can.ReceiveMessage(&id, nullptr, rxData, &len) >= 0) {
      Serial.printf("RX: ID=0x%X, Len=%d\n", id, len);
    }
  }
  
  delay(100);
}
```

### Interrupt Mode - For Performance

```cpp
#include "waveshare_can.h"

WaveshareCan can(kBoard43b);
volatile uint32_t msgCount = 0;

void onMessage(const twai_message_t& msg) {
  msgCount++;  // Keep it fast - no Serial.print here!
}

void setup() {
  Serial.begin(115200);
  can.Begin(kCan500Kbps);
  can.EnableRxInterrupt(onMessage);
}

void loop() {
  // Process queued messages
  while (can.QueuedMessages() > 0) {
    uint32_t id;
    uint8_t data[8], len;
    
    can.ReceiveFromQueue(&id, nullptr, data, &len);
    Serial.printf("ID=0x%X\n", id);
  }
  
  delay(10);
}
```

## API Reference

### Initialization

```cpp
WaveshareCan can(BoardType board = kBoard43b, int rx_pin = -1, int tx_pin = -1);
```
Create instance. Custom pins override board defaults.

```cpp
bool Begin(twai_timing_config_t speed = kCan500Kbps);
```
Start CAN. Available speeds: 5K to 1M bps. Returns false if hardware init fails.

```cpp
void End();
```
Stop and cleanup. Disables interrupts, uninstalls driver. Safe to call anytime.

### Transmit

```cpp
bool SendMessage(uint32_t id, uint8_t* data, uint8_t length);
```
Send standard ID message. Returns false if TX buffer full or in listen-only mode.

```cpp
bool SendMessage(uint32_t id, bool extended, uint8_t* data, uint8_t length, bool rtr = false);
```
Full control: extended IDs, RTR frames. Use this for 29-bit IDs.

### Receive - Polling

```cpp
int Available();
```
How many messages waiting in hardware queue. Returns 0 if none.

```cpp
int ReceiveMessage(uint32_t* id, bool* extended, uint8_t* data, uint8_t* length, bool* rtr = nullptr);
```
Non-blocking receive. Returns bytes received or -1 if queue empty.

### Receive - Interrupt Mode

```cpp
bool EnableRxInterrupt(void (*callback)(const twai_message_t&) = nullptr);
```
Start background RX task. Messages buffered in queue, callback fires on each message.

**Callback Rules:**
- Executes in FreeRTOS task context
- Keep under 1ms
- NO Serial.print, NO delays, NO blocking
- Set flags, increment counters, toggle GPIOs only
- Complex processing? Use ReceiveFromQueue() in main loop

```cpp
void OnReceive(void (*callback)(const twai_message_t&));
```
Change callback during runtime. Pass nullptr to disable callback (messages still queued).

```cpp
void DisableRxInterrupt();
```
Stop RX task. Queue deleted, messages lost. Clean shutdown guaranteed.

```cpp
int QueuedMessages();
```
Messages waiting in interrupt queue (16 max). Check before ReceiveFromQueue().

```cpp
int ReceiveFromQueue(uint32_t* id, bool* extended, uint8_t* data, uint8_t* length, bool* rtr = nullptr);
```
Non-blocking queue read. Returns -1 if empty. Use in main loop for heavy processing.

### Filters

```cpp
bool Filter(uint32_t id, uint32_t mask = 0, bool extended = false);
```
Hardware acceptance filter. Re-initializes driver.

Examples:
- `Filter(0x200, 0x7FF, false)` - Accept only standard ID 0x200
- `Filter(0x100, 0x700, false)` - Accept 0x100-0x1FF (mask lower 8 bits)
- `Filter(0x12345678, 0x1FFFFFFF, true)` - Accept only extended 0x12345678
- `Filter(0, 0, false)` - Accept all

### Alerts

```cpp
bool ProcessAlerts(uint32_t* alerts_triggered = nullptr);
```
Check and handle CAN alerts. Call in loop for polling mode. Returns true if alerts fired.

```cpp
void OnAlert(void (*callback)(uint32_t));
```
Register alert callback. Fires on bus errors, queue full, TX failures.

```cpp
bool EnableAlertInterrupt(void (*callback)(uint32_t) = nullptr);
```
Background alert handling. Same callback rules as RX interrupt.

```cpp
void DisableAlertInterrupt();
```
Stop alert task.

Alert flags (bitwise OR):
- `TWAI_ALERT_BUS_OFF` - Too many errors, bus disabled
- `TWAI_ALERT_BUS_RECOVERED` - Recovered from bus-off
- `TWAI_ALERT_ERR_PASS` - Error passive (>127 errors)
- `TWAI_ALERT_BUS_ERROR` - Bus error detected
- `TWAI_ALERT_RX_QUEUE_FULL` - Hardware queue overflow
- `TWAI_ALERT_TX_FAILED` - Transmission failed

### Status & Monitoring

```cpp
bool GetStatus(twai_status_info_t* status);
```
Complete TWAI driver status. State, error counters, queue depths.

```cpp
bool SetListenOnly(bool listen_only);
```
Toggle listen-only mode. Re-initializes driver.

```cpp
TaskStats GetTaskStats() const;
```
Stack usage for RX and Alert tasks. Monitor for stack overflow.

```cpp
uint32_t GetDroppedRxCount() const;
```
Messages lost due to queue full. Reset with ResetCounters().

```cpp
uint32_t GetTxFailedCount() const;
```
Failed transmissions. Usually means no bus partner or bus-off.

```cpp
void ResetCounters();
```
Clear drop and TX fail counters.

## Advanced Usage

### Custom Pins

```cpp
WaveshareCan can(kBoard43b, 10, 11);  // Custom RX=10, TX=11
```

### Extended IDs with Filters

```cpp
// Accept only extended ID 0x12345000 to 0x123450FF
can.Filter(0x12345000, 0x1FFFFF00, true);
```

### Production Monitoring

```cpp
void loop() {
  // Check for dropped messages
  if (can.GetDroppedRxCount() > 0) {
    Serial.printf("WARNING: %lu messages dropped!\n", can.GetDroppedRxCount());
    can.ResetCounters();
  }
  
  // Monitor stack health
  WaveshareCan::TaskStats stats = can.GetTaskStats();
  if (stats.rx_stack_free < 1024) {  // Less than 1KB free
    Serial.println("WARNING: RX task stack running low!");
  }
}
```

### Bus Error Handling

```cpp
void onAlert(uint32_t alerts) {
  if (alerts & TWAI_ALERT_BUS_OFF) {
    // Bus-off detected - library automatically attempts recovery
    Serial.println("Bus-off! Waiting for recovery...");
  }
  
  if (alerts & TWAI_ALERT_BUS_RECOVERED) {
    Serial.println("Bus recovered!");
  }
}

void setup() {
  can.Begin(kCan500Kbps);
  can.EnableAlertInterrupt(onAlert);
}
```

## Technical Details

### Architecture
- Built on ESP-IDF TWAI driver (Two-Wire Automotive Interface)
- FreeRTOS tasks for interrupt handlers (8KB RX stack, 8KB Alert stack)
- Thread-safe with proper task synchronization
- Clean shutdown via task self-deletion pattern

### Memory Usage
- ~2KB RAM baseline
- +8KB per interrupt task when enabled
- 16-message queue buffer (16 × sizeof(twai_message_t))

### Performance
- Interrupt latency: <100μs
- Burst handling: 50+ messages without loss
- Background processing: RX task priority 5, Alert task priority 4

### Error Recovery
- Automatic bus-off recovery via `twai_initiate_recovery()`
- Error passive detection and reporting
- TX retry on NACK (hardware handles retransmission)

## Troubleshooting

**"Stack canary watchpoint triggered"**
- Increase task stack size in header (currently 8KB per task)
- Check callback complexity - keep under 1ms

**Messages dropped (GetDroppedRxCount > 0)**
- Process queue faster in main loop
- Reduce message rate
- Enable RX interrupt if using polling

**TX fails continuously**
- Check physical connections (CANH, CANL, GND)
- Verify bus termination (120Ω at each end)
- Confirm bus partner is ACKing
- Check bitrate matches network

**Bus-off state**
- Too many errors (TX error counter > 255)
- Usually physical layer issue
- Library auto-recovers, but fix root cause

## License

Copyright 2026 p43lz3r

Released under MIT License.