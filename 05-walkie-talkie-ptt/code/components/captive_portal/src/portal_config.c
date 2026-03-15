/*
 * Captive Portal — NVS Configuration Manager
 *
 * All device config is stored in NVS under the "config" namespace.
 * Each field maps to one NVS key. Strings are stored as NVS strings.
 *
 * Adapted from hey_livekit config_manager.c — decoupled from app-specific
 * types so this component can be reused in any ESP-IDF project.
 */

#include "captive_portal.h"

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "portal_config";

#define NVS_NAMESPACE "config"

/* NVS key names (max 15 chars per ESP-IDF NVS limit) */
#define KEY_WIFI_SSID     "wifi_ssid"
#define KEY_WIFI_PASS     "wifi_pass"
#define KEY_LK_URL        "lk_url"
#define KEY_LK_API_KEY    "lk_api_key"
#define KEY_LK_API_SECRET "lk_api_secret"
#define KEY_LK_ROOM       "lk_room"
#define KEY_DEVICE_ID     "device_id"

/* ── Helper: read a string from NVS, empty string on failure ── */
static void nvs_read_str(nvs_handle_t nvs, const char *key, char *buf, size_t buf_len)
{
    size_t len = buf_len;
    esp_err_t err = nvs_get_str(nvs, key, buf, &len);
    if (err != ESP_OK) {
        buf[0] = '\0';
    }
}

/* ── Helper: write a string to NVS (skip empty strings) ── */
static void nvs_write_str(nvs_handle_t nvs, const char *key, const char *val)
{
    if (val[0] != '\0') {
        nvs_set_str(nvs, key, val);
    }
}

/* ── Public API ── */

esp_err_t portal_config_load(portal_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No config namespace in NVS, starting unconfigured");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_read_str(nvs, KEY_WIFI_SSID,     cfg->wifi_ssid,       sizeof(cfg->wifi_ssid));
    nvs_read_str(nvs, KEY_WIFI_PASS,      cfg->wifi_password,   sizeof(cfg->wifi_password));
    nvs_read_str(nvs, KEY_LK_URL,         cfg->lk_server_url,   sizeof(cfg->lk_server_url));
    nvs_read_str(nvs, KEY_LK_API_KEY,     cfg->lk_api_key,      sizeof(cfg->lk_api_key));
    nvs_read_str(nvs, KEY_LK_API_SECRET,  cfg->lk_api_secret,   sizeof(cfg->lk_api_secret));
    nvs_read_str(nvs, KEY_LK_ROOM,        cfg->lk_room_name,    sizeof(cfg->lk_room_name));
    nvs_read_str(nvs, KEY_DEVICE_ID,      cfg->device_identity,  sizeof(cfg->device_identity));

    nvs_close(nvs);

    ESP_LOGI(TAG, "Config loaded: WiFi=%s, LK=%s",
        cfg->wifi_ssid[0] ? "yes" : "no",
        cfg->lk_server_url[0] ? "yes" : "no");

    return ESP_OK;
}

esp_err_t portal_config_save(const portal_config_t *cfg)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open for write failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_write_str(nvs, KEY_WIFI_SSID,     cfg->wifi_ssid);
    nvs_write_str(nvs, KEY_WIFI_PASS,      cfg->wifi_password);
    nvs_write_str(nvs, KEY_LK_URL,         cfg->lk_server_url);
    nvs_write_str(nvs, KEY_LK_API_KEY,     cfg->lk_api_key);
    nvs_write_str(nvs, KEY_LK_API_SECRET,  cfg->lk_api_secret);
    nvs_write_str(nvs, KEY_LK_ROOM,        cfg->lk_room_name);
    nvs_write_str(nvs, KEY_DEVICE_ID,      cfg->device_identity);

    err = nvs_commit(nvs);
    nvs_close(nvs);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Config saved");
    return ESP_OK;
}

esp_err_t portal_config_clear(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    ESP_LOGW(TAG, "Config cleared (factory reset)");
    return err;
}

bool portal_config_has_wifi(const portal_config_t *cfg)
{
    return (cfg->wifi_ssid[0] != '\0' && cfg->wifi_password[0] != '\0');
}

bool portal_config_has_livekit(const portal_config_t *cfg)
{
    return (cfg->lk_server_url[0] != '\0' &&
            cfg->lk_api_key[0] != '\0' &&
            cfg->lk_api_secret[0] != '\0' &&
            cfg->lk_room_name[0] != '\0');
}
