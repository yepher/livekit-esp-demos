/*
 * JWT Generator — LiveKit Access Token
 *
 * Builds a JWT with header.payload.signature, each base64url-encoded.
 * Signed with HMAC-SHA256 using the LiveKit API Secret.
 *
 * JWT structure:
 *   Header:  {"alg":"HS256","typ":"JWT"}
 *   Payload: {"iss":"<key>","sub":"<identity>","iat":N,"exp":N,"nbf":N,
 *             "video":{"room":"<name>","roomJoin":true,...}}
 *   Signature: HMAC-SHA256(base64url(header).base64url(payload), secret)
 *
 * Adapted from hey_livekit jwt_generator.c — simplified by removing
 * session_type enum and metadata. This generates a standard LiveKit
 * access token suitable for any use case.
 */

#include "jwt_generator.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_log.h"
#include "mbedtls/md.h"
#include "cJSON.h"

static const char *TAG = "jwt";

/* ── Base64url encoding ── */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * Encode raw bytes to base64url (no padding).
 * Returns the number of characters written (not including null terminator).
 */
static size_t base64url_encode(const uint8_t *src, size_t src_len,
                               char *dst, size_t dst_size)
{
    size_t out_len = 0;
    size_t i;

    for (i = 0; i + 2 < src_len; i += 3) {
        if (out_len + 4 >= dst_size) return 0;
        uint32_t n = ((uint32_t)src[i] << 16) | ((uint32_t)src[i+1] << 8) | src[i+2];
        dst[out_len++] = b64_table[(n >> 18) & 0x3F];
        dst[out_len++] = b64_table[(n >> 12) & 0x3F];
        dst[out_len++] = b64_table[(n >>  6) & 0x3F];
        dst[out_len++] = b64_table[ n        & 0x3F];
    }

    if (i < src_len) {
        if (out_len + 4 >= dst_size) return 0;
        uint32_t n = (uint32_t)src[i] << 16;
        if (i + 1 < src_len) n |= (uint32_t)src[i+1] << 8;

        dst[out_len++] = b64_table[(n >> 18) & 0x3F];
        dst[out_len++] = b64_table[(n >> 12) & 0x3F];
        if (i + 1 < src_len) {
            dst[out_len++] = b64_table[(n >> 6) & 0x3F];
        }
        /* No padding in base64url */
    }

    /* Convert base64 -> base64url: + -> -, / -> _ */
    for (size_t j = 0; j < out_len; j++) {
        if (dst[j] == '+') dst[j] = '-';
        else if (dst[j] == '/') dst[j] = '_';
    }

    dst[out_len] = '\0';
    return out_len;
}

/* ── HMAC-SHA256 ── */

static esp_err_t hmac_sha256(const uint8_t *key, size_t key_len,
                             const uint8_t *msg, size_t msg_len,
                             uint8_t out[32])
{
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        ESP_LOGE(TAG, "mbedTLS SHA256 not available");
        return ESP_FAIL;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    int ret = mbedtls_md_setup(&ctx, md_info, 1 /* HMAC */);
    if (ret != 0) {
        ESP_LOGE(TAG, "md_setup failed: %d", ret);
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }

    ret = mbedtls_md_hmac_starts(&ctx, key, key_len);
    if (ret != 0) goto fail;

    ret = mbedtls_md_hmac_update(&ctx, msg, msg_len);
    if (ret != 0) goto fail;

    ret = mbedtls_md_hmac_finish(&ctx, out);
    if (ret != 0) goto fail;

    mbedtls_md_free(&ctx);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "HMAC-SHA256 failed: %d", ret);
    mbedtls_md_free(&ctx);
    return ESP_FAIL;
}

/* ── JWT Generation ── */

