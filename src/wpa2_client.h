/*
 * src/wpa2_client.h
 *
 * Bare-Metal Wi-Fi Station (STA) WPA2-PSK Client & 802.11i 4-Way Handshake Engine
 * Espressif ESP32-C6 RISC-V SoC Bare-Metal Kernel Runtime
 *
 * Implements IEEE 802.11i / WPA2-Personal authentication:
 * - PBKDF2-HMAC-SHA1 Pairwise Master Key (PMK) derivation (RFC 2898 / RFC 8018)
 * - PRF-512 Pseudo-Random Function for Pairwise Transient Key (PTK) expansion
 * - 4-Way EAPOL-Key handshake state machine (Message 1..4 exchange)
 * - HMAC-SHA1-128 Key Confirmation Key (KCK) Message Integrity Code (MIC) verification
 * - RFC 3394 AES Key Unwrap using Key Encryption Key (KEK) for Group Temporal Key (GTK)
 * - Hardware MAC CCMP key installation and smooth SoftAP-to-Station handover.
 */

#ifndef IRON_V_WPA2_CLIENT_H
#define IRON_V_WPA2_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Cryptographic & Geometry Constants (Zero Magic Numbers)                   */
/* ========================================================================= */
#define WPA2_PMK_LEN                        32U
#define WPA2_PTK_LEN                        64U
#define WPA2_KCK_LEN                        16U
#define WPA2_KEK_LEN                        16U
#define WPA2_TK_LEN                         16U
#define WPA2_MIC_LEN                        16U
#define WPA2_NONCE_LEN                      32U
#define WPA2_REPLAY_LEN                     8U
#define WPA2_GTK_LEN                        16U
#define WPA2_MAC_ADDR_LEN                   6U
#define WPA2_MAX_SSID_LEN                   32U
#define WPA2_MAX_PASS_LEN                   64U
#define WPA2_MIN_PASS_LEN                   8U
#define WPA2_PBKDF2_ITERATIONS              4096U
#define WPA2_HANDSHAKE_TIMEOUT_US           5000000ULL /* 5.0 seconds */

/* EAPOL & 802.1X Protocol Constants */
#define ETHERTYPE_EAPOL                     0x888EU
#define EAPOL_VERSION_1                     1U
#define EAPOL_VERSION_2                     2U
#define EAPOL_TYPE_EAP                      0U
#define EAPOL_TYPE_START                    1U
#define EAPOL_TYPE_LOGOFF                   2U
#define EAPOL_TYPE_KEY                      3U
#define EAPOL_DESC_TYPE_RSN                 2U
#define EAPOL_DESC_TYPE_WPA                 254U

/* 802.11i EAPOL-Key Information Bitfield Definitions */
#define WPA2_KEY_INFO_KEY_DESC_MASK         0x0007U
#define WPA2_KEY_INFO_KEY_DESC_V2           0x0002U /* HMAC-SHA1-128 + AES Key Wrap */
#define WPA2_KEY_INFO_PAIRWISE              (1U << 3)
#define WPA2_KEY_INFO_INSTALL               (1U << 6)
#define WPA2_KEY_INFO_ACK                   (1U << 7)
#define WPA2_KEY_INFO_MIC                   (1U << 8)
#define WPA2_KEY_INFO_SECURE                (1U << 9)
#define WPA2_KEY_INFO_ERROR                 (1U << 10)
#define WPA2_KEY_INFO_REQUEST               (1U << 11)
#define WPA2_KEY_INFO_ENCRYPTED             (1U << 12)
#define WPA2_KEY_INFO_SMC                   (1U << 13)

/* Expected Nominal Key Information Combinations */
#define WPA2_MSG1_KEY_INFO_NOMINAL          (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_PAIRWISE | WPA2_KEY_INFO_ACK)
#define WPA2_MSG2_KEY_INFO_NOMINAL          (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_PAIRWISE | WPA2_KEY_INFO_MIC)
#define WPA2_MSG3_KEY_INFO_NOMINAL          (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_PAIRWISE | WPA2_KEY_INFO_INSTALL | WPA2_KEY_INFO_ACK | WPA2_KEY_INFO_MIC)
#define WPA2_MSG4_KEY_INFO_NOMINAL          (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_PAIRWISE | WPA2_KEY_INFO_MIC | WPA2_KEY_INFO_SECURE)

/* RFC 3394 AES Key Wrap Initial Value */
#define WPA2_AES_KEYWRAP_IV_HI              0xA6A6A6A6U
#define WPA2_AES_KEYWRAP_IV_LO              0xA6A6A6A6U

/* ========================================================================= */
/* WPA2 Client State & Status Enumerations                                   */
/* ========================================================================= */
typedef enum {
    WPA2_STATE_DISCONNECTED = 0,
    WPA2_STATE_CONNECTING,
    WPA2_STATE_4WAY_M1_RECEIVED,
    WPA2_STATE_4WAY_M2_SENT,
    WPA2_STATE_4WAY_M3_RECEIVED,
    WPA2_STATE_4WAY_M4_SENT,
    WPA2_STATE_AUTHENTICATED,
    WPA2_STATE_FAILED
} wpa2_state_t;

