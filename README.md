# Waveshare ESP32-S3 CAN Library

This is a small, clean C++ library that lets an ESP32-S3 talk to a CAN bus using the built-in TWAI controller.

It was written for — and is mainly tested on — the Waveshare ESP32-S3-Touch-LCD-4.3B board.  
The library also works (with different pin defaults) on the 7-inch Touch-LCD version.

## Hardware

The library assumes you are using one of these two Waveshare boards:

- ESP32-S3-Touch-LCD-4.3B  
  CAN TX → GPIO 15  
  CAN RX → GPIO 16  
  Transceiver: TJA1051 or equivalent, direct connection, no expander pin control required

- ESP32-S3-Touch-LCD-7  
  CAN TX → GPIO 20  
  CAN RX → GPIO 19  
  (USB/CAN pin multiplexing controlled by expander EXIO5 — not handled here)

Both boards use a 120 Ω termination switch on the PCB. Make sure it is in the correct position for your bus topology.

No IO expander (CH422G) code is included. The CAN transceiver does not depend on it.

## Features

- Normal mode (sends ACK bits) or listen-only mode
- Transmit standard or extended frames, with or without RTR
- Non-blocking receive with simple polling interface
- Acceptance filter support (single filter mode)
- Automatic bus-off recovery attempt
- Basic alert handling (bus errors, queue full, TX fail, etc.)
- Increased RX queue length (32 messages)
- Pin defaults chosen automatically based on board type

## API Summary

```cpp
WaveshareCan can(kBoard43b);           // or kBoard7b

bool Begin(twai_timing_config_t speed = kCan500Kbps);
void End();

bool SendMessage(uint32_t id, bool ext, uint8_t* data, uint8_t len, bool rtr = false);
bool SendMessage(uint32_t id, uint8_t* data, uint8_t len);     // shortcut, standard, no RTR

int ReceiveMessage(uint32_t* id, bool* ext, uint8_t* data, uint8_t* len, bool* rtr);

void Filter(uint32_t id, uint32_t mask = 0);                   // re-inits driver

bool SetListenOnly(bool listen_only);
bool ProcessAlerts(uint32_t* alerts_triggered = nullptr);
void OnAlert(void (*callback)(uint32_t));

int Available();                                               // messages in RX queue
bool GetStatus(twai_status_info_t* status);
```

## Building & Wiring Notes

- Baudrate must match on both ends (library defaults to 500 kbit/s).
- Use a 120 Ω resistor at each physical end of the bus.
- Connect CAN_H ↔ CAN_H, CAN_L ↔ CAN_L, GND ↔ GND.
- If the board retransmits endlessly when receiving from another node → you are in listen-only mode or no other node is acknowledging.

## License
- MIT
