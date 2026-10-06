/*
 * src/provisioning.c
 *
 * SoftAP Captive Portal Wi-Fi Provisioning Engine
 * Espressif ESP32-C6 RISC-V SoC Bare-Metal Kernel Runtime
 *
 * Implements interactive 2.4 GHz Wi-Fi network scanning, captive portal
 * web setup (/setup), secure credential configuration (POST /api/wifi/configure),
 * and persistent wear-leveled NVS storage for automated Station (STA) onboarding.
 */

#include "provisioning.h"
#include "http_server.h"
#include "web_assets.h"
#include "nvs.h"
#include "wifi.h"
#include "wifi_vendor_types.h"
#include "wpa2_client.h"
#include "dhcp.h"
#include "net.h"
#include "config.h"
#include "string.h"

#if defined(__riscv)
#include "console.h"
#include "utils.h"
#else
#include <stdio.h>
#define console_puts(s) printf("%s", (s))
#endif

/* ========================================================================= */
/* Static Storage & Subsystem State (Zero Dynamic Heap Allocation)           */
/* ========================================================================= */
static provisioning_state_t    s_prov_state = PROV_STATE_UNPROVISIONED;
static wifi_credentials_t      s_prov_creds;
static wifi_scan_item_t        s_prov_scan_items[PROVISIONING_MAX_SCAN_APS];
static uint16_t                s_prov_scan_count = 0U;
static provisioning_telemetry_t s_prov_telemetry;
static bool                    s_prov_initialized = false;
static prov_join_info_t        s_prov_join;
static uint32_t                s_prov_join_disc_base = 0U;  /* STA disconnect count when the phase began */
static bool                    s_prov_scan_requested = false; /* portal rescan, run from provisioning_tick() */

/* ========================================================================= */
/* Zero-Libc String & Integer Formatting Utilities                           */
/* ========================================================================= */
static size_t prov_u32_to_dec(uint32_t val, char *buf, size_t max_len)
{
    if (buf == NULL || max_len < 2U) return 0U;
    if (val == 0U)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1U;
    }
    char temp[12];
    size_t digits = 0U;
    while (val > 0U && digits < sizeof(temp))
    {
        temp[digits++] = (char)('0' + (val % 10U));
        val /= 10U;
    }
    if (digits >= max_len) digits = max_len - 1U;
    for (size_t i = 0U; i < digits; i++)
    {
        buf[i] = temp[digits - 1U - i];
    }
    buf[digits] = '\0';
    return digits;
}

static size_t prov_i32_to_dec(int32_t val, char *buf, size_t max_len)
{
    if (buf == NULL || max_len < 3U) return 0U;
    if (val == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1U;
    }
    size_t offset = 0U;
    uint32_t uval;
    if (val < 0)
    {
        buf[offset++] = '-';
        uval = (uint32_t)(-val);
    }
    else
    {
        uval = (uint32_t)val;
    }
    char temp[12];
    size_t digits = 0U;
    while (uval > 0U && digits < sizeof(temp))
    {
        temp[digits++] = (char)('0' + (uval % 10U));
        uval /= 10U;
    }
    if (offset + digits >= max_len) digits = max_len - offset - 1U;
    for (size_t i = 0U; i < digits; i++)
    {
        buf[offset + i] = temp[digits - 1U - i];
    }
    buf[offset + digits] = '\0';
    return offset + digits;
}

static size_t prov_str_append(char *dest, size_t dest_max, const char *src)
{
    if (dest == NULL || src == NULL || dest_max == 0U) return 0U;
    size_t dlen = strlen(dest);
    if (dlen >= dest_max - 1U) return dlen;
    size_t avail = dest_max - 1U - dlen;
    size_t slen = strlen(src);
    size_t to_copy = (slen < avail) ? slen : avail;
    memcpy(dest + dlen, src, to_copy);
    dest[dlen + to_copy] = '\0';
    return dlen + to_copy;
}

static inline void prov_safe_copy(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || dest_size == 0U)
    {
        return;
    }
    if (src == NULL)
    {
        dest[0] = '\0';
        return;
    }
    size_t slen = strlen(src);
    if (slen >= dest_size)
    {
        slen = dest_size - 1U;
    }
    memcpy(dest, src, slen);
    dest[slen] = '\0';
}

/* ========================================================================= */
/* Payload Parameter Extraction (JSON & URL-Encoded Form Parser)             */
/* ========================================================================= */
static bool prov_extract_param(const char *input, const char *key, char *out_val, size_t max_len)
{
    if (input == NULL || key == NULL || out_val == NULL || max_len == 0U)
    {
        return false;
    }
    out_val[0] = '\0';
    size_t klen = strlen(key);

    /* 1. Try JSON format: "key" : "value" or "key":"value" */
    const char *p = input;
    while ((p = strstr(p, key)) != NULL)
    {
        /* Verify key boundary (either preceded by " or at start) */
        bool key_match = false;
        if (p > input && *(p - 1) == '"')
        {
            if (*(p + klen) == '"')
            {
                key_match = true;
                p += klen + 1; /* Skip key and closing quote */
            }
        }
        else if (p == input || *(p - 1) == '&' || *(p - 1) == '?' || *(p - 1) == ' ' || *(p - 1) == '{')
        {
            if (*(p + klen) == '=')
            {
                /* Form encoded: key=value */
                p += klen + 1;
                size_t idx = 0U;
                while (*p != '\0' && *p != '&' && *p != ' ' && *p != '\r' && *p != '\n' && idx < max_len - 1U)
                {
                    if (*p == '%' && *(p + 1) != '\0' && *(p + 2) != '\0')
                    {
                        /* Simple 2-hex-digit URL decode */
                        char h1 = *(p + 1);
                        char h2 = *(p + 2);
                        uint8_t b = 0U;
                        if (h1 >= '0' && h1 <= '9') b = (uint8_t)((h1 - '0') << 4);
                        else if (h1 >= 'a' && h1 <= 'f') b = (uint8_t)((h1 - 'a' + 10) << 4);
                        else if (h1 >= 'A' && h1 <= 'F') b = (uint8_t)((h1 - 'A' + 10) << 4);
                        if (h2 >= '0' && h2 <= '9') b |= (uint8_t)(h2 - '0');
                        else if (h2 >= 'a' && h2 <= 'f') b |= (uint8_t)(h2 - 'a' + 10);
                        else if (h2 >= 'A' && h2 <= 'F') b |= (uint8_t)(h2 - 'A' + 10);
                        out_val[idx++] = (char)b;
                        p += 3;
                    }
                    else if (*p == '+')
                    {
                        out_val[idx++] = ' ';
                        p++;
                    }
                    else
                    {
                        out_val[idx++] = *p++;
                    }
                }
                out_val[idx] = '\0';
                return (idx > 0U);
            }
        }

        if (key_match)
        {
            /* Skip whitespace and colon */
            while (*p == ' ' || *p == '\t' || *p == ':') p++;
            if (*p == '"')
            {
                p++; /* Skip opening quote */
                size_t idx = 0U;
                while (*p != '\0' && *p != '"' && idx < max_len - 1U)
                {
                    if (*p == '\\' && *(p + 1) != '\0') p++; /* Skip escape */
                    out_val[idx++] = *p++;
                }
                out_val[idx] = '\0';
                return true;
            }
        }
        p++;
    }

    return false;
}

