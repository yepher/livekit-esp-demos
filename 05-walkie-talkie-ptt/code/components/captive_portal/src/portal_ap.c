/*
 * Captive Portal — Soft-AP + HTTP Server + DNS Captive Portal
 *
 * Starts a soft-AP, serves a config form over HTTP, and runs a tiny DNS
 * server that resolves ALL queries to 192.168.4.1 — triggering the captive
 * portal popup on most phones and laptops.
 *
 * Adapted from hey_livekit config_ap.c — decoupled from app-specific globals.
 * The AP name prefix and SD card path are passed as parameters instead of
 * being read from a global app context.
 */

#include "captive_portal.h"
#include "portal_page.h"

#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

static const char *TAG = "portal_ap";

#define AP_CHANNEL      1
#define AP_MAX_CONN     4
#define DNS_PORT        53
#define AP_IP           "192.168.4.1"

static httpd_handle_t s_httpd = NULL;
static esp_netif_t   *s_ap_netif = NULL;
static bool           s_running = false;
static TaskHandle_t   s_dns_task = NULL;

/* Stored parameters from portal_start() */
static const char *s_sd_mount_path = NULL;

/* Forward declarations */
static void dns_server_task(void *arg);
static esp_err_t start_http_server(void);
static void stop_http_server(void);

/* ══════════════════════════════════════════════════════════════════
 * HTTP Handlers
 * ══════════════════════════════════════════════════════════════════ */

/* GET / — serve config page */
static esp_err_t handler_get_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, PORTAL_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

/* Helper: copy src into dst if dst is empty */
static void fill_if_empty(char *dst, const char *src, size_t dst_size)
{
    if (dst[0] == '\0' && src[0] != '\0') {
        strncpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

/* GET /status — return current config + device info as JSON */
static esp_err_t handler_get_status(httpd_req_t *req)
{
    portal_config_t cfg = {0};
    portal_config_load(&cfg);

    /* Overlay env file defaults for any fields still empty.
     * This pre-fills the captive portal form without committing to NVS. */
    if (s_sd_mount_path != NULL) {
        portal_config_t env_cfg = {0};
        esp_err_t env_ret = portal_env_load(s_sd_mount_path, &env_cfg);
        if (env_ret == ESP_OK) {
            fill_if_empty(cfg.wifi_ssid,      env_cfg.wifi_ssid,      sizeof(cfg.wifi_ssid));
            fill_if_empty(cfg.wifi_password,   env_cfg.wifi_password,  sizeof(cfg.wifi_password));
            fill_if_empty(cfg.lk_server_url,   env_cfg.lk_server_url,  sizeof(cfg.lk_server_url));
            fill_if_empty(cfg.lk_api_key,      env_cfg.lk_api_key,     sizeof(cfg.lk_api_key));
            fill_if_empty(cfg.lk_api_secret,   env_cfg.lk_api_secret,  sizeof(cfg.lk_api_secret));
            fill_if_empty(cfg.lk_room_name,    env_cfg.lk_room_name,   sizeof(cfg.lk_room_name));
            fill_if_empty(cfg.device_identity,  env_cfg.device_identity, sizeof(cfg.device_identity));
            ESP_LOGI(TAG, "/status: env file overlay applied");
        }
    } else {
        ESP_LOGD(TAG, "/status: no SD card path, skipping env file overlay");
    }

    cJSON *root = cJSON_CreateObject();

    /* Config values — safe on a local-only AP with no internet */
    cJSON_AddStringToObject(root, "wifi_ssid", cfg.wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_pass", cfg.wifi_password);
    cJSON_AddStringToObject(root, "lk_url", cfg.lk_server_url);
    cJSON_AddStringToObject(root, "lk_api_key", cfg.lk_api_key);
    cJSON_AddStringToObject(root, "lk_api_secret", cfg.lk_api_secret);
    cJSON_AddStringToObject(root, "lk_room", cfg.lk_room_name);
    cJSON_AddStringToObject(root, "device_id", cfg.device_identity);

    /* Device info */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(root, "mac", mac_str);
    cJSON_AddNumberToObject(root, "free_heap", esp_get_free_internal_heap_size());

    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);

    cJSON_free(json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* POST /config — save configuration and reboot */
static esp_err_t handler_post_config(httpd_req_t *req)
{
    /* Read body */
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 4096) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_FAIL;
    }

    char *buf = malloc(total_len + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total_len) {
        int ret = httpd_req_recv(req, buf + received, total_len - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[total_len] = '\0';

    /* Parse JSON */
    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    /* Build config struct — load existing first so we don't lose fields */
    portal_config_t cfg = {0};
    portal_config_load(&cfg);

    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "wifi_ssid")) && cJSON_IsString(item))
        strncpy(cfg.wifi_ssid, item->valuestring, sizeof(cfg.wifi_ssid) - 1);
    if ((item = cJSON_GetObjectItem(root, "wifi_pass")) && cJSON_IsString(item) && item->valuestring[0])
        strncpy(cfg.wifi_password, item->valuestring, sizeof(cfg.wifi_password) - 1);
    if ((item = cJSON_GetObjectItem(root, "lk_url")) && cJSON_IsString(item))
        strncpy(cfg.lk_server_url, item->valuestring, sizeof(cfg.lk_server_url) - 1);
    if ((item = cJSON_GetObjectItem(root, "lk_api_key")) && cJSON_IsString(item))
        strncpy(cfg.lk_api_key, item->valuestring, sizeof(cfg.lk_api_key) - 1);
    if ((item = cJSON_GetObjectItem(root, "lk_api_secret")) && cJSON_IsString(item) && item->valuestring[0])
        strncpy(cfg.lk_api_secret, item->valuestring, sizeof(cfg.lk_api_secret) - 1);
    if ((item = cJSON_GetObjectItem(root, "lk_room")) && cJSON_IsString(item))
        strncpy(cfg.lk_room_name, item->valuestring, sizeof(cfg.lk_room_name) - 1);
    if ((item = cJSON_GetObjectItem(root, "device_id")) && cJSON_IsString(item))
        strncpy(cfg.device_identity, item->valuestring, sizeof(cfg.device_identity) - 1);

    cJSON_Delete(root);

    /* Validate minimum required fields */
    if (!portal_config_has_wifi(&cfg)) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"WiFi SSID and password are required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    /* Save to NVS */
    esp_err_t err = portal_config_save(&cfg);
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"ok\":false,\"error\":\"Failed to save config\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Config saved via web UI, rebooting in 3s...");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);

    /* Reboot after a short delay to let the response send */
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();

    return ESP_OK;  /* unreachable */
}

