# 01 — Custom Hardware Quickstart

Build a hardware frontend for [LiveKit Agents](https://docs.livekit.io/agents/) using the [LiveKit ESP32 SDK](https://github.com/livekit/client-sdk-esp32) on a board that isn't in the examples folder. This project uses the **Waveshare ESP32-S3-Touch-LCD-1.83** as a concrete example — it's affordable (~$16), easy to source, and uses the same ES8311 + ES7210 codec pair as Espressif's reference boards, making it a great starting point for custom products. The process works for any ESP32-S3 board with I2S audio codecs.

> **Detailed walkthrough:** See the [blog post](blog/post.md) for the full story — reading schematics, understanding the codec init chain, and troubleshooting I2C issues.

## Prerequisites

| What | Why |
|------|-----|
| [Waveshare ESP32-S3-Touch-LCD-1.83](https://www.waveshare.com/esp32-s3-touch-lcd-1.83.htm) | Target board (or substitute your own) |
| Small speaker with MX1.25 connector | Board has a speaker header but no built-in speaker |
| [ESP-IDF 5.4+](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html) | Build toolchain |
| [LiveKit Cloud](https://cloud.livekit.io) account | Room server (free tier works) |
| Python 3 + `livekit-api` (`pip install livekit-api`) | Token generation |
| USB-C cable | Flashing and serial monitor |

## Quick start

### 1. Clone and set up credentials

```bash
cd 01-custom-hardware-quickstart/code
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
python3 ../../tools/make_test_token.py
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
I (1023) board: Initializing Waveshare ESP32-S3-Touch-LCD-1.83
I (1049) board: AXP2101: ALDO1 enabled at 3.3 V (codec power)
I (1104) ES8311: Work in Slave mode
I (1114) ES7210: Work in Slave mode
I (1139) board: Board init complete
...
I (3200) livekit_example: Room state changed: CONNECTED
```

### 5. Test from your browser

Open the **User join URL** from step 2 in your browser. You'll join the same room as the ESP32 — speak into your mic and hear it through the speaker, and vice versa.

### 6. Connect an agent (optional)

Any [LiveKit Agent](https://docs.livekit.io/agents/quickstarts/voice-agent/) that joins the same room will automatically exchange audio with the ESP32. Follow the Voice Agent Quickstart to get an agent running — no changes needed on the device side.

To end the session, press the reset button or power off the ESP32.

## Project structure

```
01-custom-hardware-quickstart/
├── README.md
├── blog/
│   ├── post.md                     # Detailed blog post walkthrough
│   └── res/                        # Schematic screenshots and photos
└── code/
    ├── CMakeLists.txt
    ├── partitions.csv
    ├── sdkconfig.defaults.example  # Template — copy to sdkconfig.defaults
    └── main/
        ├── board.c                 # Board init (I2C, PMU, I2S, codecs)
        ├── board.h
        ├── media.c                 # Audio capture + render pipeline
        ├── media.h
        ├── example.c               # LiveKit room connection
        ├── example.h
        ├── main.c                  # Entry point
        ├── Kconfig.projbuild       # Menuconfig options
        └── idf_component.yml       # Component dependencies
```

## Key files

| File | What it does |
|------|-------------|
| [`board.c`](code/main/board.c) | Initializes I2C bus, AXP2101 PMU, I2S channels, ES8311 (speaker), ES7210 (microphone). Exports `get_playback_handle()` and `get_record_handle()`. |
| [`media.c`](code/main/media.c) | Builds the audio capture system (with AEC) and render system using the codec handles from `board.c`. |
| [`example.c`](code/main/example.c) | Creates and connects to a LiveKit room. Publishes microphone audio and subscribes to incoming audio. |
| [`sdkconfig.defaults.example`](code/sdkconfig.defaults.example) | Template config with WiFi, LiveKit, PSRAM, and flash settings for this board. |

## Adapting to your own board

1. Read your board's schematic to find: I2S pins, I2C pins, codec addresses, PA enable pin, PMU (if any)
2. Edit `board.c` with your pin assignments and codec addresses
3. Everything else (media pipeline, room connection, LiveKit SDK) stays the same

See the [blog post](blog/post.md) for a detailed guide on reading schematics and debugging I2C issues.

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| I2C NACK errors during codec init | Use 8-bit I2C addresses (e.g. `0x30` not `0x18`) — see [blog post](blog/post.md#troubleshooting) |
| Speaker silent | Check PA enable pin (GPIO 46). Ensure speaker is connected to MX1.25 header |
| WiFi won't connect | ESP32-S3 only supports 2.4 GHz. Delete `sdkconfig` and rebuild after editing `sdkconfig.defaults` |
| No microphone audio | ES7210 needs TDM mode, not standard I2S |
