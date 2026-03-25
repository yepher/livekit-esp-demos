# ESP32 Board Support Packages: When to Use One and How to Write Your Own

In [Post 01](../../01-custom-hardware-quickstart/blog/post.md) you brought up the Waveshare ESP32-S3-Touch-LCD-1.83 from scratch — reading the schematic, mapping every GPIO pin, writing register-level PMU and codec init code, and wiring up I2S in TDM mode. The result was a fully working LiveKit audio session, but the `board.c` file ran to about 300 lines, and every new board would need a similar effort.

Most of that code is boilerplate that someone has already written. ESP-IDF's **Board Support Package** (BSP) system lets hardware vendors publish pre-built components that encapsulate all the pin definitions, peripheral init sequences, and board-specific quirks for a given board. When a BSP exists for your hardware, you can replace hundreds of lines of manual init with a handful of function calls.

This post covers three things:
1. **What a BSP is** and how the ESP-IDF BSP ecosystem works
2. **Using the published Waveshare BSP** to shrink the `board.c` from ~300 lines to ~30
3. **Writing your own BSP** when no published one exists (or the published one doesn't fit your needs)

The code for this post is in the [`02-bsp-deep-dive/code/`](../code/) directory. The `board.h` API contract — `board_init()`, `get_playback_handle()`, `get_record_handle()` — is identical to Post 01. Everything above the board layer (media pipeline, LiveKit room connection) is unchanged.

## What you'll need

Same hardware and tools as Post 01:

- [Waveshare ESP32-S3-Touch-LCD-1.83](https://www.waveshare.com/esp32-s3-touch-lcd-1.83.htm).
- Small speaker with MX1.25 connector.
- ESP-IDF 5.4 or later ([install guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/index.html)).
- [LiveKit Cloud](https://cloud.livekit.io) account (free tier works).
- Python 3 with `livekit-api` (`pip install livekit-api`).
- USB-C cable.

## Part 1: What is a BSP?

A Board Support Package is an ESP-IDF component that encapsulates everything specific to a particular development board:

- **Pin definitions** — which GPIOs connect to which peripherals.
- **Peripheral initialization** — I2C bus setup, I2S channel configuration, display init.
- **Board quirks** — PMU power sequencing, IO expander control, codec-specific workarounds.
- **Kconfig options** — configurable peripheral port numbers, feature toggles, display settings.

The BSP exposes a standardized API so application code can call `bsp_audio_codec_speaker_init()` without knowing whether the board uses an ES8311, ES8388, or something else entirely. Your application code just gets an `esp_codec_dev_handle_t` back and uses it the same way regardless of the underlying hardware.

### Where to find BSPs

**[IDF Component Registry](https://components.espressif.com/)** — Search for your board name. BSPs are published as regular IDF components that you add to `idf_component.yml`. For example, the Waveshare board in this series has `waveshare/esp32_s3_touch_lcd_1_83`.

**[esp-bsp GitHub repo](https://github.com/espressif/esp-bsp)** — Espressif maintains BSPs for their own boards (ESP-BOX, Korvo, LCD-EV-Board) plus community-contributed BSPs. This is a good reference for writing your own.

**[Official BSP documentation](https://docs.espressif.com/projects/esp-bsp/en/latest/)** — The full specification for creating and using BSPs, including the standard API surface and Kconfig conventions.

### The four audio functions you need

Every BSP with audio support exposes these functions:

| Function | Returns | What it does |
|----------|---------|-------------|
| `bsp_i2c_init()` | `esp_err_t` | Initializes the I2C bus with the board's SDA/SCL pins |
| `bsp_audio_init(cfg)` | `esp_err_t` | Sets up I2S channels and the power amplifier |
| `bsp_audio_codec_speaker_init()` | `esp_codec_dev_handle_t` | Configures the DAC codec (ES8311 in our case) |
| `bsp_audio_codec_microphone_init()` | `esp_codec_dev_handle_t` | Configures the ADC codec (ES7210 in our case) |

The speaker and microphone init functions are *lazy* — they automatically call `bsp_i2c_init()` and `bsp_audio_init(NULL)` if those haven't been called yet. So in the simplest case, you only need two function calls to bring up the entire audio path.

## Part 2: Using the Waveshare BSP

### Step 1: Update dependencies

In Post 01, the `idf_component.yml` listed the codec drivers explicitly:

```yaml
# Post 01 — manual codec drivers
dependencies:
  idf: ">=5.4"
  livekit/livekit: "0.3.6"
  livekit/sandbox_token: "~0.1.0"
  livekit/example_utils: "~0.2.0"
  espressif/esp_codec_dev: "~1.4"
  espressif/es8311: "*"
  espressif/es7210: "*"
```

With the BSP, the individual codec drivers are replaced by a single BSP dependency. The BSP pulls in `es8311`, `es7210`, and `esp_codec_dev` transitively:

```yaml
# Post 02 — BSP handles codec drivers
dependencies:
  idf: ">=5.4"
  livekit/livekit: "0.3.6"
  livekit/sandbox_token: "~0.1.0"
  livekit/example_utils: "~0.2.0"
  espressif/esp_codec_dev: "~1.4"
  waveshare/esp32_s3_touch_lcd_1_83: "*"
```

Keep `esp_codec_dev` explicitly because `media.c` uses its types directly. The BSP also depends on it, so there's no conflict.

### Step 2: Replace board.c

Here's the entire BSP-based `board.c`:

```c
// Board initialization for the Waveshare ESP32-S3-Touch-LCD-1.83 using its BSP.

#include "board.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "board";

static esp_codec_dev_handle_t play_dev;
static esp_codec_dev_handle_t rec_dev;

void board_init(void)
{
    ESP_LOGI(TAG, "Initializing Waveshare ESP32-S3-Touch-LCD-1.83 (via BSP)");

    play_dev = bsp_audio_codec_speaker_init();
    assert(play_dev && "Speaker codec (ES8311) init failed");

    rec_dev = bsp_audio_codec_microphone_init();
    assert(rec_dev && "Microphone codec (ES7210) init failed");

    esp_codec_dev_set_out_vol(play_dev, CONFIG_LK_EXAMPLE_SPEAKER_VOLUME);
    esp_codec_dev_set_in_gain(rec_dev, 30.0);

    ESP_LOGI(TAG, "Board init complete (via BSP)");
}

esp_codec_dev_handle_t get_playback_handle(void) { return play_dev; }
esp_codec_dev_handle_t get_record_handle(void) { return rec_dev; }
```

Compare this to Post 01's `board.c`:

| Concern | Post 01 (manual) | Post 02 (BSP) |
|---------|------------------|---------------|
| I2C bus init | 10 lines (bus config + `i2c_new_master_bus`) | Handled by BSP |
| I2C bus scan | 20 lines (diagnostic probe loop) | Not needed |
| AXP2101 PMU | 30 lines (register writes for ALDO1) | Handled by BSP |
| I2S channels | 40 lines (STD TX + TDM RX config) | Handled by BSP |
| ES8311 init | 45 lines (I2C ctrl + GPIO + codec + data if) | 1 line: `bsp_audio_codec_speaker_init()` |
| ES7210 init | 40 lines (I2C ctrl + codec + data if) | 1 line: `bsp_audio_codec_microphone_init()` |
| Pin definitions | 12 `#define` lines | Defined in BSP header |
| **Total** | **~300 lines** | **~30 lines** |

The `board.h` header is unchanged — same three functions, same types. Nothing else in the project needs to change.

### Step 3: Configure Kconfig overrides

BSPs ship with default Kconfig values that may not match your setup. The Waveshare BSP defaults to I2C peripheral 1 and I2S peripheral 1. If you want to use peripheral 0 (to match Post 01's configuration), add these lines to `sdkconfig.defaults`:

```
# BSP overrides — use peripheral 0 for I2C and I2S
CONFIG_BSP_I2C_NUM=0
CONFIG_BSP_I2S_NUM=0
```

On the ESP32-S3, both peripherals are functionally identical and can be mapped to any GPIO pins, so either port works. But if you have other code or components that assume a specific port number, being explicit here avoids conflicts.

> **Gotcha:** If you've already built the project before adding these lines, delete `sdkconfig` and rebuild. ESP-IDF only reads `sdkconfig.defaults` when `sdkconfig` doesn't exist yet.

### Step 4: Build, flash, and verify

```bash
cd 02-bsp-deep-dive/code
cp sdkconfig.defaults.example sdkconfig.defaults
# Edit sdkconfig.defaults with your WiFi and LiveKit credentials (same as Post 01)

idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

You should see the BSP initializing everything:

```
I (1023) board: Initializing Waveshare ESP32-S3-Touch-LCD-1.83 (via BSP)
I (1104) ES8311: Work in Slave mode
I (1114) ES7210: Work in Slave mode
I (1139) board: Board init complete (via BSP)
...
I (3200) livekit_example: Room state changed: CONNECTED
```

Open the **User join URL** from the token generation step — same two-way audio as Post 01, much less code.

## Part 3: Writing your own BSP

Using a published BSP is the easy path. But you'll need to write your own when:

- **No BSP exists** for your board.
- **The BSP is missing audio support** (some display-focused BSPs don't include audio functions).
- **You need a custom I2S configuration** (e.g., TDM mode for multi-channel recording, or non-standard sample rates).
- **You're designing a custom product** and want a clean, reusable hardware abstraction.

The [official Espressif BSP documentation](https://docs.espressif.com/projects/esp-bsp/en/latest/) covers the full BSP specification, including display, touch, buttons, SD card, and other peripherals. This section focuses on the audio subset — the four functions your LiveKit application needs.

### File structure

A minimal audio BSP lives in your project's `components/` directory:

```
components/my_board_bsp/
├── CMakeLists.txt          # Component registration
├── Kconfig                 # Configurable options (I2C/I2S port, etc.)
├── idf_component.yml       # Dependencies (esp_codec_dev, es8311, es7210)
├── include/
│   └── bsp/
│       └── esp-bsp.h       # Public API header
└── src/
    └── my_board_bsp.c      # Implementation
```

The `bsp/esp-bsp.h` path is a convention — it's the standard include path that all BSPs follow, so application code always uses `#include "bsp/esp-bsp.h"` regardless of the board.

### Mapping manual code to BSP functions

If you worked through Post 01, you already have all the code you need — it just needs to be reorganized into BSP functions. Here's how each Post 01 function maps:

#### `bsp_i2c_init()` ← wraps `init_i2c()` + `init_pmu()`

```c
static bool i2c_initialized = false;

esp_err_t bsp_i2c_init(void)
{
    if (i2c_initialized) return ESP_OK;  // idempotent guard

    // Same I2C bus config as Post 01's init_i2c()
    i2c_master_bus_config_t cfg = {
        .i2c_port   = CONFIG_BSP_I2C_NUM,   // from Kconfig, not hardcoded
        .scl_io_num = BSP_I2C_SCL,           // #define in header
        .sda_io_num = BSP_I2C_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &i2c_handle), TAG, "I2C init failed");

    // PMU init (Post 01's init_pmu) — enable ALDO1 for codec power
    init_pmu();

    i2c_initialized = true;
    return ESP_OK;
}
```

Key differences from the raw Post 01 code:
- **Idempotent guard** — safe to call multiple times (the codec init functions may call it lazily)
- **Kconfig port number** — `CONFIG_BSP_I2C_NUM` instead of hardcoded `I2C_NUM_0`
- **Pin defines in header** — `BSP_I2C_SCL` / `BSP_I2C_SDA` instead of local `#define`

#### `bsp_audio_init()` ← wraps `init_i2s()`

```c
esp_err_t bsp_audio_init(const i2s_std_config_t *i2s_config)
{
    if (i2s_tx_chan && i2s_rx_chan) return ESP_OK;  // already initialized

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &i2s_tx_chan, &i2s_rx_chan),
                        TAG, "I2S channel creation failed");

    // Use caller's config or fall back to defaults
    const i2s_std_config_t default_cfg = BSP_I2S_DUPLEX_MONO_CFG(16000);
    const i2s_std_config_t *cfg = i2s_config ? i2s_config : &default_cfg;

    i2s_channel_init_std_mode(i2s_tx_chan, cfg);
    i2s_channel_init_std_mode(i2s_rx_chan, cfg);
    i2s_channel_enable(i2s_tx_chan);
    i2s_channel_enable(i2s_rx_chan);

    // Create the shared codec data interface
    audio_codec_i2s_cfg_t i2s_data_cfg = {
        .port = CONFIG_BSP_I2S_NUM,
        .rx_handle = i2s_rx_chan,
        .tx_handle = i2s_tx_chan,
    };
    i2s_data_if = audio_codec_new_i2s_data(&i2s_data_cfg);

    // Enable PA
    gpio_config_t pa_cfg = {
        .pin_bit_mask = BIT64(BSP_PA_PIN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&pa_cfg);
    gpio_set_level(BSP_PA_PIN, 1);

    return ESP_OK;
}
```

The `i2s_config` parameter is what makes BSPs flexible — callers can pass `NULL` for defaults or provide a custom configuration (for example, TDM mode for 4-channel recording).

#### `bsp_audio_codec_speaker_init()` ← wraps `init_es8311()`

```c
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    // Lazy dependency init
    if (!i2s_data_if) {
        bsp_i2c_init();
        bsp_audio_init(NULL);
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = CONFIG_BSP_I2C_NUM,
        .bus_handle = i2c_handle,
        .addr       = ES8311_CODEC_DEFAULT_ADDR,  // 0x30 (8-bit)
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);

    es8311_codec_cfg_t codec_cfg = {
        .ctrl_if    = ctrl,
        .gpio_if    = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin     = BSP_PA_PIN,
        .use_mclk   = true,
        .hw_gain    = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec = es8311_codec_new(&codec_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec,
        .data_if  = i2s_data_if,  // shared with microphone
    };
    return esp_codec_dev_new(&dev_cfg);
}
```

#### `bsp_audio_codec_microphone_init()` ← wraps `init_es7210()`

```c
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    if (!i2s_data_if) {
        bsp_i2c_init();
        bsp_audio_init(NULL);
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port       = CONFIG_BSP_I2C_NUM,
        .bus_handle = i2c_handle,
        .addr       = ES7210_CODEC_DEFAULT_ADDR,  // 0x80 (8-bit)
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);

    es7210_codec_cfg_t codec_cfg = {
        .ctrl_if     = ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 |
                        ES7210_SEL_MIC3 | ES7210_SEL_MIC4,
    };
    const audio_codec_if_t *codec = es7210_codec_new(&codec_cfg);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec,
        .data_if  = i2s_data_if,
    };
    return esp_codec_dev_new(&dev_cfg);
}
```

### Kconfig

```kconfig
menu "Board Support Package"
    config BSP_I2C_NUM
        int "I2C peripheral index"
        default 0
        range 0 1

    config BSP_I2S_NUM
        int "I2S peripheral index"
        default 0
        range 0 1
endmenu
```

### idf_component.yml

```yaml
dependencies:
  idf: ">=5.4"
  espressif/esp_codec_dev: "~1.4"
  espressif/es8311: "*"
  espressif/es7210: "*"
```

### Testing locally

Drop your BSP component directory into `components/` in your project. ESP-IDF discovers it automatically during the build — no need to publish to the Component Registry until you're ready to share.

```
your-project/
├── components/
│   └── my_board_bsp/       ← IDF finds this automatically
├── main/
│   ├── board.c             ← uses #include "bsp/esp-bsp.h"
│   └── ...
└── CMakeLists.txt
```

Once you're happy with it, you can [publish to the IDF Component Registry](https://docs.espressif.com/projects/idf-component-manager/en/latest/guides/packaging_components.html) so others (and your future projects) can pull it in with a single line in `idf_component.yml`.

## Part 4: Trade-offs

There's no single "right" approach. Here's a comparison to help you decide:

| Dimension | Manual init (Post 01) | Published BSP (Post 02) | Custom BSP |
|-----------|----------------------|------------------------|------------|
| **Setup effort** | High — read schematic, write driver code | Low — add one dependency | Medium — write BSP once, reuse forever |
| **Code in `board.c`** | ~300 lines | ~30 lines | ~30 lines (consumer side) |
| **Pin visibility** | All pins visible in your code | Hidden in BSP source | You control both sides |
| **I2S flexibility** | Full control (TDM, custom sample rates) | Limited to BSP's `bsp_audio_init()` config | Full control |
| **Upstream updates** | Manual maintenance | Automatic via component manager | Manual maintenance |
| **Team onboarding** | Must understand hardware details | Can start immediately | BSP documents the hardware |
| **Binary size** | Only what you use | BSP may pull in display/LVGL deps | Only what you include |
| **Best for** | Learning, prototyping, one-off boards | Production boards with published BSPs | Custom products, teams, multi-project reuse |

### A note on I2S modes

One practical trade-off worth highlighting: the BSP's `bsp_audio_init()` uses **standard I2S mode** for both TX and RX channels. Post 01 used **TDM mode** for the RX channel (ES7210) to get all four microphone channels on separate time slots — this is what the AEC (Acoustic Echo Cancellation) pipeline expects.

If your application needs TDM (for example, multi-channel recording or AEC with a reference channel), you have two options:

1. **Call `bsp_audio_init()` with a custom `i2s_std_config_t`** before the codec init functions — this prevents them from using the default standard-mode config
2. **Write a custom BSP** (Part 3) where `bsp_audio_init()` sets up TDM mode for RX

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `ESP_ERR_NOT_FOUND` during build | BSP component not found in registry | Check the exact component name: `waveshare/esp32_s3_touch_lcd_1_83` (underscores, not hyphens) |
| Codec NACK errors at boot | BSP using wrong I2C port | Add `CONFIG_BSP_I2C_NUM=0` to `sdkconfig.defaults`, delete `sdkconfig`, rebuild |
| Speaker silent | BSP not enabling PA pin | Check BSP source — some BSPs use an IO expander for PA control. May need to call `bsp_audio_poweramp_enable(true)` explicitly |
| Stale config after editing defaults | `sdkconfig` caches old values | Delete `sdkconfig` and rebuild: `rm sdkconfig && idf.py build` |
| Binary too large for partition | BSP pulls in LVGL / display dependencies | Increase factory partition size in `partitions.csv`, or use `BSP_CONFIG_NO_GRAPHIC_LIB=1` if the BSP supports it |
| `esp_codec_dev` version conflict | BSP wants a different version than your `idf_component.yml` | Change your version constraint to `"*"` and let the BSP's version win |

## Conclusion

The `board.h` API contract — `board_init()`, `get_playback_handle()`, `get_record_handle()` — stayed exactly the same across Post 01 and Post 02. That's the point: **the hardware abstraction boundary means everything above it is unchanged**. `media.c`, `example.c`, and the LiveKit SDK don't know or care whether you initialized the codecs by hand or through a BSP.

If a published BSP exists for your board and fits your needs, use it — you'll save time and get upstream bug fixes for free. If not, writing a minimal audio BSP is straightforward: take your working manual init code, wrap it in the four standard functions, add an idempotent guard and Kconfig options, and you've got a reusable component.

The next post builds an audio pipeline from discrete components on a breadboard — no dev board, no BSP, just an ESP32-S3 module wired directly to codec breakout boards.