esp_err_t jwt_generate(const jwt_params_t *params, char *out_token, size_t token_size)
{
    if (!params || !out_token || token_size < 256) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    if (!params->api_key || !params->api_secret || !params->room_name || !params->identity) {
        ESP_LOGE(TAG, "Missing required JWT fields");
        return ESP_ERR_INVALID_ARG;
    }

    /* Get current time */
    time_t now = time(NULL);
    if (now < 1700000000) {  /* Sanity check: should be after Nov 2023 */
        ESP_LOGE(TAG, "System time not set (NTP not synced?). time=%ld", (long)now);
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t ttl = params->ttl_sec > 0 ? params->ttl_sec : JWT_DEFAULT_TTL_SEC;

    /* ── Build header JSON ── */
    const char *header_json = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";

    /* ── Build payload JSON ── */
    cJSON *payload = cJSON_CreateObject();
    if (!payload) {
        ESP_LOGE(TAG, "Failed to create JSON payload");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(payload, "iss", params->api_key);
    cJSON_AddStringToObject(payload, "sub", params->identity);
    cJSON_AddNumberToObject(payload, "iat", (double)now);
    cJSON_AddNumberToObject(payload, "exp", (double)(now + ttl));
    cJSON_AddNumberToObject(payload, "nbf", (double)now);

    /* Video grant — room join with publish/subscribe permissions */
    cJSON *video = cJSON_CreateObject();
    cJSON_AddStringToObject(video, "room", params->room_name);
    cJSON_AddBoolToObject(video, "roomJoin", 1);
    cJSON_AddBoolToObject(video, "canPublish", 1);
    cJSON_AddBoolToObject(video, "canSubscribe", 1);
    cJSON_AddBoolToObject(video, "canPublishData", 1);
    cJSON_AddItemToObject(payload, "video", video);

    /* Render payload to compact JSON */
    char *payload_json = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    if (!payload_json) {
        ESP_LOGE(TAG, "Failed to render JSON payload");
        return ESP_ERR_NO_MEM;
    }

    /* ── Base64url encode header and payload ── */
    char header_b64[128];
    char payload_b64[512];

    size_t h_len = base64url_encode(
        (const uint8_t *)header_json, strlen(header_json),
        header_b64, sizeof(header_b64));

    size_t p_len = base64url_encode(
        (const uint8_t *)payload_json, strlen(payload_json),
        payload_b64, sizeof(payload_b64));

    free(payload_json);

    if (h_len == 0 || p_len == 0) {
        ESP_LOGE(TAG, "Base64url encoding failed");
        return ESP_FAIL;
    }

    /* ── Build signing input: header_b64.payload_b64 ── */
    size_t signing_len = h_len + 1 + p_len;
    char *signing_input = malloc(signing_len + 1);
    if (!signing_input) {
        ESP_LOGE(TAG, "Failed to allocate signing input");
        return ESP_ERR_NO_MEM;
    }
    snprintf(signing_input, signing_len + 1, "%s.%s", header_b64, payload_b64);

    /* ── HMAC-SHA256 sign ── */
    uint8_t signature[32];
    esp_err_t ret = hmac_sha256(
        (const uint8_t *)params->api_secret, strlen(params->api_secret),
        (const uint8_t *)signing_input, signing_len,
        signature);

    if (ret != ESP_OK) {
        free(signing_input);
        return ret;
    }

    /* ── Base64url encode signature ── */
    char sig_b64[64];
    size_t s_len = base64url_encode(signature, 32, sig_b64, sizeof(sig_b64));
    if (s_len == 0) {
        free(signing_input);
        ESP_LOGE(TAG, "Signature base64url encoding failed");
        return ESP_FAIL;
    }

    /* ── Assemble final token: header.payload.signature ── */
    size_t total_len = h_len + 1 + p_len + 1 + s_len;
    if (total_len + 1 > token_size) {
        free(signing_input);
        ESP_LOGE(TAG, "Token buffer too small (need %d, have %d)",
            (int)(total_len + 1), (int)token_size);
        return ESP_ERR_NO_MEM;
    }

    snprintf(out_token, token_size, "%s.%s", signing_input, sig_b64);
    free(signing_input);

    ESP_LOGI(TAG, "JWT generated: %d bytes, ttl=%lus, room=%s, identity=%s",
        (int)total_len, (unsigned long)ttl, params->room_name, params->identity);

    return ESP_OK;
}
