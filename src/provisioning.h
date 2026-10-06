/*
 * src/provisioning.h
 *
 * SoftAP Captive Portal Wi-Fi Provisioning Engine
 * Espressif ESP32-C6 RISC-V SoC Bare-Metal Kernel Runtime
 *
 * Implements interactive 2.4 GHz Wi-Fi network scanning, captive portal
 * web setup (/setup), secure credential configuration (POST /api/wifi/configure),
 * and persistent wear-leveled NVS storage for automated Station (STA) onboarding.
 */

#ifndef IRON_V_PROVISIONING_H
#define IRON_V_PROVISIONING_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Provisioning Limits & Sizing Constants                                    */
/* ========================================================================= */
#define PROVISIONING_MAX_SCAN_APS        16U
#define PROVISIONING_MAX_SSID_LEN        32U
#define PROVISIONING_MAX_PASS_LEN        64U
#define PROVISIONING_MIN_PASS_LEN        8U

/* Join and hand-over timing (REV-29) */
#define PROV_JOIN_TIMEOUT_US             30000000ULL   /* one STA join attempt, PBKDF2 included */
#define PROV_HANDOVER_DELAY_US           15000000ULL   /* portal shows the result before the SoftAP goes */
#define PROV_PORTAL_JOIN_DELAY_US        1000000ULL    /* let the save reply out before the radio retunes */
#define PROV_RETRY_MIN_US                5000000ULL    /* first retry after a lost or failed boot join */
#define PROV_RETRY_MAX_US                300000000ULL  /* retry backoff cap */
#define PROV_US_PER_SECOND               1000000ULL
#define PROV_SCAN_REFRESH_PARAM          "refresh"     /* GET /api/wifi/scan?refresh=1 rescans */

/* NVS Storage Keys */
#define PROV_NVS_KEY_SSID                "wifi_ssid"
#define PROV_NVS_KEY_PASS                "wifi_pass"
#define PROV_NVS_KEY_PROV                "wifi_prov"
#define PROV_NVS_KEY_SEED                "wifi_seed"   /* SSID last seeded from .config (REV-30) */

/* ========================================================================= */
/* Provisioning Enumerations                                                 */
/* ========================================================================= */
typedef enum {
    PROV_STATE_UNPROVISIONED = 0,
    PROV_STATE_SCANNING,
    PROV_STATE_CONFIGURING,
    PROV_STATE_CONFIGURED,
    PROV_STATE_PROVISIONED,
    PROV_STATE_ERROR
} provisioning_state_t;

typedef enum {
    PROV_OK                      =  0,
    PROV_ERR_INVALID_ARG         = -1,
    PROV_ERR_NVS_FAIL            = -2,
    PROV_ERR_SCAN_BUSY           = -3,
    PROV_ERR_SSID_EMPTY          = -4,
    PROV_ERR_PASS_TOO_SHORT      = -5,
    PROV_ERR_NOT_FOUND           = -6,
    PROV_ERR_BUFFER_TOO_SMALL    = -7
} provisioning_status_t;

/* Station join driven by provisioning (portal save, boot with saved credentials) */
typedef enum {
    PROV_JOIN_IDLE = 0,      /* nothing requested */
    PROV_JOIN_PENDING,       /* requested; starts on the next provisioning_tick() */
    PROV_JOIN_JOINING,       /* association and 4-way handshake in progress */
    PROV_JOIN_HANDOVER,      /* joined; SoftAP still up so the portal can show the result */
    PROV_JOIN_ONLINE,        /* STA owns the IP stack (DHCP lease follows) */
    PROV_JOIN_RETRY_WAIT,    /* boot join failed or link lost; retry after a backoff */
    PROV_JOIN_FAILED         /* portal join failed; SoftAP stays, user can try again */
} prov_join_state_t;

typedef struct {
    prov_join_state_t state;
    bool     keep_ap;          /* portal join: SoftAP stays up until the hand-over */
    bool     unproven;         /* credentials came from the portal and never joined: forget on failure */
    uint16_t last_reason;      /* last STA disconnect reason (0: timeout or none) */
    uint8_t  wpa2_fail;        /* supplicant verdict at the failure (wpa2_fail_t) */
    uint32_t attempts;
    uint64_t next_event_us;    /* JOINING timeout, HANDOVER time or RETRY_WAIT end */
    uint64_t retry_delay_us;
} prov_join_info_t;

