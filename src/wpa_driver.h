/*
 * src/wpa_driver.h
 *
 * The Wi-Fi blob calls the supplicant needs, as one thin interface.
 * Target: implemented in wifi_os_adapter.c on top of the blob's *_internal API
 * (ESP-IDF 66ab063a9a7f esp_wifi_driver.h). Host tests: recorded by stubs in the
 * same file so the handshake logic can run without a radio.
 */
#ifndef IRON_V_WPA_DRIVER_H
#define IRON_V_WPA_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* enum wpa_alg (esp_wifi_driver.h) */
#define WPA_DRV_ALG_CCMP                    3

/* enum key_flag (esp_wifi_driver.h) */
#define WPA_DRV_KEY_FLAG_RX                 (1U << 2)
#define WPA_DRV_KEY_FLAG_TX                 (1U << 3)
#define WPA_DRV_KEY_FLAG_GROUP              (1U << 4)
#define WPA_DRV_KEY_FLAG_PAIRWISE           (1U << 5)

/* enum for esp_wifi_sta_get_prof_authmode_internal() (esp_wifi_driver.h) */
#define WPA_DRV_AUTH_WPA2_PSK               0x05U

/* esp_wifi_sta_get_{pairwise,group}_cipher_internal() return a bit index of
 * the supplicant cipher bitfield: WPA_CIPHER_CCMP = BIT(3) */
#define WPA_DRV_CIPHER_IDX_TKIP             1U
#define WPA_DRV_CIPHER_IDX_CCMP             3U

/* struct wifi_appie {uint16_t ie_len; uint8_t ie_data[];} header in front of the IE */
#define WPA_DRV_APPIE_HDR_LEN               2U

/* IEEE 802.11 reason codes we send */
#define WPA_DRV_REASON_UNSPECIFIED          1U
#define WPA_DRV_REASON_IE_IN_4WAY_DIFFERS   17U

typedef struct {
    bool    is_rsn;             /* AP profile is RSN (WPA2/WPA3), not WPA1/WAPI */
    uint8_t authmode;           /* WPA_DRV_AUTH_* */
    uint8_t pairwise_idx;       /* WPA_DRV_CIPHER_IDX_* */
    uint8_t group_idx;
    char    ssid[33];
} wpa_drv_profile_t;

/* Blob STA profile chosen for this BSS */
void wpa_drv_get_profile(wpa_drv_profile_t *out);
/* AP RSN element from the scan/beacon, NULL if unknown */
const uint8_t *wpa_drv_get_ap_rsn_ie(const uint8_t *bssid);
/* Register the association RSN element. appie points at a struct wifi_appie
 * buffer the blob keeps a reference to (ESP-IDF set_assoc_ie). */
int  wpa_drv_set_assoc_ie(uint8_t *appie, uint16_t ie_len);
/* Continue authentication/association with this BSS */
int  wpa_drv_sta_connect(const uint8_t *bssid);
int  wpa_drv_set_sta_key(int alg, const uint8_t *addr, int key_idx, int set_tx,
                         const uint8_t *seq, size_t seq_len,
                         const uint8_t *key, size_t key_len, uint32_t key_flag);
/* 4-way handshake done: the blob opens the port and posts STA_CONNECTED */
void wpa_drv_auth_done(void);
void wpa_drv_deauthenticate(uint8_t reason);
/* Ethernet frame (destination, source, ethertype, 802.1X) on the STA interface */
int  wpa_drv_tx_eapol(const uint8_t *eth_frame, uint16_t len);
/* Hardware RNG */
void wpa_drv_random(uint8_t *buf, size_t len);

#if !defined(__riscv)
/* Host test hooks */
typedef struct {
    wpa_drv_profile_t profile;
    const uint8_t    *ap_rsn_ie;
    uint32_t          assoc_ie_calls;
    uint8_t           assoc_ie[64];
    uint16_t          assoc_ie_len;
    uint32_t          connect_calls;
    uint32_t          set_key_calls;
    uint32_t          last_key_flag;
    int               last_key_idx;
    uint8_t           last_key[32];
    size_t            last_key_len;
    uint8_t           last_seq[8];
    uint32_t          auth_done_calls;
    uint32_t          deauth_calls;
    uint8_t           last_deauth_reason;
    uint32_t          tx_calls;
    uint8_t           last_tx[256];
    uint16_t          last_tx_len;
    uint8_t           random_fill;
} wpa_drv_host_t;
extern wpa_drv_host_t g_wpa_drv_host;
void wpa_drv_host_reset(void);
#endif

#endif /* IRON_V_WPA_DRIVER_H */