/* POST /reset — factory reset */
static esp_err_t handler_post_reset(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Factory reset requested via web UI");

    portal_config_clear();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);

    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    return ESP_OK;  /* unreachable */
}

/* Catch-all handler for captive portal — redirect to root */
static esp_err_t handler_captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════════
 * HTTP Server
 * ══════════════════════════════════════════════════════════════════ */

static esp_err_t start_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    /* Register handlers — order matters for wildcard matching */
    const httpd_uri_t uri_root = {
        .uri = "/", .method = HTTP_GET, .handler = handler_get_root
    };
    const httpd_uri_t uri_status = {
        .uri = "/status", .method = HTTP_GET, .handler = handler_get_status
    };
    const httpd_uri_t uri_config = {
        .uri = "/config", .method = HTTP_POST, .handler = handler_post_config
    };
    const httpd_uri_t uri_reset = {
        .uri = "/reset", .method = HTTP_POST, .handler = handler_post_reset
    };
    /* Captive portal: Android/Apple/Windows check URLs */
    const httpd_uri_t uri_catch_all = {
        .uri = "/*", .method = HTTP_GET, .handler = handler_captive_redirect
    };

    httpd_register_uri_handler(s_httpd, &uri_root);
    httpd_register_uri_handler(s_httpd, &uri_status);
    httpd_register_uri_handler(s_httpd, &uri_config);
    httpd_register_uri_handler(s_httpd, &uri_reset);
    httpd_register_uri_handler(s_httpd, &uri_catch_all);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