typedef enum {
    PROV_AUTH_OPEN               = 0,
    PROV_AUTH_WEP                = 1,
    PROV_AUTH_WPA_PSK            = 2,
    PROV_AUTH_WPA2_PSK           = 3,
    PROV_AUTH_WPA_WPA2_PSK       = 4,
    PROV_AUTH_WPA2_ENTERPRISE    = 5,
    PROV_AUTH_WPA3_PSK           = 6,
    PROV_AUTH_WPA2_WPA3_PSK      = 7,
    PROV_AUTH_OTHER              = 8    /* OWE, WAPI, WPA3-Enterprise, ... */
} provisioning_auth_mode_t;

/* ========================================================================= */
/* Provisioning Data Structures                                              */
/* ========================================================================= */
typedef struct {
    char    ssid[PROVISIONING_MAX_SSID_LEN + 1U];
    int8_t  rssi;
    uint8_t channel;
    uint8_t auth_mode;
    uint8_t bssid[6];
} wifi_scan_item_t;

typedef struct {
    char ssid[PROVISIONING_MAX_SSID_LEN + 1U];
    char passphrase[PROVISIONING_MAX_PASS_LEN + 1U];
    bool provisioned;
} wifi_credentials_t;

typedef struct {
    provisioning_state_t state;
    uint32_t scans_initiated;
    uint32_t scans_completed;
    uint32_t configs_received;
    uint32_t configs_valid;
    uint32_t configs_rejected;
    uint16_t ap_count;
    bool     has_saved_credentials;
    char     active_ssid[PROVISIONING_MAX_SSID_LEN + 1U];
} provisioning_telemetry_t;

/* ========================================================================= */
/* Public Provisioning Management APIs                                       */
/* ========================================================================= */

/* Core lifecycle & state control */
provisioning_status_t provisioning_init(void);
provisioning_status_t provisioning_reload_credentials(void);
provisioning_status_t provisioning_start(void);
provisioning_status_t provisioning_stop(void);
provisioning_state_t  provisioning_get_state(void);

/* Credential Configuration & NVS Persistence */
provisioning_status_t provisioning_set_credentials(const char *ssid, const char *passphrase);
provisioning_status_t provisioning_get_credentials(wifi_credentials_t *out_creds);
provisioning_status_t provisioning_clear_credentials(void);
bool                  provisioning_has_credentials(void);

/* Wi-Fi Network Scanning Integration */
provisioning_status_t provisioning_start_scan(void);
provisioning_status_t provisioning_get_scan_results(wifi_scan_item_t *out_items, uint16_t max_items, uint16_t *out_count);

/* Station join (REV-29). keep_ap: portal join with hand-over, else STA only */
provisioning_status_t provisioning_request_join(bool keep_ap);
void                  provisioning_cancel_join(void);
/* do-test: put the join back as it was before the suite (drop test joins; rejoin if it was active) */
void                  provisioning_restore_join(prov_join_state_t saved_state);
/* Boot: join with saved credentials, else start the setup SoftAP (CONFIG_WIFI_AUTO_START_AP) */
void                  provisioning_boot(void);
/* Seeds NVS from the build-time CONFIG_WIFI_STA_* once: only with no saved credentials and only
 * for an SSID not seeded before (a later `prov clear` is respected). True when it seeded. */
bool                  provisioning_seed_from_config(const char *ssid, const char *passphrase);
void                  provisioning_tick(uint64_t now_us);
provisioning_status_t provisioning_get_join(prov_join_info_t *out_info);
const char           *provisioning_join_state_to_str(prov_join_state_t state);
/* Short user-facing explanation of a disconnect reason */
const char           *provisioning_reason_hint(uint16_t reason);
/* Explanation of the last failed join: the supplicant's verdict first, else the reason */
const char           *provisioning_join_failure_str(const prov_join_info_t *info);

/* Telemetry & Identity */
provisioning_status_t provisioning_get_telemetry(provisioning_telemetry_t *out_telemetry);
const char           *provisioning_state_to_str(provisioning_state_t state);
const char           *provisioning_auth_mode_to_str(uint8_t auth_mode);

/* Flash XIP Diagnostic Visualizers */
void provisioning_print_status(void);
void provisioning_print_scan(void);

/* Embedded HTTP Route Handlers */
void provisioning_http_handler_setup(const char *query_params, char *response_body, size_t max_len);
void provisioning_http_handler_scan(const char *query_params, char *response_body, size_t max_len);
void provisioning_http_handler_configure(const char *query_params, char *response_body, size_t max_len);
void provisioning_http_handler_status(const char *query_params, char *response_body, size_t max_len);
void provisioning_http_handler_credentials(const char *query_params, char *response_body, size_t max_len);

/* Host Testing / Simulation Interfaces */
#if !defined(__riscv)
void provisioning_mock_reset(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IRON_V_PROVISIONING_H */
