# 05 – Walkie-Talkie (Push-to-Talk)

Push-to-talk audio on the ESP32. The microphone starts muted; press and hold the boot button to transmit. This solves the echo-feedback problem when talking to AI agents (the agent hears its own speaker output and starts responding to itself) and also works as a device-to-device walkie-talkie.

## Quick Start

```bash
cd code
idf.py build
idf.py flash monitor
```

On first boot, connect to the device's WiFi AP (e.g., `LiveKit-ESP-A3F7`) and configure via the captive portal (same as Post 04).

## What You'll Learn

- Why push-to-talk matters (echo feedback without AEC)
- Using the boot button as a GPIO interrupt with debounce
- Hardware-level mic muting via `esp_codec_dev_set_in_mute()`
- Joining two ESP32 devices to the same LiveKit room as walkie-talkies
- Building a clean PTT module that's independent of the LiveKit SDK

## Directory Structure

```
05-walkie-talkie-ptt/
├── blog/
│   ├── post.md                     # Blog post
│   └── res/                        # Images and resources
└── code/
    ├── CMakeLists.txt
    ├── partitions.csv
    ├── sdkconfig.defaults
    ├── components/
    │   ├── esp_codec_dev/           # Local override (resolves BSP ↔ LiveKit version conflict)
    │   └── captive_portal/          # Reusable component (from Post 04)
    │       ├── include/
    │       │   └── captive_portal.h
    │       └── src/
    │           ├── portal_config.c
    │           ├── portal_ap.c
    │           ├── portal_page.h
    │           ├── portal_wifi.c
    │           └── portal_env.c
    └── main/
        ├── CMakeLists.txt
        ├── idf_component.yml
        ├── Kconfig.projbuild
        ├── main.c                    # Boot flow with portal + PTT
        ├── ptt.c / ptt.h            # Push-to-talk button module (NEW)
        ├── board.c / board.h         # BSP-based board init (from Post 02)
        ├── media.c / media.h         # Audio pipelines (from Post 02)
        └── jwt_generator.c / .h      # On-device JWT generation (from Post 04)
```

## Key Files

| File | Purpose |
|------|---------||
| `main/ptt.c` / `ptt.h` | PTT button: GPIO interrupt + 50ms debounce + codec mute toggle |
| `main/main.c` | Boot flow: portal → WiFi → NTP → JWT → room → PTT init on connect |
| `components/captive_portal/` | WiFi + LiveKit credential provisioning (reused from Post 04) |

## What Changed from Post 04

| Aspect | Post 04 | Post 05 |
|--------|---------|---------|
| Mic state | Always on | Muted by default (PTT) |
| Boot button | Unused | Push-to-talk trigger |
| New files | — | `ptt.c`, `ptt.h` |
| Main loop | Keep-alive only | Reports PTT state |
| Use case | Always-on audio | Walkie-talkie / agent interaction |

## Multi-Device Walkie-Talkie

Flash the same firmware onto two boards. Give each a different device identity via the captive portal but the same room name. Hold the boot button on one device and speak — the other device plays the audio through its speaker.

## SD Card `env` File (Optional)

Format a MicroSD card as **FAT32**, then create a file called `env` in the root directory to pre-fill the portal form:

```bash
WIFI_SSID="your-wifi"
WIFI_PASSWORD="your-password"
LIVEKIT_URL="wss://your-project.livekit.cloud"
LIVEKIT_API_KEY="APIkey123"
LIVEKIT_API_SECRET="secret456"
LIVEKIT_ROOM="walkie-room-<RANDOM4>"
DEVICE_IDENTITY="walkie-1"
```
