# 01 – Custom Hardware Quickstart

Run the LiveKit ESP32 SDK on non-reference and custom boards. Uses the Waveshare as a concrete example and walks through reading board wikis/specs, choosing between official BSPs and manual wiring.

## What You'll Learn

- How to identify the peripherals and pin mappings on a new board
- Reading a board's wiki, schematic, and datasheet to extract what you need
- Choosing between an official Espressif BSP and manual peripheral setup
- Getting a LiveKit session running on hardware that isn't in the examples folder

## Directory Structure

```
01-custom-hardware-quickstart/
├── blog/
│   ├── post.md       # Blog post
│   └── res/          # Images and resources
└── code/             # Source code
```


## Hardware

![ESP32 Device](blog/res/esp32_device.jpg)

As a reference we will use this ESP32 board.

| Component | Part |
|-----------|------|
| Board | [Waveshare ESP32-S3-Touch-LCD-1.83](https://www.waveshare.com/esp32-s3-touch-lcd-1.83.htm) |
| Wiki | [Waveshare ESP32-S3-Touch-LCD-1.83 Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.83) |

| # | Component | Description |
|---|-----------|-------------|
| 1 | ESP32-S3R8 | SoC with WiFi and Bluetooth, up to 240 MHz, with onboard 8 MB PSRAM |
| 2 | AXP2101 | Highly integrated power management IC |
| 3 | ES8311 | Low-power audio codec IC |
| 4 | ES7210 | ADC chip for echo cancellation circuits |
| 5 | MX1.25 speaker header | Non-polarized connector for external speaker |
| 6 | 1.2 mm lithium battery header | 2-pin connector for 3.7 V lithium battery, supports charging and discharging |
| 7 | Type-C port | ESP32-S3 USB port for flashing and log output |
| 8 | 16 MB NOR Flash | For storing data |
| 9 | Dual microphone array | Microphone input and echo cancellation |
| 10 | Onboard antenna | 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 (LE) |
| 11 | Reserved GPIO pads | Available I/O pins for easy expansion |
| 12 | QMI8658 | 6-axis IMU (3-axis gyroscope + 3-axis accelerometer) |
| 13 | PCF85063 | RTC chip |
| 14 | BOOT button | Device startup and functional debugging |
| 15 | PWR button | Power on/off, supports custom functions |
| 16 | 1.83" display panel connector | 240×284 ST7789P SPI LCD with CST816D capacitive touch (I2C) |
| 17 | Speaker amplifier chip | NS4150B class-D amplifier |
| 18 | TF card slot | MicroSD storage |