/* Appends src as JSON string content: escapes quote and backslash, drops control characters */
static void prov_json_append_str(char *dest, size_t dest_max, const char *src)
{
    char esc[3] = {'\\', '\0', '\0'};
    char one[2] = {'\0', '\0'};
    for (const char *p = src; p != NULL && *p != '\0'; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            esc[1] = *p;
            prov_str_append(dest, dest_max, esc);
        }
        else if ((unsigned char)*p >= (unsigned char)' ')
        {
            one[0] = *p;
            prov_str_append(dest, dest_max, one);
        }
    }
}

static uint8_t prov_auth_from_vendor(wifi_auth_mode_t mode)
{
    switch (mode)
    {
        case WIFI_AUTH_OPEN:            return PROV_AUTH_OPEN;
        case WIFI_AUTH_WEP:             return PROV_AUTH_WEP;
        case WIFI_AUTH_WPA_PSK:         return PROV_AUTH_WPA_PSK;
        case WIFI_AUTH_WPA2_PSK:        return PROV_AUTH_WPA2_PSK;
        case WIFI_AUTH_WPA_WPA2_PSK:    return PROV_AUTH_WPA_WPA2_PSK;
        case WIFI_AUTH_WPA2_ENTERPRISE: return PROV_AUTH_WPA2_ENTERPRISE;
        case WIFI_AUTH_WPA3_PSK:        return PROV_AUTH_WPA3_PSK;
        case WIFI_AUTH_WPA2_WPA3_PSK:   return PROV_AUTH_WPA2_WPA3_PSK;
        default:                        return PROV_AUTH_OTHER;
    }
}

/* The station joins WPA2-PSK networks only (wpa2_client.c, review decision 9) */
static bool prov_auth_is_supported(uint8_t auth_mode)
{
    return (auth_mode == PROV_AUTH_WPA2_PSK) || (auth_mode == PROV_AUTH_WPA_WPA2_PSK) ||
           (auth_mode == PROV_AUTH_WPA2_WPA3_PSK);
}

/* Fills the portal list from the last scan: hidden networks dropped, one entry per SSID (strongest) */
static void prov_load_scan_records(void)
{
    uint16_t rec_count = 0U;
    const wifi_ap_record_t *recs = wifi_get_scan_records(&rec_count);
    s_prov_scan_count = 0U;
    for (uint16_t i = 0U; i < rec_count; i++)
    {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == '\0')
        {
            continue;
        }
        wifi_scan_item_t *item = NULL;
        for (uint16_t j = 0U; j < s_prov_scan_count; j++)
        {
            if (strcmp(s_prov_scan_items[j].ssid, ssid) == 0)
            {
                item = &s_prov_scan_items[j];
                break;
            }
        }
        if (item != NULL && item->rssi >= recs[i].rssi)
        {
            continue;
        }
        if (item == NULL)
        {
            if (s_prov_scan_count >= PROVISIONING_MAX_SCAN_APS)
            {
                continue;
            }
            item = &s_prov_scan_items[s_prov_scan_count++];
            prov_safe_copy(item->ssid, sizeof(item->ssid), ssid);
        }
        item->rssi = recs[i].rssi;
        item->channel = recs[i].primary;
        item->auth_mode = prov_auth_from_vendor(recs[i].authmode);
        memcpy(item->bssid, recs[i].bssid, sizeof(item->bssid));
    }
    s_prov_telemetry.ap_count = s_prov_scan_count;
}

/* ========================================================================= */
/* Core Subsystem Lifecycle & Configuration Management                       */
/* ========================================================================= */

provisioning_status_t provisioning_init(void)
{
    memset(&s_prov_creds, 0, sizeof(s_prov_creds));
    memset(&s_prov_telemetry, 0, sizeof(s_prov_telemetry));
    memset(s_prov_scan_items, 0, sizeof(s_prov_scan_items));
    s_prov_scan_count = 0U;

    /* Register HTTP provisioning routes */
    http_route_register("/setup", HTTP_METHOD_GET, provisioning_http_handler_setup);
    http_route_register("/api/wifi/scan", HTTP_METHOD_GET, provisioning_http_handler_scan);
    http_route_register("/api/wifi/configure", HTTP_METHOD_POST, provisioning_http_handler_configure);
    http_route_register("/api/wifi/status", HTTP_METHOD_GET, provisioning_http_handler_status);
    http_route_register("/api/wifi/credentials", HTTP_METHOD_GET, provisioning_http_handler_credentials);

    /* A re-init (do-test) must not drop a join in progress */
    if (!s_prov_initialized)
    {
        memset(&s_prov_join, 0, sizeof(s_prov_join));
        s_prov_join.state = PROV_JOIN_IDLE;
        s_prov_join.retry_delay_us = PROV_RETRY_MIN_US;
    }

    s_prov_initialized = true;
    return provisioning_reload_credentials();
}

