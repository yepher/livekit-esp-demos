# 04 – Captive Portal Provisioning

Device onboarding without hardcoded credentials. Implements a captive portal flow for WiFi, LiveKit URL, and API key/secret configuration so you never need to recompile for a new network. The device generates JWT access tokens on-device using HMAC-SHA256.

## Quick Start

```bash
cd code
idf.py build
idf.py flash monitor
```

On first boot, connect to the device's WiFi AP (e.g., `LiveKit-ESP-A3F7`) and open a browser to configure.

## What You'll Learn

- Setting up a captive portal on the ESP32 for device onboarding
- Collecting WiFi credentials and LiveKit API key/secret from a browser
- Generating JWT access tokens on-device (no pre-generated tokens)
- Persisting configuration to NVS so it survives reboots
- Using SD card `env` files for fleet/factory provisioning
- Building a reusable captive portal component for any ESP-IDF project

## Directory Structure

```
04-captive-portal-provisioning/
├── blog/
│   ├── post.md                     # Blog post
│   └── res/                        # Images and resources
└── code/
    ├── CMakeLists.txt
    ├── partitions.csv
    ├── sdkconfig.defaults
    ├── components/
    │   ├── esp_codec_dev/           # Local override (resolves BSP ↔ LiveKit version conflict)
    │   └── captive_portal/          # Reusable component
    │       ├── CMakeLists.txt
    │       ├── Kconfig
    │       ├── idf_component.yml
    │       ├── include/
    │       │   └── captive_portal.h  # Public API
    │       └── src/
    │           ├── portal_config.c   # NVS read/write
    │           ├── portal_ap.c       # Soft-AP + HTTP + DNS
    │           ├── portal_page.h     # Embedded HTML form
    │           ├── portal_wifi.c     # WiFi STA with retry
    │           └── portal_env.c      # env file parser
    └── main/
        ├── CMakeLists.txt
        ├── idf_component.yml
        ├── Kconfig.projbuild
        ├── main.c                    # Boot flow with portal
        ├── board.c / board.h         # BSP-based board init (from Post 02)
        ├── media.c / media.h         # Audio pipelines (from Post 02)
        └── jwt_generator.c / .h      # On-device JWT generation
```

## Key Files

| File | Purpose |
|------|---------|
| `components/captive_portal/include/captive_portal.h` | Public API — NVS config, portal start/stop, WiFi connect, env file loading |
| `components/captive_portal/src/portal_ap.c` | Soft-AP + HTTP server + DNS captive portal |
| `components/captive_portal/src/portal_page.h` | Embedded HTML form (dark theme, mobile-friendly) |
| `main/main.c` | Boot flow: NVS → portal or WiFi → NTP → JWT → LiveKit |
| `main/jwt_generator.c` | HMAC-SHA256 JWT generation via mbedTLS |

## What Changed from Post 02

| Aspect | Post 02 | Post 04 |
|--------|---------|---------|
| WiFi credentials | `sdkconfig.defaults` | Captive portal (NVS) |
| LiveKit auth | Pre-generated token | On-device JWT (API key + secret) |
| WiFi management | `livekit/example_utils` | `captive_portal` component |
| Recompile to change network? | Yes | No |
| Board init | BSP (same) | BSP (same) |
| Media pipeline | Same | Same |

## SD Card `env` File (Optional)

Format a MicroSD card as **FAT32**, then create a file called `env` in the root directory to pre-fill the portal form:

```bash
WIFI_SSID="your-wifi"
WIFI_PASSWORD="your-password"
LIVEKIT_URL="wss://your-project.livekit.cloud"
LIVEKIT_API_KEY="APIkey123"
LIVEKIT_API_SECRET="secret456"
LIVEKIT_ROOM="my-room-<RANDOM4>"
DEVICE_IDENTITY="kitchen-speaker"
```

The `<RANDOM4>` placeholder generates 4 random hex characters per device.
