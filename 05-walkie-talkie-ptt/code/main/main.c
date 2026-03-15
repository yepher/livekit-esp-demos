/*
 * Walkie-Talkie PTT — Main Application
 *
 * Boot flow (same as Post 04):
 *   1. Initialize NVS, LiveKit system, board (BSP), and media pipelines
 *   2. Try to mount SD card (for env file defaults)
 *   3. Load config from NVS
 *   4. If no WiFi config → start captive portal (blocks until user saves & reboots)
 *   5. Connect to WiFi (with retry); if WiFi fails → fall back to captive portal
 *   6. Sync NTP (required for JWT timestamps)
 *   7. If LiveKit config exists → generate JWT on-device, connect to room
 *
 * New in Post 05:
 *   8. When the room connects, initialize PTT on the boot button (GPIO0)
 *   9. Microphone starts muted; press and hold the boot button to talk
 *
 * PTT solves the echo-feedback problem: without AEC, the microphone picks up
 * the agent's speaker output and the agent starts responding to itself. With
 * PTT, the mic is only live while the button is held.
 */

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "bsp/esp-bsp.h"
#include "livekit.h"

#include "captive_portal.h"
#include "board.h"
#include "media.h"
#include "jwt_generator.h"
#include "ptt.h"

static const char *TAG = "main";

/* ── LiveKit room handle ── */
static livekit_room_handle_t s_room_handle = NULL;

/* ── Room state callback ── */
static void on_room_state_changed(livekit_connection_state_t state, void *ctx)
{
    ESP_LOGI(TAG, "Room state changed: %s", livekit_connection_state_str(state));

    livekit_failure_reason_t reason = livekit_room_get_failure_reason(s_room_handle);
    if (reason != LIVEKIT_FAILURE_REASON_NONE) {
        ESP_LOGE(TAG, "Failure reason: %s", livekit_failure_reason_str(reason));
    }

    /* Initialize PTT when the room connects */
    if (state == LIVEKIT_CONNECTION_STATE_CONNECTED) {
        esp_err_t err = ptt_init(get_record_handle());
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "PTT ready — press and hold BOOT button to talk");
        } else {
            ESP_LOGE(TAG, "PTT init failed: %s", esp_err_to_name(err));
        }
    }
}

/* ── NTP time sync ── */
static esp_err_t sync_ntp(void)
{
    ESP_LOGI(TAG, "Starting NTP time sync...");

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("time.google.com", "pool.ntp.org"));
    esp_netif_sntp_init(&sntp_config);

    /* Wait for NTP sync (up to 15 seconds) */
    int retries = 0;
    const int max_retries = 15;
    while (retries < max_retries) {
        time_t now = time(NULL);
        if (now > 1700000000) {  /* After Nov 2023 = valid time */
            ESP_LOGI(TAG, "NTP synced, time=%ld", (long)now);
            return ESP_OK;
        }
        ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retries + 1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(1000));
        retries++;
    }

    ESP_LOGW(TAG, "NTP sync timed out");
    return ESP_FAIL;
}

/* ── Create and connect to LiveKit room ── */
static void join_room(const portal_config_t *cfg)
{
    /* Generate JWT on-device */
    char token[JWT_MAX_TOKEN_LEN];
    jwt_params_t jwt_params = {
        .api_key    = cfg->lk_api_key,
        .api_secret = cfg->lk_api_secret,
        .room_name  = cfg->lk_room_name,
        .identity   = cfg->device_identity[0] ? cfg->device_identity : "esp32-device",
        .ttl_sec    = JWT_DEFAULT_TTL_SEC,
    };

    esp_err_t err = jwt_generate(&jwt_params, token, sizeof(token));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "JWT generation failed: %s", esp_err_to_name(err));
        return;
    }

    /* Create room */
    livekit_room_options_t room_options = {
        .publish = {
            .kind = LIVEKIT_MEDIA_TYPE_AUDIO,
            .audio_encode = {
                .codec = LIVEKIT_AUDIO_CODEC_OPUS,
                .sample_rate = 16000,
                .channel_count = 1,
            },
            .capturer = media_get_capturer(),
        },
        .subscribe = {
            .kind = LIVEKIT_MEDIA_TYPE_AUDIO,
            .renderer = media_get_renderer(),
        },
        .on_state_changed = on_room_state_changed,
    };

    if (livekit_room_create(&s_room_handle, &room_options) != LIVEKIT_ERR_NONE) {
        ESP_LOGE(TAG, "Failed to create room");
        return;
    }

    /* Connect with the generated JWT */
    livekit_err_t connect_res = livekit_room_connect(
        s_room_handle, cfg->lk_server_url, token);

    if (connect_res != LIVEKIT_ERR_NONE) {
        ESP_LOGE(TAG, "Failed to connect to room");
    } else {
        ESP_LOGI(TAG, "Connected to room '%s' at %s",
            cfg->lk_room_name, cfg->lk_server_url);
    }
}

