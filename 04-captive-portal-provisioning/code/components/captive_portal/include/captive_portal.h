/*
 * Captive Portal — Reusable ESP-IDF Component
 *
 * Provides a browser-based setup flow for WiFi and LiveKit credentials.
 * On first boot (or after factory reset), the device starts a soft-AP with
 * a captive portal. Users connect from a phone or laptop, fill in credentials,
 * and the device reboots into normal operation.
 *
 * Credentials are stored in NVS and persist across reboots.
 * An optional SD card env file can pre-fill the form with defaults.
 *
 * Usage:
 *   1. Call portal_config_load() to check for saved config
 *   2. If no WiFi config exists, call portal_start() to launch the captive portal
 *   3. If WiFi config exists, call portal_wifi_init() + portal_wifi_connect()
 *   4. On WiFi failure, fall back to portal_start()
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── String length limits ── */
#define PORTAL_STR_SHORT   64    /* SSID, identity, room name */
#define PORTAL_STR_MEDIUM  128   /* URLs */
#define PORTAL_STR_LONG    256   /* API secrets */

/* ── Configuration struct ── */
typedef struct {
    /* WiFi */
    char wifi_ssid[PORTAL_STR_SHORT];
    char wifi_password[PORTAL_STR_SHORT];

    /* LiveKit */
    char lk_server_url[PORTAL_STR_MEDIUM];   /* e.g. "wss://my.livekit.cloud" */
    char lk_api_key[PORTAL_STR_SHORT];
    char lk_api_secret[PORTAL_STR_LONG];
    char lk_room_name[PORTAL_STR_SHORT];

    /* Device identity */
    char device_identity[PORTAL_STR_SHORT];   /* e.g. "kitchen-speaker" */
} portal_config_t;

/* ══════════════════════════════════════════════════════════════════
 * NVS Configuration
 * ══════════════════════════════════════════════════════════════════ */

/**
 * Load configuration from NVS.
 * Fields not found in NVS are left as empty strings.
 */
esp_err_t portal_config_load(portal_config_t *cfg);

/**
 * Save configuration to NVS.
 * Only non-empty fields are written.
 */
esp_err_t portal_config_save(const portal_config_t *cfg);

/**
 * Erase all config from NVS (factory reset).
 */
esp_err_t portal_config_clear(void);

/**
 * Check if minimum WiFi config exists (SSID + password).
 */
bool portal_config_has_wifi(const portal_config_t *cfg);

/**
 * Check if LiveKit config is complete (URL, API key, secret, room).
 */
bool portal_config_has_livekit(const portal_config_t *cfg);

/* ══════════════════════════════════════════════════════════════════
 * Captive Portal (Soft-AP + HTTP + DNS)
 * ══════════════════════════════════════════════════════════════════ */

/**
 * Start the captive portal.
 *
 * Launches a soft-AP named "<prefix>-XXXX" (last 4 hex of MAC), starts an
 * HTTP server with a config form, and a DNS server that redirects all
 * queries to the device IP (triggering the captive portal popup).
 *
 * This function does NOT block. The portal runs in the background until
 * the user submits credentials (which triggers a reboot) or portal_stop()
 * is called.
 *
 * @param ap_name_prefix  Prefix for the AP SSID (e.g. "LiveKit-ESP")
 * @param sd_mount_path   SD card mount path for env file defaults (e.g. "/sdcard"),
 *                        or NULL if no SD card is available
 * @return ESP_OK on success
 */
esp_err_t portal_start(const char *ap_name_prefix, const char *sd_mount_path);

/**
 * Check if the captive portal is currently running.
 */
bool portal_is_running(void);

/**
 * Stop the captive portal (tears down AP, HTTP, and DNS).
 */
esp_err_t portal_stop(void);

/* ══════════════════════════════════════════════════════════════════
 * WiFi STA Connection
 * ══════════════════════════════════════════════════════════════════ */

/**
 * Initialize the WiFi subsystem for STA mode.
 * Call once before portal_wifi_connect().
 */
esp_err_t portal_wifi_init(void);

/**
 * Connect to a WiFi network. Blocks until connected or failed.
 * Uses exponential backoff with up to 10 retries.
 *
 * @param ssid      WiFi SSID
 * @param password  WiFi password
 * @return ESP_OK if connected, ESP_FAIL on timeout/failure
 */
esp_err_t portal_wifi_connect(const char *ssid, const char *password);

/**
 * Check if WiFi is currently connected.
 */
bool portal_wifi_is_connected(void);

/* ══════════════════════════════════════════════════════════════════
 * SD Card env File Loading
 * ══════════════════════════════════════════════════════════════════ */

/**
 * Load configuration defaults from an env file on the SD card.
 *
 * Tries env and env.txt in the given directory.
 * Does NOT save to NVS — caller decides whether to commit.
 *
 * Supported keys: WIFI_SSID, WIFI_PASSWORD, LIVEKIT_URL, LIVEKIT_API_KEY,
 * LIVEKIT_API_SECRET, LIVEKIT_ROOM, DEVICE_IDENTITY (or DEVICE_ID).
 *
 * LIVEKIT_ROOM supports a <RANDOM4> placeholder for 4 random hex characters.
 *
 * @param base_path  Directory containing the env file (e.g. "/sdcard")
 * @param cfg        Config struct to populate
 * @return ESP_OK if file found and parsed, ESP_ERR_NOT_FOUND if no file
 */
esp_err_t portal_env_load(const char *base_path, portal_config_t *cfg);

#ifdef __cplusplus
}
#endif
