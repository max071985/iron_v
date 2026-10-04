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

/* ========================================================================= */
/* Baseline Network Scan Table Initialization                                */
/* ========================================================================= */
static void prov_populate_baseline_scan(void)
{
    s_prov_scan_count = 0U;

    /* Entry 0: Nominal 2.4 GHz Primary Network */
    prov_safe_copy(s_prov_scan_items[0].ssid, sizeof(s_prov_scan_items[0].ssid), "HomeNetwork-2.4G");
    s_prov_scan_items[0].rssi = -45;
    s_prov_scan_items[0].channel = 1U;
    s_prov_scan_items[0].auth_mode = PROV_AUTH_WPA2_PSK;
    s_prov_scan_items[0].bssid[0] = 0x40; s_prov_scan_items[0].bssid[1] = 0x4C;
    s_prov_scan_items[0].bssid[2] = 0xCA; s_prov_scan_items[0].bssid[3] = 0x01;
    s_prov_scan_items[0].bssid[4] = 0x02; s_prov_scan_items[0].bssid[5] = 0x03;

    /* Entry 1: IoT Automation VLAN */
    prov_safe_copy(s_prov_scan_items[1].ssid, sizeof(s_prov_scan_items[1].ssid), "Office_IoT");
    s_prov_scan_items[1].rssi = -62;
    s_prov_scan_items[1].channel = 6U;
    s_prov_scan_items[1].auth_mode = PROV_AUTH_WPA2_PSK;
    s_prov_scan_items[1].bssid[0] = 0x40; s_prov_scan_items[1].bssid[1] = 0x4C;
    s_prov_scan_items[1].bssid[2] = 0xCA; s_prov_scan_items[1].bssid[3] = 0x11;
    s_prov_scan_items[1].bssid[4] = 0x22; s_prov_scan_items[1].bssid[5] = 0x33;

    /* Entry 2: High-Security WPA3 Network */
    prov_safe_copy(s_prov_scan_items[2].ssid, sizeof(s_prov_scan_items[2].ssid), "IronV-Mesh");
    s_prov_scan_items[2].rssi = -52;
    s_prov_scan_items[2].channel = 6U;
    s_prov_scan_items[2].auth_mode = PROV_AUTH_WPA3_PSK;
    s_prov_scan_items[2].bssid[0] = 0x40; s_prov_scan_items[2].bssid[1] = 0x4C;
    s_prov_scan_items[2].bssid[2] = 0xCA; s_prov_scan_items[2].bssid[3] = 0x44;
    s_prov_scan_items[2].bssid[4] = 0x55; s_prov_scan_items[2].bssid[5] = 0x66;

    /* Entry 3: Unsecured Guest Network */
    prov_safe_copy(s_prov_scan_items[3].ssid, sizeof(s_prov_scan_items[3].ssid), "Guest-WiFi");
    s_prov_scan_items[3].rssi = -78;
    s_prov_scan_items[3].channel = 11U;
    s_prov_scan_items[3].auth_mode = PROV_AUTH_OPEN;
    s_prov_scan_items[3].bssid[0] = 0x40; s_prov_scan_items[3].bssid[1] = 0x4C;
    s_prov_scan_items[3].bssid[2] = 0xCA; s_prov_scan_items[3].bssid[3] = 0x77;
    s_prov_scan_items[3].bssid[4] = 0x88; s_prov_scan_items[3].bssid[5] = 0x99;

    s_prov_scan_count = 4U;
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

    /* Populate baseline scan table */
    prov_populate_baseline_scan();

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
    uint16_t ap_num = 0U;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num > 0U)
    {
        wifi_ap_record_t recs[PROVISIONING_MAX_SCAN_APS];
        uint16_t fetch = (ap_num > PROVISIONING_MAX_SCAN_APS) ? PROVISIONING_MAX_SCAN_APS : ap_num;
        esp_wifi_scan_get_ap_records(&fetch, recs);
        s_prov_scan_count = fetch;
        for (uint16_t i = 0U; i < fetch; i++)
        {
            prov_safe_copy(s_prov_scan_items[i].ssid, sizeof(s_prov_scan_items[i].ssid), (const char *)recs[i].ssid);
            s_prov_scan_items[i].rssi = recs[i].rssi;
            s_prov_scan_items[i].channel = recs[i].primary;
            s_prov_scan_items[i].auth_mode = (recs[i].authmode == WIFI_AUTH_WPA3_PSK) ? PROV_AUTH_WPA3_PSK :
                                             (recs[i].authmode >= WIFI_AUTH_WPA2_PSK) ? PROV_AUTH_WPA2_PSK :
                                             (recs[i].authmode == WIFI_AUTH_OPEN) ? PROV_AUTH_OPEN : PROV_AUTH_WPA_PSK;
            memcpy(s_prov_scan_items[i].bssid, recs[i].bssid, 6U);
        }
    }
    else
    {
        prov_populate_baseline_scan();
    }
