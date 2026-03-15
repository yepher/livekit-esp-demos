/*
 * JWT Generator — LiveKit Access Token
 *
 * Generates JWT tokens for authenticating with a LiveKit server.
 * Uses HMAC-SHA256 via mbedTLS (included in ESP-IDF).
 *
 * The token encodes:
 *   - iss: API key (issuer)
 *   - sub: device identity (participant identity)
 *   - iat/exp/nbf: timing claims
 *   - video: room permissions (join, publish, subscribe)
 *
 * Requires NTP time sync before calling jwt_generate().
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default token TTL in seconds (10 minutes) */
#define JWT_DEFAULT_TTL_SEC  600

/* Maximum JWT string length */
#define JWT_MAX_TOKEN_LEN    1024

/**
 * Parameters for JWT generation.
 */
typedef struct {
    const char *api_key;        /* LiveKit API key (iss claim) */
    const char *api_secret;     /* LiveKit API secret (HMAC key) */
    const char *room_name;      /* Room to join */
    const char *identity;       /* Participant identity (sub claim) */
    uint32_t ttl_sec;           /* Token TTL in seconds (0 = default) */
} jwt_params_t;

/**
 * Generate a LiveKit JWT access token.
 *
 * The token is written to `out_token` which must be at least
 * JWT_MAX_TOKEN_LEN bytes. The string is null-terminated.
 *
 * Requires NTP time to be synced (uses time() for iat/exp/nbf).
 *
 * @param params     Token parameters
 * @param out_token  Output buffer (at least JWT_MAX_TOKEN_LEN bytes)
 * @param token_size Size of the output buffer
 * @return ESP_OK on success
 */
esp_err_t jwt_generate(const jwt_params_t *params, char *out_token, size_t token_size);

#ifdef __cplusplus
}
#endif