typedef enum {
    WPA2_OK                         =  0,
    WPA2_ERR_INVALID_ARG            = -1,
    WPA2_ERR_STATE                  = -2,
    WPA2_ERR_MIC_FAIL               = -3,
    WPA2_ERR_REPLAY                 = -4,
    WPA2_ERR_DECRYPT_FAIL           = -5,
    WPA2_ERR_TIMEOUT                = -6,
    WPA2_ERR_NOT_FOUND              = -7,
    WPA2_ERR_BUFFER_SMALL           = -8,
    WPA2_ERR_TX_FAIL                = -9
} wpa2_status_t;

/* ========================================================================= */
/* Concrete Protocol Structures                                              */
/* ========================================================================= */
typedef struct {
    uint8_t  dest_mac[WPA2_MAC_ADDR_LEN];
    uint8_t  src_mac[WPA2_MAC_ADDR_LEN];
    uint16_t ethertype;
    uint8_t  version;
    uint8_t  type;
    uint16_t length;
} __attribute__((packed)) eapol_ethernet_hdr_t;

typedef struct {
    uint8_t  descriptor_type;
    uint16_t key_info;
    uint16_t key_length;
    uint8_t  replay_counter[WPA2_REPLAY_LEN];
    uint8_t  key_nonce[WPA2_NONCE_LEN];
    uint8_t  key_iv[16];
    uint8_t  key_rsc[8];
    uint8_t  key_id[8];
    uint8_t  key_mic[WPA2_MIC_LEN];
    uint16_t key_data_length;
} __attribute__((packed)) eapol_key_header_t;

typedef struct {
    uint8_t kck[WPA2_KCK_LEN];
    uint8_t kek[WPA2_KEK_LEN];
    uint8_t tk[WPA2_TK_LEN];
    uint8_t tx_mic[8];
    uint8_t rx_mic[8];
} wpa2_ptk_t;

typedef struct {
    wpa2_state_t state;
    char         target_ssid[WPA2_MAX_SSID_LEN + 1U];
    uint8_t      target_bssid[WPA2_MAC_ADDR_LEN];
    uint8_t      local_mac[WPA2_MAC_ADDR_LEN];
    bool         has_pmk;
    bool         has_ptk;
    bool         has_gtk;
    uint32_t     handshakes_attempted;
    uint32_t     handshakes_completed;
    uint32_t     mic_failures;
    uint32_t     replay_errors;
    uint32_t     m1_rx_count;
    uint32_t     m2_tx_count;
    uint32_t     m3_rx_count;
    uint32_t     m4_tx_count;
    uint64_t     last_handshake_duration_us;
} wpa2_telemetry_t;

/* ========================================================================= */
/* Public API Declarations                                                   */
/* ========================================================================= */

/* Subsystem Lifecycle */
wpa2_status_t wpa2_client_init(void);
wpa2_status_t wpa2_client_configure(const char *ssid, const char *passphrase);
wpa2_status_t wpa2_client_start(void);
wpa2_status_t wpa2_client_stop(void);
wpa2_state_t  wpa2_client_get_state(void);
bool          wpa2_client_is_in_4way(void);
bool          wpa2_client_is_authenticated(void);
wpa2_status_t wpa2_client_get_telemetry(wpa2_telemetry_t *out_telem);

/* Vendor Callback Hooks */
wpa2_status_t wpa2_client_rx_eapol(const uint8_t *src_mac, const uint8_t *frame, uint16_t len);
void          wpa2_client_on_connected(const uint8_t *bssid);
void          wpa2_client_on_disconnected(uint8_t reason);

/* Handover & Home LAN Join Orchestrator */
wpa2_status_t wpa2_client_handover(const char *ssid, const char *passphrase);
wpa2_status_t wpa2_client_handover_chan(const char *ssid, const char *passphrase, uint8_t channel);

/* Cryptographic Engines & Test Vectors */
wpa2_status_t wpa2_crypto_pbkdf2_sha1(const char *passphrase, const char *ssid,
                                     uint32_t iterations, uint8_t *out_pmk);
wpa2_status_t wpa2_crypto_prf512(const uint8_t *pmk, const uint8_t *mac1, const uint8_t *mac2,
                                 const uint8_t *nonce1, const uint8_t *nonce2,
                                 wpa2_ptk_t *out_ptk);
wpa2_status_t wpa2_crypto_compute_mic(const uint8_t *kck, const uint8_t *eapol_frame,
                                      uint16_t frame_len, uint8_t *out_mic);
wpa2_status_t wpa2_crypto_aes_unwrap(const uint8_t *kek, const uint8_t *wrapped,
                                     uint16_t wrapped_len, uint8_t *out_plain,
                                     uint16_t *out_plain_len);
wpa2_status_t wpa2_crypto_aes_wrap(const uint8_t *kek, const uint8_t *plain,
                                   uint16_t plain_len, uint8_t *out_wrapped,
                                   uint16_t *out_wrapped_len);
wpa2_status_t wpa2_client_get_ptk(wpa2_ptk_t *out_ptk);

/* Diagnostic Visualizer (Flash XIP) */
void wpa2_client_print_status(void);
const char *wpa2_state_to_str(wpa2_state_t state);


#ifdef __cplusplus
}
#endif

#endif /* IRON_V_WPA2_CLIENT_H */