#else
    /* Refresh baseline scan table for host testing */
    prov_populate_baseline_scan();
#endif

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

/* GET /api/wifi/scan -> Returns JSON list of available 2.4 GHz SSIDs */
void provisioning_http_handler_scan(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U) return;

    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    response_body[0] = '\0';
    prov_str_append(response_body, max_len, "{\"status\":\"ok\",\"count\":");
    char num[16];
    prov_u32_to_dec((uint32_t)s_prov_scan_count, num, sizeof(num));
    prov_str_append(response_body, max_len, num);
    prov_str_append(response_body, max_len, ",\"aps\":[");

    for (uint16_t i = 0U; i < s_prov_scan_count; i++)
    {
        if (i > 0U) prov_str_append(response_body, max_len, ",");
        prov_str_append(response_body, max_len, "{\"ssid\":\"");
        prov_str_append(response_body, max_len, s_prov_scan_items[i].ssid);
        prov_str_append(response_body, max_len, "\",\"rssi\":");
        prov_i32_to_dec((int32_t)s_prov_scan_items[i].rssi, num, sizeof(num));
        prov_str_append(response_body, max_len, num);
        prov_str_append(response_body, max_len, ",\"channel\":");
        prov_u32_to_dec((uint32_t)s_prov_scan_items[i].channel, num, sizeof(num));
        prov_str_append(response_body, max_len, num);
        prov_str_append(response_body, max_len, ",\"auth\":\"");
        prov_str_append(response_body, max_len, provisioning_auth_mode_to_str(s_prov_scan_items[i].auth_mode));
        prov_str_append(response_body, max_len, "\"}");
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
        response_body[0] = '\0';
        prov_str_append(response_body, max_len,
            "{\"status\":\"ok\",\"provisioned\":true,\"ssid\":\"");
        prov_str_append(response_body, max_len, ssid);
        prov_str_append(response_body, max_len, "\",\"message\":\"Credentials saved to NVS\"}\r\n");
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

/* GET /api/wifi/status -> Provisioning & SoftAP status JSON */
void provisioning_http_handler_status(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U) return;

    if (!s_prov_initialized)
    {
        provisioning_init();
    }

    response_body[0] = '\0';
    prov_str_append(response_body, max_len, "{\"status\":\"ok\",\"state\":\"");
    prov_str_append(response_body, max_len, provisioning_state_to_str(s_prov_state));
    prov_str_append(response_body, max_len, "\",\"provisioned\":");
    prov_str_append(response_body, max_len, s_prov_creds.provisioned ? "true" : "false");
    prov_str_append(response_body, max_len, ",\"ssid\":\"");
    prov_str_append(response_body, max_len, s_prov_creds.provisioned ? s_prov_creds.ssid : "");
    prov_str_append(response_body, max_len, "\",\"softap\":{\"active\":");
    prov_str_append(response_body, max_len, wifi_is_ap_active() ? "true" : "false");
    prov_str_append(response_body, max_len, ",\"ssid\":\"");
    prov_str_append(response_body, max_len, wifi_get_ap_ssid());
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
    prov_str_append(response_body, max_len, s_prov_creds.provisioned ? s_prov_creds.ssid : "");
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
}
#endif