/* Re-reads the saved credentials from NVS into the in-RAM state */
provisioning_status_t provisioning_reload_credentials(void)
{
    memset(&s_prov_creds, 0, sizeof(s_prov_creds));
    char saved_ssid[PROVISIONING_MAX_SSID_LEN + 1U];
    memset(saved_ssid, 0, sizeof(saved_ssid));
    if (nvs_get_str(PROV_NVS_KEY_SSID, saved_ssid, sizeof(saved_ssid)) == NVS_OK && saved_ssid[0] != '\0')
    {
        prov_safe_copy(s_prov_creds.ssid, sizeof(s_prov_creds.ssid), saved_ssid);
        nvs_get_str(PROV_NVS_KEY_PASS, s_prov_creds.passphrase, sizeof(s_prov_creds.passphrase));
        s_prov_creds.provisioned = true;
        s_prov_state = PROV_STATE_PROVISIONED;
        s_prov_telemetry.has_saved_credentials = true;
        prov_safe_copy(s_prov_telemetry.active_ssid, sizeof(s_prov_telemetry.active_ssid), saved_ssid);
    }
    else
    {
        s_prov_creds.provisioned = false;
        s_prov_state = PROV_STATE_UNPROVISIONED;
        s_prov_telemetry.has_saved_credentials = false;
        s_prov_telemetry.active_ssid[0] = '\0';
    }

    s_prov_telemetry.state = s_prov_state;
    return PROV_OK;
}

provisioning_status_t provisioning_start(void)
{
    if (!s_prov_initialized)
    {
        provisioning_init();
    }
    if (s_prov_state == PROV_STATE_ERROR)
    {
        s_prov_state = s_prov_creds.provisioned ? PROV_STATE_PROVISIONED : PROV_STATE_UNPROVISIONED;
    }
    s_prov_telemetry.state = s_prov_state;
    return PROV_OK;
}

provisioning_status_t provisioning_stop(void)
{
    if (!s_prov_initialized)
    {
        return PROV_OK;
    }
    s_prov_state = s_prov_creds.provisioned ? PROV_STATE_PROVISIONED : PROV_STATE_UNPROVISIONED;
    s_prov_telemetry.state = s_prov_state;
    return PROV_OK;
}

provisioning_state_t provisioning_get_state(void)
{
    return s_prov_state;
}

/* ========================================================================= */
/* Credential Persistence & Validation                                       */
/* ========================================================================= */

provisioning_status_t provisioning_set_credentials(const char *ssid, const char *passphrase)
{
    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    s_prov_telemetry.configs_received++;

    if (ssid == NULL || ssid[0] == '\0')
    {
        s_prov_telemetry.configs_rejected++;
        return PROV_ERR_SSID_EMPTY;
    }

    size_t slen = strlen(ssid);
    if (slen > PROVISIONING_MAX_SSID_LEN)
    {
        s_prov_telemetry.configs_rejected++;
        return PROV_ERR_INVALID_ARG;
    }

    size_t plen = (passphrase != NULL) ? strlen(passphrase) : 0U;
    if (plen > 0U && plen < PROVISIONING_MIN_PASS_LEN)
    {
        s_prov_telemetry.configs_rejected++;
        return PROV_ERR_PASS_TOO_SHORT;
    }
    if (plen > PROVISIONING_MAX_PASS_LEN)
    {
        s_prov_telemetry.configs_rejected++;
        return PROV_ERR_INVALID_ARG;
    }

    s_prov_state = PROV_STATE_CONFIGURING;
    s_prov_telemetry.state = s_prov_state;

    /* Write to NVS */
    if (nvs_set_str(PROV_NVS_KEY_SSID, ssid) != NVS_OK)
    {
        s_prov_state = PROV_STATE_ERROR;
        s_prov_telemetry.state = s_prov_state;
        s_prov_telemetry.configs_rejected++;
        return PROV_ERR_NVS_FAIL;
    }

    if (plen > 0U)
    {
        if (nvs_set_str(PROV_NVS_KEY_PASS, passphrase) != NVS_OK)
        {
            s_prov_state = PROV_STATE_ERROR;
            s_prov_telemetry.state = s_prov_state;
            s_prov_telemetry.configs_rejected++;
            return PROV_ERR_NVS_FAIL;
        }
    }
    else
    {
        nvs_erase_key(PROV_NVS_KEY_PASS);
    }

    nvs_set_u32(PROV_NVS_KEY_PROV, 1U);

    /* Update in-memory state */
    memset(&s_prov_creds, 0, sizeof(s_prov_creds));
    prov_safe_copy(s_prov_creds.ssid, sizeof(s_prov_creds.ssid), ssid);
    if (plen > 0U)
    {
        prov_safe_copy(s_prov_creds.passphrase, sizeof(s_prov_creds.passphrase), passphrase);
    }
    s_prov_creds.provisioned = true;

    s_prov_state = PROV_STATE_CONFIGURED;
    s_prov_telemetry.state = s_prov_state;
    s_prov_telemetry.configs_valid++;
    s_prov_telemetry.has_saved_credentials = true;
    prov_safe_copy(s_prov_telemetry.active_ssid, sizeof(s_prov_telemetry.active_ssid), ssid);

    return PROV_OK;
}

