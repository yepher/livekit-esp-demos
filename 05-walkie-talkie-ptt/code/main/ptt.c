/*
 * Push-to-Talk (PTT) Button Implementation
 *
 * Adapted from hey_livekit/ptt_minimal. Uses GPIO0 (boot button) with a 50 ms
 * debounce timer to toggle codec-level microphone muting.
 *
 * How it works:
 *   1. GPIO is configured as input with internal pull-up, interrupt on any edge.
 *   2. Each edge (press or release) triggers an ISR that restarts a 50 ms
 *      one-shot timer. The ISR does no real work — it just resets the timer.
 *   3. When the timer fires (50 ms after the last edge), the callback reads the
 *      actual GPIO level. If the state changed, it mutes or unmutes the codec.
 *
 * The 50 ms window absorbs mechanical contact bounce. Only stable state
 * transitions produce mute/unmute calls.
 */

#include "ptt.h"

#include "esp_attr.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "ptt";

/* ── State ── */
static esp_codec_dev_handle_t s_record_handle = NULL;
static esp_timer_handle_t     s_debounce_timer = NULL;
static volatile bool          s_last_button_state = true;  /* true = released (high) */
static ptt_state_t            s_ptt_state = PTT_STATE_IDLE;

#define PTT_BUTTON_GPIO   CONFIG_PTT_BUTTON_GPIO
#define DEBOUNCE_US       (50 * 1000)  /* 50 ms */

/* ── Debounce timer callback (runs in timer task context, not ISR) ── */
static void debounce_cb(void *arg)
{
    bool released = (gpio_get_level(PTT_BUTTON_GPIO) == 1);

    /* Ignore if state hasn't actually changed */
    if (released == s_last_button_state) {
        return;
    }
    s_last_button_state = released;

    if (!released) {
        /* Button pressed → unmute mic */
        s_ptt_state = PTT_STATE_TALKING;
        esp_codec_dev_set_in_mute(s_record_handle, false);
        ESP_LOGI(TAG, "PTT: Talking (mic unmuted)");
    } else {
        /* Button released → mute mic */
        s_ptt_state = PTT_STATE_IDLE;
        esp_codec_dev_set_in_mute(s_record_handle, true);
        ESP_LOGI(TAG, "PTT: Idle (mic muted)");
    }
}

/* ── GPIO ISR — just restart the debounce timer ── */
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    esp_timer_stop(s_debounce_timer);
    esp_timer_start_once(s_debounce_timer, DEBOUNCE_US);
}

/* ── Public API ── */

esp_err_t ptt_init(esp_codec_dev_handle_t record_handle)
{
    s_record_handle = record_handle;

    /* Start muted */
    esp_codec_dev_set_in_mute(s_record_handle, true);
    s_ptt_state = PTT_STATE_IDLE;
    s_last_button_state = true;

    /* Configure GPIO: input, pull-up, interrupt on both edges */
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_ANYEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PTT_BUTTON_GPIO),
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "GPIO config failed");

    /* Create debounce timer */
    esp_timer_create_args_t timer_args = {
        .callback = debounce_cb,
        .name     = "ptt_debounce",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_debounce_timer),
                        TAG, "Timer create failed");

    /* Install ISR service (may already be installed — that's OK) */
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "ISR service install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(PTT_BUTTON_GPIO, gpio_isr_handler, NULL),
                        TAG, "ISR handler add failed");

    ESP_LOGI(TAG, "PTT initialized on GPIO%d (press and hold to talk)", PTT_BUTTON_GPIO);
    return ESP_OK;
}

void ptt_cleanup(void)
{
    if (s_debounce_timer) {
        esp_timer_stop(s_debounce_timer);
        esp_timer_delete(s_debounce_timer);
        s_debounce_timer = NULL;
    }
    gpio_isr_handler_remove(PTT_BUTTON_GPIO);

    if (s_record_handle) {
        esp_codec_dev_set_in_mute(s_record_handle, true);
    }
    s_ptt_state = PTT_STATE_IDLE;
    ESP_LOGI(TAG, "PTT cleanup complete");
}

ptt_state_t ptt_get_state(void)
{
    return s_ptt_state;
}
