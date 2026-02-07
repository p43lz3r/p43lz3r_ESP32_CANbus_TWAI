# WaveshareCAN Library for ESP32-S3

A CAN bus library for ESP32-S3 Waveshare boards. Built on ESP-IDF TWAI driver with FreeRTOS task management.

Version: 2.3.0  
Date: 03.02.2026  
Copyright: 2026 p43lz3r

## Hardware Compatibility

Tested and working:
- ESP32-S3-Touch-LCD-4.3B (RX=GPIO16, TX=GPIO15)

Expected to work (pin defaults configured):
- ESP32-S3-Touch-LCD-7.0B (RX=GPIO19, TX=GPIO20)

Custom GPIO pins supported via constructor arguments.

## Features

### Core CAN Functionality
Tested and working:
- Standard 11-bit CAN identifiers (send/receive verified)
- Polling and interrupt-driven reception modes
- Hardware acceptance filters
- Automatic bus-off recovery

Implemented but not extensively tested:
- Extended 29-bit CAN identifiers
- Listen-only mode
- Remote Transmission Request (RTR) frames

### Software Message Filtering (v2.2.0)
- Runtime configurable without bus restart
- Two modes: Monitoring (accept all) and Specific (filter by ID)
- Up to 5 configurable CAN IDs
- O(n) filter check where n <= 5
- Integrates with configuration manager for persistent settings

### Bitrate Configuration (v2.3.0)
- Runtime configurable CAN bitrate via Serial interface
- Supported: 125 kbps, 250 kbps, 500 kbps, 1000 kbps
- Persistent storage in ESP32 NVS flash
- Tested at 250 kbps and 500 kbps

### Production Features
- Thread-safe FreeRTOS task management
- 16-message deep queue for burst handling
- Drop counters and statistics
- Stack overflow monitoring
- Clean task shutdown

## Installation

Copy library files to your project:
```
project/
├── src/
│   ├── waveshare_can.h
│   ├── waveshare_can.cpp
│   ├── can_config_manager.h        (optional)
│   └── can_config_manager.cpp      (optional)
└── tools/
    └── can_config.py               (optional)
```

For PlatformIO, add ArduinoJson if using configuration manager:
```ini
lib_deps = 
    bblanchon/ArduinoJson@^7.0.0
```

## Basic Usage

### Polling Mode

```cpp
#include "waveshare_can.h"

WaveshareCan can(kBoard43b);

void setup() {
  Serial.begin(115200);
  can.Begin(kCan500Kbps);
}

void loop() {
  // Transmit
  uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  can.SendMessage(0x123, data, 4);
  
  // Receive
  if (can.Available() > 0) {
    uint32_t id;
    uint8_t rxData[8], len;
    can.ReceiveMessage(&id, nullptr, rxData, &len);
  }
  
  delay(100);
}
```

### Interrupt Mode

```cpp
#include "waveshare_can.h"

WaveshareCan can(kBoard43b);
volatile uint32_t msgCount = 0;

void onMessage(const twai_message_t& msg) {
  msgCount++;  // Keep callback fast
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
    Serial.printf("ID: 0x%X\n", id);
  }
  delay(10);
}
```

### Software Filtering

```cpp
#include "waveshare_can.h"

WaveshareCan can(kBoard43b);

void setup() {
  can.Begin(kCan500Kbps);
  
  // Accept only specific IDs
  uint32_t acceptedIds[] = {0x100, 0x200, 0x300};
  can.SetAcceptedIds(acceptedIds, 3, false);
  can.SetFilterMode(kFilterSpecific);
  
  can.EnableRxInterrupt(onMessage);
}
```

## Configuration System

Optional runtime configuration via Serial interface with NVS persistence.

### Python Tool

Upload configuration without reflashing firmware:

