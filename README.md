# LiveKit ESP32 Demos

Source code examples for the LiveKit ESP32 SDK blog post series. Each directory contains a self-contained project covering a specific topic, organized into progressive arcs.

## Beginner / Setup

| # | Directory | Description |
|---|-----------|-------------|
| 1 | [01-custom-hardware-quickstart](./01-custom-hardware-quickstart) | Run the LiveKit ESP32 SDK on non-reference and custom boards. Uses the Waveshare as a concrete example and walks through reading board wikis/specs, choosing between official BSPs and manual wiring. |
| 2 | [02-bsp-deep-dive](./02-bsp-deep-dive) | Modify or create a Board Support Package for niche boards. Covers peripheral initialization handles, when to reuse an existing Espressif BSP, and when to roll your own. |
| 3 | [03-breadboard-audio-pipeline](./03-breadboard-audio-pipeline) | Get to first audio on a plain ESP32 with an external amp and ADC on a breadboard. Aimed at product teams still in the prototyping stage. |
| 4 | [04-captive-portal-provisioning](./04-captive-portal-provisioning) | Device onboarding without hardcoded credentials. Implements a captive portal flow for Wi-Fi, LiveKit URL, and token configuration so you never need to recompile for a new network. |

## App

| # | Directory | Description |
|---|-----------|-------------|
| 5 | [05-walkie-talkie-ptt](./05-walkie-talkie-ptt) | Two ESP32 devices in one LiveKit room with push-to-talk via the boot button. A complete, clonable app people can adapt for their own use cases. |
| 6 | [06-aec-echo-cancellation](./06-aec-echo-cancellation) | Acoustic Echo Cancellation on the ESP32. Covers why AEC matters, pipeline setup, embedded constraints and tradeoffs, common gotchas, and tuning tips. |

## AI / Voice

| # | Directory | Description |
|---|-----------|-------------|
| 7 | [07-wake-word-hey-livekit](./07-wake-word-hey-livekit) | On-device wake word detection to trigger a LiveKit WebRTC session. Documents the journey from the stock "Hi ESP" model to a custom "Hey LiveKit" phrase using WakeNet9, including training data and model constraints. |

## Roadmap / Advanced

| # | Directory | Description |
|---|-----------|-------------|
| 8 | [08-data-packets-and-tracks](./08-data-packets-and-tracks) | Sending structured data over LiveKit from an ESP32. Covers what is available today with data packets and the migration path to data tracks once they ship in the ESP SDK. |
| 9 | [09-livekit-modem-sensor-bridge](./09-livekit-modem-sensor-bridge) | Use an ESP32 as a drop-in LiveKit modem — a self-contained module that handles WiFi, WebRTC, and the LiveKit protocol so any hardware project can send and receive audio, data, and sensor readings over LiveKit. Connect your project's MCU over UART/I2C/SPI and let it handle the cloud connectivity. |
