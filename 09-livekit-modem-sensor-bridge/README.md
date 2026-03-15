# 09 – LiveKit Modem: a Sensor Bridge

Use an ESP32 as a drop-in **LiveKit modem** — a self-contained module that handles WiFi, WebRTC, and the LiveKit protocol so any hardware project can send and receive audio, data, and sensor readings over LiveKit without implementing networking itself. Connect your project's MCU to the ESP32 over UART, I2C, or SPI, and let it handle the cloud connectivity.

## What You'll Learn

- Treating the ESP32 as a "modem" — a networking coprocessor for other hardware
- Defining a simple serial protocol (UART/I2C/SPI) between a host MCU and the ESP32
- Bridging sensor data and commands across LiveKit data channels
- Publishing and subscribing to sensor readings in real time over a LiveKit session
- Design considerations for latency, bandwidth, and data format
- How to drop this module into existing hardware projects

## Directory Structure

```
09-livekit-modem-sensor-bridge/
├── blog/
│   ├── post.md       # Blog post
│   └── res/          # Images and resources
└── code/             # Source code
```