```bash
# Monitor mode - accept all IDs at 500 kbps (default)
python can_config.py --port /dev/ttyUSB0 --mode monitoring

# Specific mode with custom bitrate
python can_config.py --port /dev/ttyUSB0 --mode specific --ids 0x100 0x200 --bitrate 250000

# Extended 29-bit IDs at 1 Mbps
python can_config.py --port /dev/ttyUSB0 --mode specific --ids 0x12345678 --extended --bitrate 1000000
```

**Supported bitrates:**
- 125000 (125 kbps) - Automotive standard
- 250000 (250 kbps) - Industrial automation
- 500000 (500 kbps) - Default, most common
- 1000000 (1 Mbps) - High-speed CAN

### Integration Example

```cpp
#include "waveshare_can.h"
#include "can_config_manager.h"

WaveshareCan can(kBoard43b);
CanConfigManager config;

void setup() {
  Serial.begin(115200);
  
  // Load saved configuration
  config.LoadFromNVS();
  
  // Wait 15 seconds for new configuration via Serial
  config.WaitForConfig(15000);
  
  // Start CAN and apply filters (includes bitrate)
  config.ApplyToCanBus(&can);
  
  can.EnableRxInterrupt(onMessage);
}
```

Configuration persists across power cycles in ESP32 NVS flash. Includes filter mode, CAN IDs, and bitrate.

## API Reference

### Initialization

**WaveshareCan(BoardType board, int rx_pin, int tx_pin)**  
Create instance. Use -1 for default pins.

**bool Begin(twai_timing_config_t speed)**  
Initialize CAN bus. Available speeds: kCan125Kbps, kCan250Kbps, kCan500Kbps, kCan1000Kbps.  
Returns false on hardware failure.

**void End()**  
Stop CAN bus and release resources.

### Transmission

**bool SendMessage(uint32_t id, uint8_t\* data, uint8_t length)**  
Send standard 11-bit ID message.

**bool SendMessage(uint32_t id, bool extended, uint8_t\* data, uint8_t length, bool rtr)**  
Full control version. Set extended=true for 29-bit IDs.

### Reception - Polling

**int Available()**  
Number of messages in hardware queue.

**int ReceiveMessage(uint32_t\* id, bool\* extended, uint8_t\* data, uint8_t\* length, bool\* rtr)**  
Non-blocking receive. Returns bytes received or -1 if empty.

### Reception - Interrupt

**bool EnableRxInterrupt(void (\*callback)(const twai_message_t&))**  
Start background task. Callback fires on each message.

Callback constraints:
- Execution time under 1ms
- No Serial.print, delay, or blocking calls
- No memory allocation
- Use only ISR-safe FreeRTOS primitives

For complex processing, use ReceiveFromQueue() in main loop.

**void DisableRxInterrupt()**  
Stop background task and delete queue.

**int QueuedMessages()**  
Messages buffered in interrupt queue (0-16).

**int ReceiveFromQueue(uint32_t\* id, bool\* extended, uint8_t\* data, uint8_t\* length, bool\* rtr)**  
Non-blocking queue read. Returns -1 if empty.

### Filtering

**void SetFilterMode(FilterMode mode)**  
Set kFilterMonitoring (accept all) or kFilterSpecific (filter by ID).

**void SetAcceptedIds(const uint32_t\* ids, uint8_t count, bool extended)**  
Configure up to 5 accepted IDs for specific mode.

**bool Filter(uint32_t id, uint32_t mask, bool extended)**  
Hardware acceptance filter. Requires bus restart. For runtime filtering without restart, use SetAcceptedIds().

### Configuration Manager

**void LoadFromNVS()**  
Load saved configuration from flash (filter mode, IDs, bitrate).

**bool WaitForConfig(uint32_t timeout_ms)**  
Wait for JSON configuration via Serial. Returns true if config received.

**void ApplyToCanBus(WaveshareCan\* can)**  
Start CAN with configured bitrate and apply filters.

**uint32_t GetBitrate()**  
Get configured bitrate in bps.