/* ── Main entry point ── */
void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    /* 1. Initialize NVS flash */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* 2. Initialize LiveKit system */
    livekit_system_init();

    /* 3. Initialize board (BSP — I2C, I2S, codecs) */
    board_init();

    /* 4. Initialize media pipelines (capture + render) */
    media_init();

    /* 5. Try to mount SD card (for env file defaults) */
    const char *sd_mount_path = NULL;
    esp_err_t sd_err = bsp_sdcard_mount();
    if (sd_err == ESP_OK) {
        sd_mount_path = BSP_SD_MOUNT_POINT;
        ESP_LOGI(TAG, "SD card mounted at %s", sd_mount_path);
    } else {
        ESP_LOGI(TAG, "No SD card (or mount failed) — continuing without env file defaults");
    }

    /* 6. Load config from NVS */
    portal_config_t cfg = {0};
    portal_config_load(&cfg);

    /* 7. If no WiFi config → start captive portal */
    if (!portal_config_has_wifi(&cfg)) {
        ESP_LOGI(TAG, "No WiFi config found — starting captive portal");
        ESP_LOGI(TAG, "Connect to the device's WiFi AP and open a browser to configure");

        /* WiFi subsystem must be initialized before starting AP */
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

        portal_start("LiveKit-ESP", sd_mount_path);

        /* Portal runs until user saves & reboots — loop here to keep alive */
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(30000));
            ESP_LOGI(TAG, "Captive portal running... waiting for configuration");
        }
        return;  /* unreachable */
    }

    /* 8. Connect to WiFi */
    ESP_LOGI(TAG, "WiFi config found, connecting...");
    portal_wifi_init();
    err = portal_wifi_connect(cfg.wifi_ssid, cfg.wifi_password);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi connection failed — falling back to captive portal");
        esp_wifi_stop();  /* Stop STA mode before switching to AP */
        portal_start("LiveKit-ESP", sd_mount_path);
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(30000));
            ESP_LOGI(TAG, "Captive portal running (WiFi fallback)... waiting for configuration");
        }
        return;  /* unreachable */
    }

    /* 9. Sync NTP (required for JWT timestamps) */
    if (sync_ntp() != ESP_OK) {
        ESP_LOGW(TAG, "NTP sync failed — JWT generation may fail");
    }

    /* 10. If LiveKit config exists → generate JWT and connect */
    if (portal_config_has_livekit(&cfg)) {
        ESP_LOGI(TAG, "LiveKit config found — generating JWT and connecting...");
        join_room(&cfg);
    } else {
        ESP_LOGI(TAG, "No LiveKit config — WiFi connected but not joining a room");
        ESP_LOGI(TAG, "To configure LiveKit, erase NVS and re-enter the captive portal");
    }

    /* 11. Main loop (keep-alive with PTT state) */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        const char *ptt_str = (ptt_get_state() == PTT_STATE_TALKING)
                              ? "TALKING" : "IDLE";
        ESP_LOGI(TAG, "Running (heap: %lu bytes free, PTT: %s)",
            (unsigned long)esp_get_free_internal_heap_size(), ptt_str);
    }
}
