// Board initialization for the Waveshare ESP32-S3-Touch-LCD-1.83 using its BSP.
//
// Post 01 configured I2C, I2S, the AXP2101 PMU, and both audio codecs by hand
// (~300 lines of pin definitions, register writes, and I2S mode setup).
// This file achieves the same result using the published Waveshare BSP,
// reducing board init to about 30 lines. The board.h API is unchanged —
// media.c and example.c work without modification.

#include "board.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"

static const char *TAG = "board";

static esp_codec_dev_handle_t play_dev;
static esp_codec_dev_handle_t rec_dev;

void board_init(void)
{
    ESP_LOGI(TAG, "Initializing Waveshare ESP32-S3-Touch-LCD-1.83 (via BSP)");

    // The BSP handles everything internally:
    //   - bsp_i2c_init()   — I2C bus on the board's SDA/SCL pins
    //   - bsp_audio_init() — I2S channels, PA enable
    //   - Codec register configuration via esp_codec_dev
    //
    // bsp_audio_codec_speaker_init() lazily calls bsp_i2c_init() and
    // bsp_audio_init(NULL) if they haven't been called yet, so a single
    // call is enough to bring up the entire audio path.

    play_dev = bsp_audio_codec_speaker_init();
    assert(play_dev && "Speaker codec (ES8311) init failed");

    rec_dev = bsp_audio_codec_microphone_init();
    assert(rec_dev && "Microphone codec (ES7210) init failed");

    esp_codec_dev_set_out_vol(play_dev, CONFIG_LK_EXAMPLE_SPEAKER_VOLUME);
    esp_codec_dev_set_in_gain(rec_dev, 30.0);

    ESP_LOGI(TAG, "Board init complete (via BSP)");
}

esp_codec_dev_handle_t get_playback_handle(void)
{
    return play_dev;
}

esp_codec_dev_handle_t get_record_handle(void)
{
    return rec_dev;
}