provisioning_status_t provisioning_get_credentials(wifi_credentials_t *out_creds)
{
    if (out_creds == NULL)
    {
        return PROV_ERR_INVALID_ARG;
    }
    if (!s_prov_initialized)
    {
        provisioning_init();
    }
    *out_creds = s_prov_creds;
    if (!s_prov_creds.provisioned)
    {
        return PROV_ERR_NOT_FOUND;
    }
    return PROV_OK;
}

provisioning_status_t provisioning_clear_credentials(void)
{
    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    nvs_erase_key(PROV_NVS_KEY_SSID);
    nvs_erase_key(PROV_NVS_KEY_PASS);
    nvs_erase_key(PROV_NVS_KEY_PROV);

    memset(&s_prov_creds, 0, sizeof(s_prov_creds));
    s_prov_creds.provisioned = false;

    s_prov_state = PROV_STATE_UNPROVISIONED;
    s_prov_telemetry.state = s_prov_state;
    s_prov_telemetry.has_saved_credentials = false;
    s_prov_telemetry.active_ssid[0] = '\0';

    return PROV_OK;
}

bool provisioning_has_credentials(void)
{
    if (!s_prov_initialized)
    {
        provisioning_init();
    }
    return s_prov_creds.provisioned;
}

/* ========================================================================= */
/* Station Join & Portal Hand-Over (REV-29)                                  */
/* ========================================================================= */

provisioning_status_t provisioning_request_join(bool keep_ap)
{
    if (!s_prov_initialized)
    {
        provisioning_init();
    }
    if (!s_prov_creds.provisioned)
    {
        return PROV_ERR_NOT_FOUND;
    }
    s_prov_join.state = PROV_JOIN_PENDING;
    s_prov_join.keep_ap = keep_ap && wifi_is_ap_active();
    s_prov_join.unproven = false;
    s_prov_join.next_event_us = 0U;
    s_prov_join.last_reason = 0U;
    s_prov_join.attempts = 0U;
    s_prov_join.retry_delay_us = PROV_RETRY_MIN_US;
    return PROV_OK;
}

void provisioning_restore_join(prov_join_state_t saved_state)
{
    if (saved_state == PROV_JOIN_IDLE || saved_state == PROV_JOIN_FAILED)
    {
        s_prov_join.state = saved_state;
        s_prov_join.keep_ap = false;
        s_prov_join.unproven = false;
    }
    else
    {
        /* The suite tore the link down: rejoin with the restored credentials */
        (void)provisioning_request_join(false);
    }
}

/* Drops a join that has not started yet (pending or waiting to retry) */
void provisioning_cancel_join(void)
{
    if (s_prov_join.state == PROV_JOIN_PENDING || s_prov_join.state == PROV_JOIN_RETRY_WAIT)
    {
        s_prov_join.state = PROV_JOIN_IDLE;
    }
}

bool provisioning_seed_from_config(const char *ssid, const char *passphrase)
{
    if (ssid == NULL || ssid[0] == '\0' || provisioning_has_credentials())
    {
        return false;
    }
    char seeded[PROVISIONING_MAX_SSID_LEN + 1U];
    memset(seeded, 0, sizeof(seeded));
    if (nvs_get_str(PROV_NVS_KEY_SEED, seeded, sizeof(seeded)) == NVS_OK && strcmp(seeded, ssid) == 0)
    {
        return false;
    }
    if (provisioning_set_credentials(ssid, passphrase) != PROV_OK)
    {
        return false;
    }
    (void)nvs_set_str(PROV_NVS_KEY_SEED, ssid);
    return true;
}

void provisioning_boot(void)
{
    /* First boot of an image built with STA_SSID/STA_PASSPHRASE in .config */
    if (provisioning_seed_from_config(CONFIG_WIFI_STA_SSID, CONFIG_WIFI_STA_PASSPHRASE))
    {
#if defined(__riscv)
        console_puts("[PROV] Wi-Fi credentials seeded from the build configuration\r\n");
#endif
    }
    if (provisioning_has_credentials())
    {
        /* Provisioned: never start the SoftAP, also not when the router is away (review 7.2) */
        (void)provisioning_request_join(false);
    }
#if CONFIG_WIFI_AUTO_START_AP
    else
    {
        /* Scan before the SoftAP is up: the portal list is ready and no client is disturbed */
        (void)provisioning_start_scan();
        wifi_start_ap(CONFIG_WIFI_AP_SSID, NULL, CONFIG_WIFI_AP_CHANNEL);
    }
#endif
}

/* Ends a failed attempt: a portal join gives up (SoftAP stays), a STA-only join retries with backoff */
static void prov_join_attempt_failed(uint64_t now_us, uint16_t reason)
{
    wpa2_telemetry_t wtel;
    s_prov_join.last_reason = reason;
    s_prov_join.wpa2_fail = (wpa2_client_get_telemetry(&wtel) == WPA2_OK) ? (uint8_t)wtel.last_fail
                                                                          : (uint8_t)WPA2_FAIL_NONE;
    if (s_prov_join.keep_ap)
    {
        (void)wifi_sta_abort_keep_ap();
        /* Unproven portal credentials must not survive a reboot: the board would retry them
         * forever without a setup SoftAP (provisioned boots never start it) */
        if (s_prov_join.unproven)
        {
            (void)provisioning_clear_credentials();
        }
        s_prov_join.state = PROV_JOIN_FAILED;
        return;
    }
    s_prov_join.state = PROV_JOIN_RETRY_WAIT;
    s_prov_join.next_event_us = now_us + s_prov_join.retry_delay_us;
    s_prov_join.retry_delay_us *= 2U;
    if (s_prov_join.retry_delay_us > PROV_RETRY_MAX_US)
    {
        s_prov_join.retry_delay_us = PROV_RETRY_MAX_US;
    }
}

/* The supplicant gave up on this attempt (e.g. no message 3: wrong passphrase). The blob keeps
 * retrying by itself (other nodes of a mesh) without a disconnect event, so check it directly. */