static void stop_http_server(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

/* ══════════════════════════════════════════════════════════════════
 * DNS Captive Portal
 *
 * Tiny DNS server that responds to ALL queries with 192.168.4.1.
 * This triggers the captive portal popup on most phones/laptops.
 * ══════════════════════════════════════════════════════════════════ */

static void dns_server_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in saddr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        ESP_LOGE(TAG, "DNS bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS captive portal started on port %d", DNS_PORT);

    uint8_t buf[512];
    struct sockaddr_in caddr;
    socklen_t clen;

    while (s_running) {
        clen = sizeof(caddr);

        /* Use select() with timeout so we can check s_running */
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

        int sel = select(sock + 1, &rfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        int len = recvfrom(sock, buf, sizeof(buf), 0,
                          (struct sockaddr *)&caddr, &clen);
        if (len < 12) continue;  /* too short for DNS header */

        /*
         * Build minimal DNS response:
         * - Copy query ID
         * - Set response flags (QR=1, AA=1, RCODE=0)
         * - 1 answer RR pointing to 192.168.4.1
         */
        uint8_t resp[512];
        memcpy(resp, buf, len);  /* copy entire query */

        /* Set flags: QR=1, AA=1, RD=1, RA=0 */
        resp[2] = 0x84;  /* QR=1, OPCODE=0, AA=1 */
        resp[3] = 0x00;  /* RCODE=0 */

        /* Answer count = 1 */
        resp[6] = 0x00;
        resp[7] = 0x01;

        /* Append answer RR after the query section */
        int pos = len;

        /* Name pointer to question name (offset 12) */
        resp[pos++] = 0xC0;
        resp[pos++] = 0x0C;

        /* Type A (1) */
        resp[pos++] = 0x00;
        resp[pos++] = 0x01;

        /* Class IN (1) */
        resp[pos++] = 0x00;
        resp[pos++] = 0x01;

        /* TTL = 60 seconds */
        resp[pos++] = 0x00;
        resp[pos++] = 0x00;
        resp[pos++] = 0x00;
        resp[pos++] = 0x3C;

        /* RDLENGTH = 4 */
        resp[pos++] = 0x00;
        resp[pos++] = 0x04;

        /* RDATA = 192.168.4.1 */
        resp[pos++] = 192;
        resp[pos++] = 168;
        resp[pos++] = 4;
        resp[pos++] = 1;

        sendto(sock, resp, pos, 0, (struct sockaddr *)&caddr, clen);
    }

    close(sock);
    ESP_LOGI(TAG, "DNS server stopped");
    vTaskDelete(NULL);
}

/* ══════════════════════════════════════════════════════════════════
 * Public API
 * ══════════════════════════════════════════════════════════════════ */

esp_err_t portal_start(const char *ap_name_prefix, const char *sd_mount_path)
{
    if (s_running) return ESP_OK;

    ESP_LOGI(TAG, "Starting captive portal...");

    /* Store SD card path for /status handler */
    s_sd_mount_path = sd_mount_path;

    /* Create AP netif if not already done */
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    /* Generate SSID from MAC: "<prefix>-XXXX" */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[32];
    const char *prefix = ap_name_prefix ? ap_name_prefix : "ESP32";
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", prefix, mac[4], mac[5]);

    /* Configure and start soft-AP */
    wifi_config_t ap_cfg = {
        .ap = {
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = strlen(ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: SSID='%s', IP=%s", ssid, AP_IP);

    /* Start HTTP server */
    esp_err_t err = start_http_server();
    if (err != ESP_OK) return err;

    /* Start DNS captive portal */
    s_running = true;
    xTaskCreate(dns_server_task, "dns_srv", 4096, NULL, 5, &s_dns_task);

    ESP_LOGI(TAG, "Captive portal ready — connect to '%s' and open http://%s", ssid, AP_IP);
    return ESP_OK;
}

esp_err_t portal_stop(void)
{
    if (!s_running) return ESP_OK;

    ESP_LOGI(TAG, "Stopping captive portal...");

    s_running = false;

    /* DNS task will exit on next select() timeout */
    if (s_dns_task) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        s_dns_task = NULL;
    }

    stop_http_server();

    esp_wifi_stop();

    s_sd_mount_path = NULL;

    ESP_LOGI(TAG, "Captive portal stopped");
    return ESP_OK;
}

bool portal_is_running(void)
{
    return s_running;
}
