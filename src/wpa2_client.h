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
 * - PTK/GTK installation into the MAC through the blob, group key rekeying
 *
 * Follows ESP-IDF 66ab063a9a7f components/wpa_supplicant (wpa.c, esp_wpa_main.c),
 * restricted to WPA2-PSK with CCMP pairwise and group ciphers and PMF off.
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
#define WPA2_KEY_IV_LEN                     16U
#define WPA2_KEY_RSC_LEN                    8U
#define WPA2_KEY_ID_LEN                     8U
#define WPA2_GTK_RSC_LEN                    6U      /* CCMP packet number */
#define WPA2_GTK_LEN                        16U     /* CCMP group key */
#define WPA2_MAC_ADDR_LEN                   6U
#define WPA2_MAX_SSID_LEN                   32U
#define WPA2_MAX_PASS_LEN                   64U
#define WPA2_MIN_PASS_LEN                   8U
#define WPA2_PBKDF2_ITERATIONS              4096U
#define WPA2_AES_KEYWRAP_BLOCK              8U
#define WPA2_AES_KEYWRAP_MIN_LEN            24U     /* IV + two 64-bit blocks */
#define WPA2_KEY_DATA_MAX_LEN               256U

/* Association RSN element buffer handed to the blob by reference:
 * struct wifi_appie header + IE (ESP-IDF assoc_ie_buf, ASSOC_IE_LEN + 2) */
#define WPA2_ASSOC_APPIE_LEN                48U

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

/* Key Information of the frames we send (ESP-IDF wpa.c send_2_of_4/4_of_4/2_of_2) */
#define WPA2_MSG2_KEY_INFO_NOMINAL          (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_PAIRWISE | WPA2_KEY_INFO_MIC)
#define WPA2_MSG4_KEY_INFO_NOMINAL          (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_PAIRWISE | WPA2_KEY_INFO_MIC | WPA2_KEY_INFO_SECURE)
#define WPA2_GROUP2_KEY_INFO_NOMINAL        (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_MIC | WPA2_KEY_INFO_SECURE)
/* Frames the AP sends, for tests and logs */
#define WPA2_MSG1_KEY_INFO_NOMINAL          (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_PAIRWISE | WPA2_KEY_INFO_ACK)
#define WPA2_MSG3_KEY_INFO_NOMINAL          (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_PAIRWISE | WPA2_KEY_INFO_INSTALL | \
                                             WPA2_KEY_INFO_ACK | WPA2_KEY_INFO_MIC | WPA2_KEY_INFO_SECURE | WPA2_KEY_INFO_ENCRYPTED)
#define WPA2_GROUP1_KEY_INFO_NOMINAL        (WPA2_KEY_INFO_KEY_DESC_V2 | WPA2_KEY_INFO_ACK | WPA2_KEY_INFO_MIC | \
                                             WPA2_KEY_INFO_SECURE | WPA2_KEY_INFO_ENCRYPTED)

/* RFC 3394 AES Key Wrap Initial Value */
#define WPA2_AES_KEYWRAP_IV_HI              0xA6A6A6A6U
#define WPA2_AES_KEYWRAP_IV_LO              0xA6A6A6A6U

/* ========================================================================= */
/* WPA2 Client State & Status Enumerations                                   */
/* ========================================================================= */
typedef enum {
    WPA2_STATE_DISCONNECTED = 0,
    WPA2_STATE_CONNECTING,          /* RSN IE registered, association in progress */
    WPA2_STATE_4WAY_M1_RECEIVED,
    WPA2_STATE_4WAY_M2_SENT,
    WPA2_STATE_4WAY_M3_RECEIVED,
    WPA2_STATE_4WAY_M4_SENT,        /* keys installed when the blob confirms M4 TX */
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
    WPA2_ERR_TX_FAIL                = -9,
    WPA2_ERR_UNSUPPORTED            = -10,
    WPA2_ERR_PROTOCOL               = -11
} wpa2_status_t;

/* Why the last join attempt stopped; shown by `sta status` */
typedef enum {
    WPA2_FAIL_NONE = 0,
    WPA2_FAIL_UNSUPPORTED_SECURITY, /* not WPA2-PSK with CCMP pairwise and group */
    WPA2_FAIL_NO_PMK,               /* no passphrase configured for this SSID */
    WPA2_FAIL_NO_M3,                /* M2 sent, AP never answered: wrong passphrase? */
    WPA2_FAIL_ANONCE_MISMATCH,
    WPA2_FAIL_IE_MISMATCH,          /* RSN IE in M3 differs from the beacon */
    WPA2_FAIL_BAD_KEY_DATA,         /* key data not decryptable or no CCMP GTK */
    WPA2_FAIL_M4_TX                 /* blob reported M4 TX failure */
} wpa2_fail_t;

