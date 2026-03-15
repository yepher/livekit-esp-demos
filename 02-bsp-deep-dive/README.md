# 02 — BSP Deep Dive

Use the [Waveshare BSP](https://components.espressif.com/components/waveshare/esp32_s3_touch_lcd_1_83) to replace the ~300-line manual `board.c` from [Post 01](../01-custom-hardware-quickstart/) with about 30 lines. Same board, same LiveKit audio session, much less code. The [blog post](blog/post.md) also covers how to write your own BSP from scratch when a published one doesn't exist.

> **Detailed walkthrough:** See the [blog post](blog/post.md) for the full story — what a BSP is, how it's structured, when to use one vs. roll your own, and a step-by-step guide to writing a minimal BSP.

## Prerequisites

| What | Why |
|------|-----|
| [Waveshare ESP32-S3-Touch-LCD-1.83](https://www.waveshare.com/esp32-s3-touch-lcd-1.83.htm) | Same board as Post 01 |
| Small speaker with MX1.25 connector | Board has a header but no built-in speaker |
| [ESP-IDF 5.4+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html) | Build toolchain |
| [LiveKit Cloud](https://cloud.livekit.io) account | Room server (free tier works) |
| Python 3 + `livekit-api` (`pip install livekit-api`) | Token generation |
| USB-C cable | Flashing and serial monitor |

## Quick start

### 1. Clone and set up credentials

```bash
cd 02-bsp-deep-dive/code
cp sdkconfig.defaults.example sdkconfig.defaults
```

Edit `sdkconfig.defaults` and set your WiFi credentials (must be a **2.4 GHz** network):

```
CONFIG_LK_EXAMPLE_WIFI_SSID="your-wifi-ssid"
CONFIG_LK_EXAMPLE_WIFI_PASSWORD="your-wifi-password"
```

### 2. Generate LiveKit tokens

Create a file called `env` in the `code/` directory:

```
LIVEKIT_API_KEY=APIxxxxxxxxxxxx
LIVEKIT_API_SECRET=your-api-secret
LIVEKIT_URL=wss://your-project.livekit.cloud
```

Get these values from [LiveKit Cloud > Settings > Keys](https://cloud.livekit.io/projects/p_/settings/keys).

Run the token script:

```bash
python3 ../../../tools/make_test_token.py
```

The script prints a **sdkconfig.defaults snippet** — paste it into `sdkconfig.defaults`. It also prints a **User join URL** — save this for step 5.

### 3. Build and flash

```bash
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

> On macOS the port is typically `/dev/cu.usbmodem*` instead of `/dev/ttyACM0`.

### 4. Verify boot

You should see:

```
I (1023) board: Initializing Waveshare ESP32-S3-Touch-LCD-1.83 (via BSP)
I (1104) ES8311: Work in Slave mode
I (1114) ES7210: Work in Slave mode
I (1139) board: Board init complete (via BSP)
...
I (3200) livekit_example: Room state changed: CONNECTED
```

### 5. Join from your browser

Open the **User join URL** from step 2 in your browser. You'll join the same room as the ESP32 — speak into your mic and hear it through the speaker, and vice versa.

## Project structure

```
02-bsp-deep-dive/
├── README.md
├── blog/
│   ├── post.md                     # Detailed blog post walkthrough
│   └── res/                        # Images and diagrams
└── code/
    ├── CMakeLists.txt
    ├── partitions.csv
    ├── sdkconfig.defaults.example  # Template — copy to sdkconfig.defaults
    └── main/
        ├── board.c                 # BSP-based board init (~30 lines)
        ├── board.h
        ├── media.c                 # Audio capture + render pipeline (same as Post 01)
        ├── media.h
        ├── example.c               # LiveKit room connection (same as Post 01)
        ├── example.h
        ├── main.c                  # Entry point (same as Post 01)
        ├── Kconfig.projbuild       # Menuconfig options
        └── idf_component.yml       # BSP dependency replaces manual codec deps
```

## Key files

| File | What it does |
|------|-------------|
| [`board.c`](code/main/board.c) | Calls `bsp_audio_codec_speaker_init()` and `bsp_audio_codec_microphone_init()` — the BSP handles I2C, I2S, PMU, and codec register configuration internally. Exports the same `get_playback_handle()` and `get_record_handle()` API as Post 01. |
| [`idf_component.yml`](code/main/idf_component.yml) | Depends on `waveshare/esp32_s3_touch_lcd_1_83` instead of listing `es8311` and `es7210` individually. The BSP pulls them transitively. |
| [`sdkconfig.defaults.example`](code/sdkconfig.defaults.example) | Same as Post 01 plus `CONFIG_BSP_I2C_NUM=0` and `CONFIG_BSP_I2S_NUM=0` to override BSP defaults. |

## Key comparison

| | Post 01 (manual) | Post 02 (BSP) |
|---|---|---|
| `board.c` size | ~300 lines | ~30 lines |
| Pin definitions | Hardcoded `#define`s | Defined in BSP header |
| PMU init | Manual I2C register writes | Handled by BSP |
| Codec I2C addresses | Must know 8-bit format | BSP handles it |
| I2S mode setup | Manual STD TX + TDM RX | BSP configures both |
| Reusable across apps | Copy-paste `board.c` | One-line `idf_component.yml` dep |

## What changed from Post 01

Only two files differ:

1. **`board.c`** — replaced ~300 lines of manual init with BSP calls
2. **`idf_component.yml`** — replaced `espressif/es8311` + `espressif/es7210` with `waveshare/esp32_s3_touch_lcd_1_83`

Everything else — `main.c`, `media.c`, `example.c`, `board.h`, `Kconfig.projbuild`, `partitions.csv` — is identical. That's the power of the `board.h` API boundary.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Codec NACK errors | Add `CONFIG_BSP_I2C_NUM=0` to `sdkconfig.defaults`. Delete `sdkconfig` and rebuild. |
| Speaker silent | Check that speaker is connected. Some BSPs need `bsp_audio_poweramp_enable(true)`. |
| WiFi won't connect | ESP32-S3 only supports 2.4 GHz. Delete `sdkconfig` and rebuild after editing `sdkconfig.defaults`. |
| Binary too large | BSP may pull in display/LVGL dependencies. Increase factory partition or add `BSP_CONFIG_NO_GRAPHIC_LIB=1`. |
| Version conflict on `esp_codec_dev` | Change your version to `"*"` in `idf_component.yml` and let the BSP version win. |