static bool prov_supplicant_failed(void)
{
    wpa2_telemetry_t wtel;
    return (wpa2_client_get_telemetry(&wtel) == WPA2_OK) && (wtel.last_fail != WPA2_FAIL_NONE);
}

/* Channel of the SSID in the last scan (0: unknown, the blob scans all channels) */
static uint8_t prov_scan_channel_of(const char *ssid)
{
    for (uint16_t i = 0U; i < s_prov_scan_count; i++)
    {
        if (strcmp(s_prov_scan_items[i].ssid, ssid) == 0)
        {
            return s_prov_scan_items[i].channel;
        }
    }
    return 0U;
}

void provisioning_tick(uint64_t now_us)
{
    bool disconnected = (wifi_get_sta_disconnect_count() != s_prov_join_disc_base);

    /* A portal rescan blocks for a few seconds; never while a join is in flight */
    if (s_prov_scan_requested && s_prov_join.state != PROV_JOIN_PENDING &&
        s_prov_join.state != PROV_JOIN_JOINING && s_prov_join.state != PROV_JOIN_HANDOVER)
    {
        s_prov_scan_requested = false;
        (void)provisioning_start_scan();
    }

    switch (s_prov_join.state)
    {
        case PROV_JOIN_PENDING:
        {
            /* Portal: the save reply is still on its way (TCP has no retransmission yet, REV-13) */
            if (s_prov_join.keep_ap && s_prov_join.next_event_us == 0U)
            {
                s_prov_join.next_event_us = now_us + PROV_PORTAL_JOIN_DELAY_US;
                break;
            }
            if (now_us < s_prov_join.next_event_us)
            {
                break;
            }
            s_prov_join.attempts++;
            s_prov_join_disc_base = wifi_get_sta_disconnect_count();
            s_prov_join.state = PROV_JOIN_JOINING;
            s_prov_join.next_event_us = now_us + PROV_JOIN_TIMEOUT_US;
            /* PBKDF2 runs here and takes seconds; it counts against the join timeout */
            /* Portal joins use the channel from the scan: the SoftAP leaves its channel only briefly */
            uint8_t channel = s_prov_join.keep_ap ? prov_scan_channel_of(s_prov_creds.ssid) : 0U;
            wpa2_status_t wst = wpa2_client_join(s_prov_creds.ssid, s_prov_creds.passphrase, channel,
                                                 s_prov_join.keep_ap);
            if (wst != WPA2_OK)
            {
                prov_join_attempt_failed(now_us, 0U);
            }
            break;
        }
        case PROV_JOIN_JOINING:
            if (disconnected || prov_supplicant_failed())
            {
                prov_join_attempt_failed(now_us, wifi_get_sta_last_disconnect_reason());
            }
            else if (wifi_is_sta_connected())
            {
                s_prov_join.unproven = false;
                if (s_prov_join.keep_ap)
                {
                    s_prov_join.state = PROV_JOIN_HANDOVER;
                    s_prov_join.next_event_us = now_us + PROV_HANDOVER_DELAY_US;
                }
                else
                {
                    s_prov_join.state = PROV_JOIN_ONLINE;
                    s_prov_join.retry_delay_us = PROV_RETRY_MIN_US;
                }
            }
            else if (now_us >= s_prov_join.next_event_us)
            {
                prov_join_attempt_failed(now_us, 0U);
            }
            break;
        case PROV_JOIN_HANDOVER:
            if (disconnected)
            {
                prov_join_attempt_failed(now_us, wifi_get_sta_last_disconnect_reason());
            }
            else if (now_us >= s_prov_join.next_event_us)
            {
                if (wifi_sta_take_over() == WIFI_OK)
                {
                    s_prov_join.state = PROV_JOIN_ONLINE;
                    s_prov_join.keep_ap = false;
                    s_prov_join.retry_delay_us = PROV_RETRY_MIN_US;
                }
                else
                {
                    s_prov_join.keep_ap = false;
                    prov_join_attempt_failed(now_us, wifi_get_sta_last_disconnect_reason());
                }
            }
            break;
        case PROV_JOIN_ONLINE:
            if (disconnected)
            {
                /* Link lost: rejoin with backoff (the full link manager is REV-12) */
                s_prov_join.last_reason = wifi_get_sta_last_disconnect_reason();
                s_prov_join_disc_base = wifi_get_sta_disconnect_count();
                s_prov_join.state = PROV_JOIN_RETRY_WAIT;
                s_prov_join.next_event_us = now_us + s_prov_join.retry_delay_us;
            }
            break;
        case PROV_JOIN_RETRY_WAIT:
            if (now_us >= s_prov_join.next_event_us)
            {
                s_prov_join.state = PROV_JOIN_PENDING;
            }
            break;
        default:
            break;
    }
}

provisioning_status_t provisioning_get_join(prov_join_info_t *out_info)
{
    if (out_info == NULL)
    {
        return PROV_ERR_INVALID_ARG;
    }
    *out_info = s_prov_join;
    return PROV_OK;
}

const char *provisioning_join_state_to_str(prov_join_state_t state)
{
    switch (state)
    {
        case PROV_JOIN_IDLE:       return "idle";
        case PROV_JOIN_PENDING:    return "pending";
        case PROV_JOIN_JOINING:    return "joining";
        case PROV_JOIN_HANDOVER:   return "connected";
        case PROV_JOIN_ONLINE:     return "online";
        case PROV_JOIN_RETRY_WAIT: return "retrying";
        case PROV_JOIN_FAILED:     return "failed";
        default:                   return "unknown";
    }
}

const char *provisioning_join_failure_str(const prov_join_info_t *info)
{
    if (info == NULL)
    {
        return "";
    }
    switch ((wpa2_fail_t)info->wpa2_fail)
    {
        case WPA2_FAIL_NO_M3:
            return "The network rejected the password";
        case WPA2_FAIL_UNSUPPORTED_SECURITY:
            return "Network security not supported (WPA2-PSK needed)";
        case WPA2_FAIL_NONE:
            return provisioning_reason_hint(info->last_reason);
        default:
            return wpa2_fail_to_str((wpa2_fail_t)info->wpa2_fail);
    }
}