/* ========================================================================= */
/* Concrete Protocol Structures                                              */
/* ========================================================================= */
/* Ethernet header in front of transmitted EAPOL frames (the blob strips it on RX) */
typedef struct {
    uint8_t  dest_mac[WPA2_MAC_ADDR_LEN];
    uint8_t  src_mac[WPA2_MAC_ADDR_LEN];
    uint16_t ethertype;
} __attribute__((packed)) eapol_eth_hdr_t;

/* IEEE 802.1X header: start of the buffer the blob passes to wpa_sta_rx_eapol */
typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint16_t length;
} __attribute__((packed)) eapol_1x_hdr_t;

typedef struct {
    uint8_t  descriptor_type;
    uint16_t key_info;
    uint16_t key_length;
    uint8_t  replay_counter[WPA2_REPLAY_LEN];
    uint8_t  key_nonce[WPA2_NONCE_LEN];
    uint8_t  key_iv[WPA2_KEY_IV_LEN];
    uint8_t  key_rsc[WPA2_KEY_RSC_LEN];
    uint8_t  key_id[WPA2_KEY_ID_LEN];
    uint8_t  key_mic[WPA2_MIC_LEN];
    uint16_t key_data_length;
} __attribute__((packed)) eapol_key_header_t;

#define WPA2_EAPOL_KEY_FRAME_MIN_LEN        (sizeof(eapol_1x_hdr_t) + sizeof(eapol_key_header_t))

typedef struct {
    uint8_t kck[WPA2_KCK_LEN];
    uint8_t kek[WPA2_KEK_LEN];
    uint8_t tk[WPA2_TK_LEN];
    uint8_t tx_mic[8];
    uint8_t rx_mic[8];
} wpa2_ptk_t;

typedef struct {
    wpa2_state_t state;
    wpa2_fail_t  last_fail;
    char         target_ssid[WPA2_MAX_SSID_LEN + 1U];
    uint8_t      target_bssid[WPA2_MAC_ADDR_LEN];
    uint8_t      local_mac[WPA2_MAC_ADDR_LEN];
    bool         has_pmk;
    bool         has_ptk;           /* PTK installed in the MAC */
    bool         has_gtk;           /* GTK installed in the MAC */
    uint8_t      gtk_keyidx;
    uint32_t     handshakes_attempted;
    uint32_t     handshakes_completed;
    uint32_t     group_rekeys;
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

/* Blob callbacks (struct wpa_funcs, eapol tx-done), wired in wifi_os_adapter.c */
int           wpa2_client_sta_connect(const uint8_t *bssid);
void          wpa2_client_on_associated(const uint8_t *bssid);
void          wpa2_client_on_disconnected(uint8_t reason);
wpa2_status_t wpa2_client_rx_eapol(const uint8_t *src_mac, const uint8_t *frame, uint16_t len);
void          wpa2_client_eapol_txdone(uint8_t *eapol, size_t len, bool tx_failure);

/* Handover & Home LAN Join Orchestrator */
wpa2_status_t wpa2_client_handover(const char *ssid, const char *passphrase);
wpa2_status_t wpa2_client_handover_chan(const char *ssid, const char *passphrase, uint8_t channel);
/* keep_ap: join while the SoftAP and the IP stack on it stay up (portal; wifi_sta_take_over) */
wpa2_status_t wpa2_client_join(const char *ssid, const char *passphrase, uint8_t channel, bool keep_ap);

/* Cryptographic Engines & Test Vectors */
wpa2_status_t wpa2_crypto_pbkdf2_sha1(const char *passphrase, const char *ssid,
                                     uint32_t iterations, uint8_t *out_pmk);
wpa2_status_t wpa2_crypto_prf512(const uint8_t *pmk, const uint8_t *mac1, const uint8_t *mac2,
                                 const uint8_t *nonce1, const uint8_t *nonce2,
                                 wpa2_ptk_t *out_ptk);
/* MIC over an EAPOL frame starting at the 802.1X header; the MIC field is
 * treated as zero */
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
const char *wpa2_fail_to_str(wpa2_fail_t fail);

#ifdef __cplusplus
}
#endif

#endif /* IRON_V_WPA2_CLIENT_H */
