/*
 * Push-to-Talk (PTT) Button Module
 *
 * Provides press-and-hold PTT via a GPIO button (default: GPIO0, the boot
 * button on most ESP32-S3 boards). The microphone starts muted; holding the
 * button unmutes it so audio is captured and sent to the LiveKit room.
 *
 * Muting is done at the codec level via esp_codec_dev_set_in_mute(), which
 * silences the ADC input while keeping the I2S stream running. This avoids
 * start/stop overhead and keeps the AEC reference channel active.
 */

#pragma once

#include "esp_err.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/** PTT state */
typedef enum {
    PTT_STATE_IDLE,     /**< Muted — not transmitting */
    PTT_STATE_TALKING   /**< Unmuted — button held, transmitting */
} ptt_state_t;

/**
 * Initialize the PTT button.
 *
 * Configures the GPIO interrupt and debounce timer. The microphone is muted
 * immediately. Call this after board_init() so the codec handle is valid,
 * and ideally after the LiveKit room has connected.
 *
 * @param record_handle  Codec device handle for microphone mute control
 * @return ESP_OK on success
 */
esp_err_t ptt_init(esp_codec_dev_handle_t record_handle);

/**
 * Release PTT resources (timer, ISR). Mutes the mic before cleanup.
 */
void ptt_cleanup(void);

/**
 * Get the current PTT state.
 */
ptt_state_t ptt_get_state(void);

#ifdef __cplusplus
}
#endif