const char *provisioning_reason_hint(uint16_t reason)
{
    switch (reason)
    {
        case 0U:
            return "No answer from the network (timed out)";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
            return "The network rejected the password";
        case WIFI_REASON_NO_AP_FOUND:
            return "Network not found (out of range, or 5 GHz only)";
        case WIFI_REASON_NO_AP_FOUND_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_AUTHMODE:
        case WIFI_REASON_AKMP_INVALID:
            return "Network security not supported (WPA2-PSK needed)";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "Signal lost";
        default:
            return wifi_disconnect_reason_str(reason);
    }
}

/* ========================================================================= */
/* Wi-Fi Scan Engine Integration                                             */
/* ========================================================================= */

provisioning_status_t provisioning_start_scan(void)
{
    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    s_prov_telemetry.scans_initiated++;
    s_prov_state = PROV_STATE_SCANNING;
    s_prov_telemetry.state = s_prov_state;

#if defined(__riscv)
    wifi_scan(NULL, 0U, false, 200U);
#endif
    prov_load_scan_records();

    s_prov_telemetry.scans_completed++;
    s_prov_state = s_prov_creds.provisioned ? PROV_STATE_PROVISIONED : PROV_STATE_UNPROVISIONED;
    s_prov_telemetry.state = s_prov_state;

    return PROV_OK;
}

provisioning_status_t provisioning_get_scan_results(wifi_scan_item_t *out_items, uint16_t max_items, uint16_t *out_count)
{
    if (out_items == NULL || out_count == NULL || max_items == 0U)
    {
        return PROV_ERR_INVALID_ARG;
    }
    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    uint16_t count = (s_prov_scan_count < max_items) ? s_prov_scan_count : max_items;
    for (uint16_t i = 0U; i < count; i++)
    {
        out_items[i] = s_prov_scan_items[i];
    }
    *out_count = count;
    return PROV_OK;
}

/* ========================================================================= */
/* Telemetry & Identity Utilities                                            */
/* ========================================================================= */

provisioning_status_t provisioning_get_telemetry(provisioning_telemetry_t *out_telemetry)
{
    if (out_telemetry == NULL)
    {
        return PROV_ERR_INVALID_ARG;
    }
    if (!s_prov_initialized)
    {
        provisioning_init();
    }
    s_prov_telemetry.state = s_prov_state;
    s_prov_telemetry.ap_count = s_prov_scan_count;
    *out_telemetry = s_prov_telemetry;
    return PROV_OK;
}

const char *provisioning_state_to_str(provisioning_state_t state)
{
    switch (state)
    {
        case PROV_STATE_UNPROVISIONED: return "UNPROVISIONED";
        case PROV_STATE_SCANNING:       return "SCANNING";
        case PROV_STATE_CONFIGURING:    return "CONFIGURING";
        case PROV_STATE_CONFIGURED:     return "CONFIGURED";
        case PROV_STATE_PROVISIONED:    return "PROVISIONED";
        case PROV_STATE_ERROR:          return "ERROR";
        default:                        return "UNKNOWN";
    }
}

const char *provisioning_auth_mode_to_str(uint8_t auth_mode)
{
    switch (auth_mode)
    {
        case PROV_AUTH_OPEN:            return "OPEN";
        case PROV_AUTH_WEP:             return "WEP";
        case PROV_AUTH_WPA_PSK:         return "WPA-PSK";
        case PROV_AUTH_WPA2_PSK:        return "WPA2-PSK";
        case PROV_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
        case PROV_AUTH_WPA2_ENTERPRISE: return "WPA2-ENTERPRISE";
        case PROV_AUTH_WPA3_PSK:        return "WPA3-PSK";
        case PROV_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3-PSK";
        default:                        return "UNKNOWN";
    }
}

/* ========================================================================= */
/* Flash XIP Diagnostic Visualizers (.flash.text)                            */
/* ========================================================================= */

void provisioning_print_status(void)
{
    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    console_puts("========================================\r\n");
    console_puts(" SoftAP Wi-Fi Provisioning Engine\r\n");
    console_puts("========================================\r\n");
    console_puts(" State:        ");
    console_puts(provisioning_state_to_str(s_prov_state));
    console_puts("\r\n");
    console_puts(" Provisioned:  ");
    console_puts(s_prov_creds.provisioned ? "YES" : "NO");
    console_puts("\r\n");
    console_puts(" Active SSID:  ");
    if (s_prov_creds.provisioned && s_prov_creds.ssid[0] != '\0')
    {
        console_puts(s_prov_creds.ssid);
    }
    else
    {
        console_puts("(None - Unconfigured)");
    }
    console_puts("\r\n");
    console_puts(" SoftAP SSID:  ");
    console_puts(wifi_get_ap_ssid());
    console_puts("\r\n");
    console_puts(" SoftAP State: ");
    console_puts(wifi_is_ap_active() ? "BROADCASTING" : "STANDBY");
    console_puts("\r\n");
    console_puts(" Scans Run:    ");
    char num[16];
    prov_u32_to_dec(s_prov_telemetry.scans_completed, num, sizeof(num));
    console_puts(num);
    console_puts("\r\n");
    console_puts(" Configs Rx:   ");
    prov_u32_to_dec(s_prov_telemetry.configs_received, num, sizeof(num));
    console_puts(num);
    console_puts(" (Valid: ");
    prov_u32_to_dec(s_prov_telemetry.configs_valid, num, sizeof(num));
    console_puts(num);
    console_puts(", Rejected: ");
    prov_u32_to_dec(s_prov_telemetry.configs_rejected, num, sizeof(num));
    console_puts(num);
    console_puts(")\r\n");
    console_puts(" Join:         ");
    console_puts(provisioning_join_state_to_str(s_prov_join.state));
    if (s_prov_join.state == PROV_JOIN_FAILED || s_prov_join.state == PROV_JOIN_RETRY_WAIT)
    {
        console_puts(" (");
        console_puts(provisioning_join_failure_str(&s_prov_join));
        console_puts(")");
    }
    console_puts(", attempts ");
    prov_u32_to_dec(s_prov_join.attempts, num, sizeof(num));
    console_puts(num);
    console_puts("\r\n");
    console_puts(" Scan Cache:   ");
    prov_u32_to_dec(s_prov_scan_count, num, sizeof(num));
    console_puts(num);
    console_puts(" APs cached\r\n");
    console_puts("========================================\r\n");
}

