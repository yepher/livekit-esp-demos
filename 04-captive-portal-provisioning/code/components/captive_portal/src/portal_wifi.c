/*
 * Captive Portal — WiFi STA Manager
 *
 * Handles WiFi STA connection with auto-retry and exponential backoff.
 * Uses ESP-IDF WiFi events + default event loop.
 *
 * Adapted from hey_livekit wifi_manager.c — decoupled from LED control
 * and global app context. All state is internal to this module.
 */

#include "captive_portal.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "portal_wifi";

/* Retry parameters */
#define WIFI_MAX_RETRIES      10
#define WIFI_RETRY_BASE_MS    1000
#define WIFI_RETRY_MAX_MS     30000

/* Event bits */
#define WIFI_CONNECTED_BIT   BIT0
#define WIFI_FAIL_BIT        BIT1

static EventGroupHandle_t s_wifi_events = NULL;
static esp_netif_t       *s_sta_netif   = NULL;
static bool               s_initialized = false;
static bool               s_connecting  = false;
static bool               s_connected   = false;
static uint32_t           s_retry_count = 0;
static char               s_ip_str[16]  = "";

/* ── Event handler ── */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            s_connected = false;
            s_ip_str[0] = '\0';

            if (!s_connecting) {
                /* Intentional disconnect */
                break;
            }

            s_retry_count++;

            if (s_retry_count >= WIFI_MAX_RETRIES) {
                ESP_LOGW(TAG, "WiFi failed after %lu retries, giving up",
                    (unsigned long)s_retry_count);
                xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
                break;
            }

            /* Exponential backoff: base * 2^retry, capped */
            uint32_t delay_ms = WIFI_RETRY_BASE_MS * (1 << (s_retry_count > 5 ? 5 : s_retry_count));
            if (delay_ms > WIFI_RETRY_MAX_MS) delay_ms = WIFI_RETRY_MAX_MS;

            ESP_LOGI(TAG, "Disconnected, retry %lu/%d in %lums",
                (unsigned long)s_retry_count, WIFI_MAX_RETRIES, (unsigned long)delay_ms);

            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            esp_wifi_connect();
            break;
        }

        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));

        ESP_LOGI(TAG, "Connected, IP: %s", s_ip_str);

        s_connected = true;
        s_retry_count = 0;

        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

/* ── Public API ── */

esp_err_t portal_wifi_init(void)
{
    if (s_initialized) return ESP_OK;

    s_wifi_events = xEventGroupCreate();
    if (!s_wifi_events) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi manager initialized");
    return ESP_OK;
}

esp_err_t portal_wifi_connect(const char *ssid, const char *password)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Connecting to '%s'...", ssid);

    wifi_config_t wifi_cfg = {0};
    strncpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));

    s_retry_count = 0;
    s_connecting = true;
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_start());

    /* Block until connected or failed */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_events,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(60000));   /* 60s overall timeout */

    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    }

    ESP_LOGW(TAG, "WiFi connection timed out or failed");
    return ESP_FAIL;
}

bool portal_wifi_is_connected(void)
{
    return s_connected;
}