### Monitoring

**bool GetStatus(twai_status_info_t\* status)**  
Get TWAI driver status including error counters and queue depths.

**uint32_t GetDroppedRxCount()**  
Messages lost due to queue overflow.

**uint32_t GetTxFailedCount()**  
Failed transmissions.

**TaskStats GetTaskStats()**  
Stack usage for interrupt tasks. Monitor for overflow (free should stay above 512 words).

**void ResetCounters()**  
Clear drop and failure counters.

### Alerts

**void OnAlert(void (\*callback)(uint32_t))**  
Register callback for CAN alerts.

**bool EnableAlertInterrupt(void (\*callback)(uint32_t))**  
Background alert monitoring. Same callback constraints as RX interrupt.

Alert flags include: TWAI_ALERT_BUS_OFF, TWAI_ALERT_ERR_PASS, TWAI_ALERT_RX_QUEUE_FULL, TWAI_ALERT_TX_FAILED.

## Technical Details

### Architecture
- ESP-IDF TWAI peripheral driver
- FreeRTOS tasks: RX (priority 5, 8KB stack), Alert (priority 4, 8KB stack)
- 16-message queue (twai_message_t = 13 bytes each)
- Hardware RX queue: 32 messages

### Memory Usage
Measured:
- Per interrupt task: 8KB stack allocation
- Queue buffer: 208 bytes (16 messages)
- Software filter: 24 bytes (5 IDs + metadata)
- NVS config blob: 32 bytes

### Performance
Tested and verified:
- Burst capacity: 50+ messages without loss at 500 kbps
- Zero dropped messages in stress test (100 messages)
- Software filter: O(n) where n <= 5
- Bitrate switching: Verified at 250 kbps and 500 kbps

### Error Recovery
- Automatic bus-off recovery via twai_initiate_recovery()
- Stack overflow detection via uxTaskGetStackHighWaterMark()
- Clean task shutdown with self-delete pattern

### NVS Storage Format
Configuration stored in 32-byte blob:
- Bytes 0-1: Mode and ID count
- Bytes 2-21: Five CAN IDs (20 bytes)
- Byte 22: Extended flag
- Bytes 23-26: Bitrate (uint32_t)
- Bytes 27-31: Reserved

## Troubleshooting

**Stack canary watchpoint triggered**  
Increase task stack size in header constants (currently 2048 words = 8KB).

**Messages dropped (GetDroppedRxCount > 0)**  
Process queue faster or reduce message rate. Enable interrupt mode if using polling.

**TX continuously fails**  
Check physical layer: CANH, CANL, GND connections and 120 ohm termination at both ends. Verify bus partner is acknowledging. Confirm bitrate matches network.

**Bus-off state**  
TX error counter exceeded 255. Usually indicates physical layer issue. Library auto-recovers but fix root cause.

**Configuration not persisting**  
Verify NVS partition in ESP32 flash. Check for sufficient free space. Use config.ClearNVS() to reset if corrupted.

**Bitrate mismatch**  
Ensure all CAN nodes use same bitrate. Use config tool to set correct bitrate. Supported: 125k, 250k, 500k, 1000k bps.

## Version History

### v2.3.0 (03.02.2026)
- Added runtime bitrate configuration (125k, 250k, 500k, 1000k)
- Extended NVS blob from 28 to 32 bytes
- ApplyToCanBus() now starts CAN with configured bitrate
- Tested at 250 kbps and 500 kbps

### v2.2.0 (02.02.2026)
- Added software-based message filtering
- Monitoring and Specific filter modes
- Python configuration tool with JSON protocol
- NVS persistence for filter configuration

### v2.1.0
- Interrupt-driven RX with 16-message queue
- Alert interrupt handling
- Task statistics and monitoring

### v2.0.0
- Initial release with polling and interrupt modes

## License

MIT License  
Copyright 2026 p43lz3r

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.