void provisioning_print_scan(void)
{
    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    console_puts("========================================\r\n");
    console_puts(" Cached Wi-Fi Scan Results\r\n");
    console_puts("========================================\r\n");
    if (s_prov_scan_count == 0U)
    {
        console_puts("  (No scan results available)\r\n");
    }
    else
    {
        char num[16];
        for (uint16_t i = 0U; i < s_prov_scan_count; i++)
        {
            console_puts("  #");
            prov_u32_to_dec((uint32_t)(i + 1U), num, sizeof(num));
            console_puts(num);
            console_puts(": '");
            console_puts(s_prov_scan_items[i].ssid);
            console_puts("' | RSSI: ");
            prov_i32_to_dec((int32_t)s_prov_scan_items[i].rssi, num, sizeof(num));
            console_puts(num);
            console_puts(" dBm | Ch: ");
            prov_u32_to_dec((uint32_t)s_prov_scan_items[i].channel, num, sizeof(num));
            console_puts(num);
            console_puts(" | Auth: ");
            console_puts(provisioning_auth_mode_to_str(s_prov_scan_items[i].auth_mode));
            console_puts("\r\n");
        }
    }
    console_puts("========================================\r\n");
}

/* ========================================================================= */
/* Embedded HTTP Route Handlers                                              */
/* ========================================================================= */

/* GET /setup -> Serves responsive Captive Portal HTML web form */
void provisioning_http_handler_setup(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U) return;

    size_t asset_len = strlen(g_setup_html);
    if (asset_len >= max_len)
    {
        asset_len = max_len - 1U;
    }
    memcpy(response_body, g_setup_html, asset_len);
    response_body[asset_len] = '\0';
}

/* GET /api/wifi/scan -> JSON list of networks from the last scan. ?refresh=1 queues a new
 * scan that runs from provisioning_tick() after this reply ("scanning":true until it is done);
 * scanning inside the request would block it for seconds and the client would resend it. */
void provisioning_http_handler_scan(const char *query_params, char *response_body, size_t max_len)
{
    if (response_body == NULL || max_len == 0U) return;

    if (!s_prov_initialized)
    {
        provisioning_init();
    }
    if ((query_params != NULL) && (strstr(query_params, PROV_SCAN_REFRESH_PARAM) != NULL))
    {
        s_prov_scan_requested = true;
    }

    response_body[0] = '\0';
    prov_str_append(response_body, max_len, "{\"status\":\"ok\",\"scanning\":");
    prov_str_append(response_body, max_len, s_prov_scan_requested ? "true" : "false");
    prov_str_append(response_body, max_len, ",\"count\":");
    char num[16];
    prov_u32_to_dec((uint32_t)s_prov_scan_count, num, sizeof(num));
    prov_str_append(response_body, max_len, num);
    prov_str_append(response_body, max_len, ",\"aps\":[");

    for (uint16_t i = 0U; i < s_prov_scan_count; i++)
    {
        if (i > 0U) prov_str_append(response_body, max_len, ",");
        prov_str_append(response_body, max_len, "{\"ssid\":\"");
        prov_json_append_str(response_body, max_len, s_prov_scan_items[i].ssid);
        prov_str_append(response_body, max_len, "\",\"rssi\":");
        prov_i32_to_dec((int32_t)s_prov_scan_items[i].rssi, num, sizeof(num));
        prov_str_append(response_body, max_len, num);
        prov_str_append(response_body, max_len, ",\"channel\":");
        prov_u32_to_dec((uint32_t)s_prov_scan_items[i].channel, num, sizeof(num));
        prov_str_append(response_body, max_len, num);
        prov_str_append(response_body, max_len, ",\"auth\":\"");
        prov_str_append(response_body, max_len, provisioning_auth_mode_to_str(s_prov_scan_items[i].auth_mode));
        prov_str_append(response_body, max_len, "\",\"supported\":");
        prov_str_append(response_body, max_len,
                        prov_auth_is_supported(s_prov_scan_items[i].auth_mode) ? "true" : "false");
        prov_str_append(response_body, max_len, "}");
    }

    prov_str_append(response_body, max_len, "]}\r\n");
}

