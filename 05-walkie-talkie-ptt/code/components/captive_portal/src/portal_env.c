/*
 * Captive Portal — env File Loader
 *
 * Parses a simple KEY=VALUE file from the SD card and maps
 * known keys to portal_config_t fields.
 *
 * Adapted from hey_livekit env_loader.c — uses portal_config_t instead
 * of device_config_t and has no dependency on app-specific headers.
 */

#include "captive_portal.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "portal_env";

#define MAX_LINE_LEN    512
#define RANDOM4_TOKEN   "<RANDOM4>"

/**
 * Strip leading/trailing whitespace in-place.
 */
static char *strip(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

/**
 * Remove optional surrounding quotes (single or double).
 */
static char *unquote(char *s)
{
    size_t len = strlen(s);
    if (len >= 2) {
        if ((s[0] == '"' && s[len - 1] == '"') ||
            (s[0] == '\'' && s[len - 1] == '\'')) {
            s[len - 1] = '\0';
            s++;
        }
    }
    return s;
}

/**
 * Generate 4 random uppercase hex characters.
 */
static void random_hex4(char *buf)
{
    uint32_t r = esp_random();
    snprintf(buf, 5, "%04X", (unsigned)(r & 0xFFFF));
}

/**
 * Expand <RANDOM4> placeholder in a string.
 */
static void expand_random(const char *src, char *dst, size_t dst_len)
{
    const char *pos = strstr(src, RANDOM4_TOKEN);
    if (!pos) {
        strncpy(dst, src, dst_len - 1);
        dst[dst_len - 1] = '\0';
        return;
    }

    /* Copy prefix */
    size_t prefix_len = pos - src;
    if (prefix_len >= dst_len - 1) {
        strncpy(dst, src, dst_len - 1);
        dst[dst_len - 1] = '\0';
        return;
    }
    memcpy(dst, src, prefix_len);

    /* Insert random hex */
    char hex[5];
    random_hex4(hex);
    size_t hex_len = 4;
    if (prefix_len + hex_len >= dst_len - 1) {
        dst[prefix_len] = '\0';
        return;
    }
    memcpy(dst + prefix_len, hex, hex_len);

    /* Copy suffix */
    const char *suffix = pos + strlen(RANDOM4_TOKEN);
    size_t suffix_len = strlen(suffix);
    size_t remaining = dst_len - prefix_len - hex_len - 1;
    if (suffix_len > remaining) suffix_len = remaining;
    memcpy(dst + prefix_len + hex_len, suffix, suffix_len);
    dst[prefix_len + hex_len + suffix_len] = '\0';
}

/**
 * Copy value into a config field, respecting max length.
 */
static void set_field(char *field, size_t field_size, const char *value)
{
    strncpy(field, value, field_size - 1);
    field[field_size - 1] = '\0';
}

/**
 * Map a KEY=VALUE pair to the appropriate config field.
 */
static bool apply_env_var(portal_config_t *cfg, const char *key, const char *value)
{
    if (strcmp(key, "WIFI_SSID") == 0) {
        set_field(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), value);
    } else if (strcmp(key, "WIFI_PASSWORD") == 0) {
        set_field(cfg->wifi_password, sizeof(cfg->wifi_password), value);
    } else if (strcmp(key, "LIVEKIT_URL") == 0) {
        set_field(cfg->lk_server_url, sizeof(cfg->lk_server_url), value);
    } else if (strcmp(key, "LIVEKIT_API_KEY") == 0) {
        set_field(cfg->lk_api_key, sizeof(cfg->lk_api_key), value);
    } else if (strcmp(key, "LIVEKIT_API_SECRET") == 0) {
        set_field(cfg->lk_api_secret, sizeof(cfg->lk_api_secret), value);
    } else if (strcmp(key, "LIVEKIT_ROOM") == 0) {
        /* Support <RANDOM4> placeholder for random room suffix */
        if (strstr(value, RANDOM4_TOKEN)) {
            expand_random(value, cfg->lk_room_name, sizeof(cfg->lk_room_name));
            ESP_LOGI(TAG, "  Room name expanded: %s", cfg->lk_room_name);
        } else {
            set_field(cfg->lk_room_name, sizeof(cfg->lk_room_name), value);
        }
    } else if (strcmp(key, "DEVICE_IDENTITY") == 0 || strcmp(key, "DEVICE_ID") == 0) {
        set_field(cfg->device_identity, sizeof(cfg->device_identity), value);
    } else {
        return false;  /* Unknown key */
    }
    return true;
}

esp_err_t portal_env_load(const char *base_path, portal_config_t *cfg)
{
    if (!base_path || !cfg) return ESP_ERR_INVALID_ARG;

    /*
     * Try multiple filenames:
     *   env     — primary (dotfiles are unreliable on FAT32/ESP32)
     *   env.txt — for users who can't create extensionless files on Windows
     */
    static const char *candidates[] = { "env", "env.txt", NULL };

    char path[64];
    FILE *f = NULL;

    for (int i = 0; candidates[i] != NULL; i++) {
        snprintf(path, sizeof(path), "%s/%s", base_path, candidates[i]);
        f = fopen(path, "r");
        if (f) {
            ESP_LOGI(TAG, "Found config file: %s", path);
            break;
        }
        ESP_LOGD(TAG, "Tried %s — not found", path);
    }

    if (!f) {
        ESP_LOGI(TAG, "No env file found (tried env, env.txt)");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Loading config defaults from %s", path);

    char line[MAX_LINE_LEN];
    int line_num = 0;
    int applied = 0;
    int skipped = 0;

    while (fgets(line, sizeof(line), f)) {
        line_num++;

        /* Strip newline and whitespace */
        char *trimmed = strip(line);

        /* Skip empty lines and comments */
        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        /* Find the = separator */
        char *eq = strchr(trimmed, '=');
        if (!eq) {
            ESP_LOGW(TAG, "  Line %d: no '=' found, skipping: %.40s", line_num, trimmed);
            skipped++;
            continue;
        }

        /* Split into key and value */
        *eq = '\0';
        char *key = strip(trimmed);
        char *value = strip(eq + 1);

        /* Remove optional quotes from value */
        value = unquote(value);

        /* Skip empty values */
        if (value[0] == '\0') {
            ESP_LOGD(TAG, "  Line %d: empty value for %s, skipping", line_num, key);
            continue;
        }

        /* Apply to config */
        if (apply_env_var(cfg, key, value)) {
            /* Log key name but NOT the value (could be a secret) */
            ESP_LOGI(TAG, "  Loaded: %s", key);
            applied++;
        } else {
            ESP_LOGD(TAG, "  Line %d: unknown key '%s', skipping", line_num, key);
            skipped++;
        }
    }

    fclose(f);

    ESP_LOGI(TAG, "env loaded: %d values applied, %d unknown/skipped", applied, skipped);
    return ESP_OK;
}
