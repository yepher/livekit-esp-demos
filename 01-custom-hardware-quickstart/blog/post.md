# Running the LiveKit ESP32 SDK on Custom Hardware

The LiveKit ESP32 SDK ships with examples that target specific reference boards like the ESP32-S3-BOX-3 and ESP-Korvo-2. If you're building a product on your own hardware — or evaluating on a dev board that isn't in the examples — you need to know how to adapt the SDK to your pin configuration and audio codecs.

This post walks through the process end-to-end using the [Waveshare ESP32-S3-Touch-LCD-1.83](https://www.waveshare.com/esp32-s3-touch-lcd-1.83.htm) as a concrete example. The board does have a published BSP, but we intentionally configure it manually to demonstrate the process for any board.

## What you'll need

- [Waveshare ESP32-S3-Touch-LCD-1.83](https://www.waveshare.com/esp32-s3-touch-lcd-1.83.htm) (or your own board)
- ESP-IDF 5.4 or later
- A LiveKit Cloud account (free sandbox) or self-hosted LiveKit server
- USB-C cable

## Meet the board

![ESP32 Board](res/ESP32-S3-Touch-LCD-1.83-intro.jpg)

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

The ES8311 and ES7210 are the same codec pair used on the ESP32-S3-BOX-3 and Korvo-2 reference boards. Only the GPIO pin assignments differ — which is exactly the problem we need to solve.

## Step 1: Extract pin assignments from the schematic

Download the board [schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.83/ESP32-S3-Touch-LCD-1.83-schematic.pdf) from the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.83). The schematic is organized into labeled blocks — the ones we care about are **Codec** (ES8311), **ADC** (ES7210), and **PA & Speaker & MIC**. It also includes a GPIO allocation table that maps every ESP32-S3 pin to its function across all peripherals.

### Reading the Codec block (ES8311)

Open the **Codec** section and look at the bottom of the ES8311 (U9). The GPIO assignments are labeled directly on the signal traces:

![ES8311 Codec Schematic](res/schematic-codec.png)

- `GPIO16` → `I2S_MCLK` → ES8311 pin 2 (MCLK)
- `GPIO9` → `I2S_SCLK` → ES8311 pin 6 (SCLK)
- `GPIO45` → `I2S_LRCK` → ES8311 pin 8 (LRCK)
- `GPIO8` → `I2S_DSDIN` → ES8311 pin 9 (DSDIN — data *into* the DAC for playback)

At the top, the I2C control lines are labeled:

- `GPIO15` → `ESP32_SDA` → ES8311 pin 19 (CDATA)
- `GPIO14` → `ESP32_SCL` → ES8311 pin 1 (CCLK)

The CE pin is pulled high through R32 (10 kΩ), which sets the ES8311 I2C address to `0x18`.

### Reading the ADC block (ES7210)

Now look at the **ADC** section for the ES7210 (U12):

![ES7210 ADC Schematic](res/schematic-adc.png)

The ES7210 shares the same I2S bus — `I2S_MCLK`, `I2S_SCLK`, and `I2S_LRCK` connect to pins 5, 9, and 10. The recording data line is:

- `GPIO10` → `I2S_ASDOUT` → ES7210 pin 11 (SDOUT1/TDMOUT — data *out of* the ADC to the ESP32)

The I2C control uses the same bus (`ESP32_SCL`/`ESP32_SDA` on GPIO14/15). The AD0 and AD1 pins are tied to ground, giving an I2C address of `0x40`.

### Cross-referencing with the GPIO table

The schematic includes a GPIO allocation table that maps every ESP32-S3 pin to its function across all peripherals. This is the fastest way to verify your readings:

![GPIO Allocation Table](res/schematic-gpio-table.png)

| ESP32-S3 | Function | Peripheral |
|----------|----------|------------|
| GPIO8 | I2S_DSDIN | ES8311 (playback data) |
| GPIO9 | I2S_SCLK | ES8311/ES7210 (bit clock) |
| GPIO10 | I2S_ASDOUT | ES7210 (recording data) |
| GPIO14 | ESP32_SCL | I2C clock (codecs, touch, RTC, IMU) |
| GPIO15 | ESP32_SDA | I2C data (codecs, touch, RTC, IMU) |
| GPIO16 | I2S_MCLK | ES8311/ES7210 (master clock) |
| GPIO45 | I2S_LRCK | ES8311/ES7210 (word select) |
| GPIO46 | PA_CTRL | Speaker amplifier enable |

Note that GPIO14/15 serve multiple peripherals on the same I2C bus — the touch controller, RTC, IMU, and both audio codecs all share it. Each device has a unique I2C address, so they coexist without conflict.

### Summary of pin assignments

**I2S bus:**

| Signal | GPIO | Direction | Description |
|--------|------|-----------|-------------|
| `I2S_MCLK` | 16 | Out | Master clock to both codecs |
| `I2S_SCLK` | 9 | Out | Bit clock (BCLK) |
| `I2S_LRCK` | 45 | Out | Word select (WS) |
| `I2S_DSDIN` | 8 | Out | Serial data from ESP32 to ES8311 (playback) |
| `I2S_ASDOUT` | 10 | In | Serial data from ES7210 to ESP32 (recording) |

**I2C bus:**

| Signal | GPIO |
|--------|------|
| `ESP32_SCL` | 14 |
| `ESP32_SDA` | 15 |

**I2C addresses:**

| Device | Address | How |
|--------|---------|-----|
| ES8311 | `0x18` | CE pin pulled high via 10 kΩ |
| ES7210 | `0x40` | AD0 = AD1 = 0 |

**Speaker amplifier:**

| Signal | GPIO |
|--------|------|
| `PA_CTRL` | 46 |

The NS4150B class-D amplifier is enabled when this pin goes high. If you forget to drive this pin, the speaker stays silent — a common gotcha on boards with external PA circuits.

## Step 2: Initialize the hardware

The SDK's reference examples use a `codec_board` component that reads board configs from a text file. That works well for supported boards, but it hides the initialization sequence. When porting to custom hardware, it's more reliable to initialize each peripheral directly.

### I2C

```c
#define BOARD_I2C_SDA  GPIO_NUM_15
#define BOARD_I2C_SCL  GPIO_NUM_14

static esp_err_t init_i2c(void)
{
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_NUM_0,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_io_num = BOARD_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &i2c_bus);
}
```

### I2S

The ES8311 (playback) uses standard I2S mode. The ES7210 (recording) uses TDM mode with 4 slots — one per microphone channel. Both share the same I2S port but use separate TX and RX channels:

```c
#define BOARD_I2S_MCLK GPIO_NUM_16
#define BOARD_I2S_BCLK GPIO_NUM_9
#define BOARD_I2S_WS   GPIO_NUM_45
#define BOARD_I2S_DOUT GPIO_NUM_8
#define BOARD_I2S_DIN  GPIO_NUM_10

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_new_channel(&chan_cfg, &i2s_tx, &i2s_rx);

    // TX: standard mode for ES8311 playback
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                        32, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws   = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,
            .din  = BOARD_I2S_DIN,
        },
    };
    i2s_channel_init_std_mode(i2s_tx, &std_cfg);

    // RX: TDM mode for ES7210 4-channel recording
    i2s_tdm_slot_mask_t slot_mask =
        I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
        I2S_TDM_SLOT2 | I2S_TDM_SLOT3;
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg  = I2S_TDM_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
                        32, I2S_SLOT_MODE_STEREO, slot_mask),
        .gpio_cfg = { /* same pins */ },
    };
    tdm_cfg.slot_cfg.total_slot = 4;
    i2s_channel_init_tdm_mode(i2s_rx, &tdm_cfg);

    // Enable channels — some codecs need the clock running
    // during register configuration
    i2s_channel_enable(i2s_tx);
    i2s_channel_enable(i2s_rx);
    return ESP_OK;
}
```

### ES8311 (DAC — speaker output)

[datasheet](http://www.everest-semi.com/pdf/ES8311%20PB.pdf)

```c
#define BOARD_PA_PIN  GPIO_NUM_46
#define ES8311_ADDR   0x18

static esp_err_t init_es8311(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = I2C_NUM_0,
        .bus_handle = i2c_bus,
        .addr       = ES8311_ADDR,
    };
    const audio_codec_ctrl_if_t *ctrl =
        audio_codec_new_i2c_ctrl(&i2c_cfg);

    const audio_codec_gpio_if_t *gpio = audio_codec_new_gpio();

    es8311_codec_cfg_t codec_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .ctrl_if    = ctrl,
        .gpio_if    = gpio,
        .pa_pin     = BOARD_PA_PIN,
        .use_mclk   = true,
        .hw_gain    = { .pa_gain = 6.0 },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&codec_cfg);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port      = I2S_NUM_0,
        .tx_handle = i2s_tx,
        .rx_handle = i2s_rx,
    };
    const audio_codec_data_if_t *data =
        audio_codec_new_i2s_data(&i2s_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec,
        .data_if  = data,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    play_dev = esp_codec_dev_new(&dev_cfg);
    esp_codec_dev_set_out_vol(play_dev, 85);
    return ESP_OK;
}
```

### ES7210 (ADC — microphone input)

[datasheet](http://www.everest-semi.com/pdf/ES7210%20PB.pdf)

```c
#define ES7210_ADDR 0x40

static esp_err_t init_es7210(void)
{
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = I2C_NUM_0,
        .bus_handle = i2c_bus,
        .addr       = ES7210_ADDR,
    };
    const audio_codec_ctrl_if_t *ctrl =
        audio_codec_new_i2c_ctrl(&i2c_cfg);

    es7210_codec_cfg_t codec_cfg = {
        .ctrl_if      = ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 |
                        ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    const audio_codec_if_t *codec = es7210_codec_new(&codec_cfg);

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port      = I2S_NUM_0,
        .rx_handle = i2s_rx,
    };
    const audio_codec_data_if_t *data =
        audio_codec_new_i2s_data(&i2s_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = codec,
        .data_if  = data,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    rec_dev = esp_codec_dev_new(&dev_cfg);
    esp_codec_dev_set_in_gain(rec_dev, 30.0);
    return ESP_OK;
}
```

### Putting it together

```c
void board_init(void)
{
    init_i2c();
    init_i2s();
    init_es8311();
    init_es7210();
}
```

The full `board.c` is in the [code directory](../code/main/board.c).

## Step 3: Wire up the media pipeline

With the codec handles ready, the media pipeline is identical to any other LiveKit ESP32 project. The capture system reads from the ES7210 (with AEC enabled), and the render system plays through the ES8311:

```c
int media_init(void)
{
    esp_audio_enc_register_default();
    esp_audio_dec_register_default();

    // Capture: ES7210 → AEC → LiveKit
    esp_capture_audio_aec_src_cfg_t aec_cfg = {
        .record_handle = get_record_handle(),
        .channel = 4,
        .channel_mask = 1 | 2,
    };
    audio_source = esp_capture_new_audio_aec_src(&aec_cfg);

    // Render: LiveKit → ES8311 → speaker
    i2s_render_cfg_t i2s_cfg = {
        .play_handle = get_playback_handle(),
    };
    audio_renderer = av_render_alloc_i2s_render(&i2s_cfg);

    // ... (full code in code/main/media.c)
}
```

## Step 4: Connect to LiveKit

The room connection logic is standard — it doesn't depend on the board at all:

```c
void app_main(void)
{
    livekit_system_init();
    board_init();    // Our custom board init
    media_init();

    // Network + SNTP
    lk_example_network_connect();

    // Join a LiveKit room
    join_room();
}
```

## Step 5: Configure, build, and flash

This section walks through the full process: getting LiveKit credentials, generating tokens for the ESP32 and for you (the user), wiring the device config, then building and joining the room.

### 5.1 Get your LiveKit credentials

Sign in to [LiveKit Cloud](https://cloud.livekit.io) and open your project’s **API keys** page:

**https://cloud.livekit.io/projects/p_/settings/keys**

Create or copy an API key and secret. You also need your project’s WebSocket URL (e.g. `wss://your-project.livekit.cloud`). These are the values the device and the token script need to log in and create tokens.

### 5.2 Create a local env file and generate tokens

From the demo repo (e.g. the `livekit-esp-demos` root or the `tools` directory), create an `env` file in the **current directory** with your LiveKit credentials. You can use a file named `./env` or set the same variables in your environment (environment variables override the file).

Create `./env` with:

```
LIVEKIT_API_KEY=your-api-key
LIVEKIT_API_SECRET=your-api-secret
LIVEKIT_URL=wss://your-project.livekit.cloud
LIVEKIT_ROOM=esp32Room
```

`LIVEKIT_ROOM` is optional; it defaults to `esp32Room` if omitted.

Then run the token script (from the directory that contains `./env`, or with the vars set in your shell):

```bash
python3 tools/make_test_token.py
```

The script prints:

1. **ESP32 token** — the token the device uses to join the room (identity `"ESP32"`).
2. **User token** — a token for you to join the same room (identity `"User"`).
3. **User join URL** — a ready-to-open link:  
   `https://meet.livekit.io/custom?liveKitUrl=...&token=...`  
   Open this in a browser to join the room and talk to the ESP32/agent.
4. **sdkconfig.defaults snippet** — a block you can copy into the firmware’s `sdkconfig.defaults` so the device uses a pre-generated token instead of the sandbox.

If any required variable is missing, the script prints a detailed error and points you back to the [LiveKit Cloud API keys page](https://cloud.livekit.io/projects/p_/settings/keys).

### 5.3 Configure the device (sdkconfig.defaults)

Copy the example config into `sdkconfig.defaults` and set WiFi:

```bash
cd code
cp sdkconfig.defaults.example sdkconfig.defaults
```

Edit `sdkconfig.defaults` and set your WiFi SSID and password. For LiveKit, **paste the snippet that `make_test_token.py` printed**: it comments out the sandbox option and fills in the pre-generated token and server URL. The relevant section should look like:

```
# CONFIG_LK_EXAMPLE_USE_SANDBOX=y
# CONFIG_LK_EXAMPLE_SANDBOX_ID="your-sandbox-id"
CONFIG_LK_EXAMPLE_USE_PREGENERATED=y
CONFIG_LK_EXAMPLE_SERVER_URL="wss://your-project.livekit.cloud"
CONFIG_LK_EXAMPLE_TOKEN="<the-ESP32-token-from-the-script>"
```

Use the **ESP32 token** from the script output (not the User token). `sdkconfig.defaults` is in `.gitignore` so your credentials stay out of version control.

### 5.4 Build and flash

```bash
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

The board will connect to WiFi and join the LiveKit room using the token you put in `sdkconfig.defaults`.

### 5.5 Join the room as a user

Open the **User join URL** that `make_test_token.py` printed (the `https://meet.livekit.io/custom?liveKitUrl=...&token=...` link) in your browser. You’ll join the same room as the ESP32 and can verify two-way audio. No need to use the LiveKit Playground separately — the script gives you a direct meet link.

## Troubleshooting

**No audio output (speaker silent)**
- Check that `PA_CTRL` (GPIO 46) is being driven high. The NS4150B amp won't produce output without it. The ES8311 driver handles this via the `pa_pin` field, but only if you pass the correct GPIO.

**Codec init fails (I2C errors)**
- Verify I2C addresses: ES8311 is `0x18` when CE is high, `0x19` when low. ES7210 is `0x40` with AD0=AD1=0. Check your schematic.
- Make sure the I2C bus is initialized before the codecs.

**No microphone input**
- The ES7210 uses TDM mode. If you initialize the RX channel in standard mode, you'll get silence or garbled data.
- Confirm `I2S_ASDOUT` (GPIO 10) is correct — this is the data line from the ES7210 to the ESP32.

**Audio glitches or echo**
- The AEC source expects 4 TDM channels with `channel_mask = 1 | 2`. If your board has fewer mic channels, adjust accordingly.
- Make sure PSRAM is enabled and configured for octal mode in sdkconfig.

## Adapting to your own board

The process is the same for any ESP32-S3 board with I2S audio codecs:

1. Get the schematic and identify: I2S pins, I2C pins, codec I2C addresses, PA enable pin
2. Write a `board.c` that initializes I2C → I2S → codec drivers → `esp_codec_dev` handles
3. Use `get_playback_handle()` and `get_record_handle()` in your media pipeline
4. Everything above the board layer (media, room connection, LiveKit SDK) stays the same

The complete source code is in the [`code/`](../code/) directory.