/* POST /api/wifi/configure -> Parses SSID & passphrase, persists to NVS */
void provisioning_http_handler_configure(const char *query_params, char *response_body, size_t max_len)
{
    if (response_body == NULL || max_len == 0U) return;

    char ssid[PROVISIONING_MAX_SSID_LEN + 1U];
    char pass[PROVISIONING_MAX_PASS_LEN + 1U];
    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));

    bool found_ssid = prov_extract_param(query_params, "ssid", ssid, sizeof(ssid));
    bool found_pass = prov_extract_param(query_params, "password", pass, sizeof(pass));
    if (!found_pass)
    {
        /* Try alt key: pass */
        found_pass = prov_extract_param(query_params, "pass", pass, sizeof(pass));
    }

    if (!found_ssid || ssid[0] == '\0')
    {
        response_body[0] = '\0';
        prov_str_append(response_body, max_len,
            "{\"status\":\"error\",\"provisioned\":false,\"error\":\"Missing or empty SSID\"}\r\n");
        return;
    }

    provisioning_status_t status = provisioning_set_credentials(ssid, found_pass ? pass : "");
    if (status == PROV_OK)
    {
        /* Join on the next tick, after this reply went out; from the portal the SoftAP stays up */
        if (provisioning_request_join(true) == PROV_OK)
        {
            s_prov_join.unproven = true;
        }
        response_body[0] = '\0';
        prov_str_append(response_body, max_len,
            "{\"status\":\"ok\",\"provisioned\":true,\"joining\":true,\"ssid\":\"");
        prov_json_append_str(response_body, max_len, ssid);
        prov_str_append(response_body, max_len, "\",\"message\":\"Credentials saved, joining\"}\r\n");
    }
    else
    {
        response_body[0] = '\0';
        prov_str_append(response_body, max_len,
            "{\"status\":\"error\",\"provisioned\":false,\"error\":\"");
        if (status == PROV_ERR_PASS_TOO_SHORT)
        {
            prov_str_append(response_body, max_len, "Passphrase must be at least 8 characters");
        }
        else
        {
            prov_str_append(response_body, max_len, "Failed to persist credentials to NVS");
        }
        prov_str_append(response_body, max_len, "\"}\r\n");
    }
}

/* GET /api/wifi/status -> provisioning, station join and SoftAP status JSON.
 * join.state: idle | pending | joining | connected (hand-over pending) | online | retrying | failed */
void provisioning_http_handler_status(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U) return;

    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    char num[16];
    response_body[0] = '\0';
    prov_str_append(response_body, max_len, "{\"status\":\"ok\",\"state\":\"");
    prov_str_append(response_body, max_len, provisioning_state_to_str(s_prov_state));
    prov_str_append(response_body, max_len, "\",\"provisioned\":");
    prov_str_append(response_body, max_len, s_prov_creds.provisioned ? "true" : "false");
    prov_str_append(response_body, max_len, ",\"ssid\":\"");
    prov_json_append_str(response_body, max_len, s_prov_creds.provisioned ? s_prov_creds.ssid : "");
    prov_str_append(response_body, max_len, "\",\"hostname\":\"" CONFIG_DEVICE_HOSTNAME ".local\"");

    prov_str_append(response_body, max_len, ",\"join\":{\"state\":\"");
    prov_str_append(response_body, max_len, provisioning_join_state_to_str(s_prov_join.state));
    prov_str_append(response_body, max_len, "\",\"attempts\":");
    prov_u32_to_dec(s_prov_join.attempts, num, sizeof(num));
    prov_str_append(response_body, max_len, num);
    prov_str_append(response_body, max_len, ",\"handover_delay_s\":");
    prov_u32_to_dec((uint32_t)(PROV_HANDOVER_DELAY_US / PROV_US_PER_SECOND), num, sizeof(num));
    prov_str_append(response_body, max_len, num);
    if (s_prov_join.state == PROV_JOIN_FAILED || s_prov_join.state == PROV_JOIN_RETRY_WAIT)
    {
        prov_str_append(response_body, max_len, ",\"reason\":");
        prov_u32_to_dec((uint32_t)s_prov_join.last_reason, num, sizeof(num));
        prov_str_append(response_body, max_len, num);
        prov_str_append(response_body, max_len, ",\"message\":\"");
        prov_json_append_str(response_body, max_len, provisioning_join_failure_str(&s_prov_join));
        prov_str_append(response_body, max_len, "\"");
    }
    dhcp_client_telemetry_t dcli;
    if (s_prov_join.state == PROV_JOIN_ONLINE && dhcp_client_get_telemetry(&dcli) == DHCP_OK &&
        dcli.state == DHCP_CLIENT_STATE_BOUND)
    {
        char ip[NET_IP_STR_BUF_LEN];
        net_ip_to_str(dcli.assigned_ip, ip, sizeof(ip));
        prov_str_append(response_body, max_len, ",\"ip\":\"");
        prov_str_append(response_body, max_len, ip);
        prov_str_append(response_body, max_len, "\"");
    }
    prov_str_append(response_body, max_len, "}");

    prov_str_append(response_body, max_len, ",\"softap\":{\"active\":");
    prov_str_append(response_body, max_len, wifi_is_ap_active() ? "true" : "false");
    prov_str_append(response_body, max_len, ",\"ssid\":\"");
    prov_json_append_str(response_body, max_len, wifi_get_ap_ssid());
    prov_str_append(response_body, max_len, "\"}}\r\n");
}

/* GET /api/wifi/credentials -> Returns configured Wi-Fi SSID JSON */
void provisioning_http_handler_credentials(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U) return;

    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    response_body[0] = '\0';
    prov_str_append(response_body, max_len, "{\"status\":\"ok\",\"provisioned\":");
    prov_str_append(response_body, max_len, s_prov_creds.provisioned ? "true" : "false");
    prov_str_append(response_body, max_len, ",\"ssid\":\"");
    prov_json_append_str(response_body, max_len, s_prov_creds.provisioned ? s_prov_creds.ssid : "");
    prov_str_append(response_body, max_len, "\"}\r\n");
}

/* ========================================================================= */
/* Host Testing Mock Interface                                               */
/* ========================================================================= */
#if !defined(__riscv)
void provisioning_mock_reset(void)
{
    s_prov_state = PROV_STATE_UNPROVISIONED;
    memset(&s_prov_creds, 0, sizeof(s_prov_creds));
    memset(&s_prov_telemetry, 0, sizeof(s_prov_telemetry));
    memset(s_prov_scan_items, 0, sizeof(s_prov_scan_items));
    s_prov_scan_count = 0U;
    s_prov_initialized = false;
    memset(&s_prov_join, 0, sizeof(s_prov_join));
    s_prov_join_disc_base = wifi_get_sta_disconnect_count();
    s_prov_scan_requested = false;
}
#endif
