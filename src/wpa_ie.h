/*
 * src/wpa_ie.h
 *
 * WPA/RSN information element and EAPOL-Key KDE parsing (IEEE 802.11-2020 9.4.2.24,
 * 12.7.2). Pure logic, no hardware access; host-tested.
 *
 * Bit values match the supplicant's own (ESP-IDF 66ab063a9a7f
 * components/wpa_supplicant/src/common/defs.h), because the Wi-Fi blob reads
 * key_mgmt and proto from wifi_wpa_ie_t as these bitfields.
 */
#ifndef IRON_V_WPA_IE_H
#define IRON_V_WPA_IE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Element IDs */
#define WLAN_EID_RSN                        48U
#define WLAN_EID_VENDOR_SPECIFIC            221U
#define WLAN_EID_RSNX                       244U

#define WPA_IE_HDR_LEN                      2U      /* element ID + length */
#define WPA_IE_MAX_LEN                      (WPA_IE_HDR_LEN + 255U)
#define RSN_VERSION                         1U
#define WPA_VERSION                         1U
#define RSN_SELECTOR_LEN                    4U
#define RSN_PMKID_LEN                       16U
#define RSNX_CAPA_OFFSET                    2U

/* Suite selectors, OUI (3 bytes) + type, big-endian */
#define RSN_SELECTOR(a, b, c, d) \
    ((((uint32_t)(a)) << 24) | (((uint32_t)(b)) << 16) | (((uint32_t)(c)) << 8) | (uint32_t)(d))
#define RSN_OUI_00_0F_AC(t)                 RSN_SELECTOR(0x00U, 0x0FU, 0xACU, (t))
#define WPA_OUI_00_50_F2(t)                 RSN_SELECTOR(0x00U, 0x50U, 0xF2U, (t))

#define RSN_CIPHER_SUITE_NONE               RSN_OUI_00_0F_AC(0U)
#define RSN_CIPHER_SUITE_WEP40              RSN_OUI_00_0F_AC(1U)
#define RSN_CIPHER_SUITE_TKIP               RSN_OUI_00_0F_AC(2U)
#define RSN_CIPHER_SUITE_CCMP               RSN_OUI_00_0F_AC(4U)
#define RSN_CIPHER_SUITE_WEP104             RSN_OUI_00_0F_AC(5U)
#define RSN_CIPHER_SUITE_AES_128_CMAC       RSN_OUI_00_0F_AC(6U)
#define RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED RSN_OUI_00_0F_AC(7U)
#define RSN_CIPHER_SUITE_GCMP               RSN_OUI_00_0F_AC(8U)
#define RSN_CIPHER_SUITE_GCMP_256           RSN_OUI_00_0F_AC(9U)
#define RSN_CIPHER_SUITE_BIP_GMAC_128       RSN_OUI_00_0F_AC(11U)
#define RSN_CIPHER_SUITE_BIP_GMAC_256       RSN_OUI_00_0F_AC(12U)

#define RSN_AKM_8021X                       RSN_OUI_00_0F_AC(1U)
#define RSN_AKM_PSK                         RSN_OUI_00_0F_AC(2U)
#define RSN_AKM_FT_8021X                    RSN_OUI_00_0F_AC(3U)
#define RSN_AKM_FT_PSK                      RSN_OUI_00_0F_AC(4U)
#define RSN_AKM_8021X_SHA256                RSN_OUI_00_0F_AC(5U)
#define RSN_AKM_PSK_SHA256                  RSN_OUI_00_0F_AC(6U)
#define RSN_AKM_SAE                         RSN_OUI_00_0F_AC(8U)
#define RSN_AKM_FT_SAE                      RSN_OUI_00_0F_AC(9U)
#define RSN_AKM_8021X_SUITE_B               RSN_OUI_00_0F_AC(11U)
#define RSN_AKM_8021X_SUITE_B_192           RSN_OUI_00_0F_AC(12U)
#define RSN_AKM_OWE                         RSN_OUI_00_0F_AC(18U)
#define RSN_AKM_SAE_EXT_KEY                 RSN_OUI_00_0F_AC(24U)

#define WPA_OUI_TYPE                        WPA_OUI_00_50_F2(1U)
#define WPA_CIPHER_SUITE_NONE               WPA_OUI_00_50_F2(0U)
#define WPA_CIPHER_SUITE_WEP40              WPA_OUI_00_50_F2(1U)
#define WPA_CIPHER_SUITE_TKIP               WPA_OUI_00_50_F2(2U)
#define WPA_CIPHER_SUITE_CCMP               WPA_OUI_00_50_F2(4U)
#define WPA_CIPHER_SUITE_WEP104             WPA_OUI_00_50_F2(5U)
#define WPA_AKM_8021X                       WPA_OUI_00_50_F2(1U)
#define WPA_AKM_PSK                         WPA_OUI_00_50_F2(2U)
#define WPA_AKM_NONE                        WPA_OUI_00_50_F2(0U)

/* Supplicant bitfields (ESP-IDF defs.h) */
#define WPA_BIT(n)                          (1U << (n))
#define WPA_CIPHER_NONE                     WPA_BIT(0)
#define WPA_CIPHER_TKIP                     WPA_BIT(1)
#define WPA_CIPHER_CCMP                     WPA_BIT(3)
#define WPA_CIPHER_AES_128_CMAC             WPA_BIT(5)
#define WPA_CIPHER_WEP40                    WPA_BIT(7)
#define WPA_CIPHER_WEP104                   WPA_BIT(8)
#define WPA_CIPHER_SMS4                     WPA_BIT(10)
#define WPA_CIPHER_GCMP                     WPA_BIT(11)
#define WPA_CIPHER_GCMP_256                 WPA_BIT(12)
#define WPA_CIPHER_BIP_GMAC_128             WPA_BIT(13)
#define WPA_CIPHER_BIP_GMAC_256             WPA_BIT(14)
#define WPA_CIPHER_GTK_NOT_USED             WPA_BIT(15)

#define WPA_KEY_MGMT_IEEE8021X              WPA_BIT(0)
#define WPA_KEY_MGMT_PSK                    WPA_BIT(1)
#define WPA_KEY_MGMT_WPA_NONE               WPA_BIT(4)
#define WPA_KEY_MGMT_FT_IEEE8021X           WPA_BIT(5)
#define WPA_KEY_MGMT_FT_PSK                 WPA_BIT(6)
#define WPA_KEY_MGMT_IEEE8021X_SHA256       WPA_BIT(7)
#define WPA_KEY_MGMT_PSK_SHA256             WPA_BIT(8)
#define WPA_KEY_MGMT_SAE                    WPA_BIT(10)
#define WPA_KEY_MGMT_FT_SAE                 WPA_BIT(11)
#define WPA_KEY_MGMT_IEEE8021X_SUITE_B      WPA_BIT(16)
#define WPA_KEY_MGMT_IEEE8021X_SUITE_B_192  WPA_BIT(17)
#define WPA_KEY_MGMT_OWE                    WPA_BIT(22)
#define WPA_KEY_MGMT_SAE_EXT_KEY            WPA_BIT(26)

#define WPA_PROTO_WPA                       WPA_BIT(0)
#define WPA_PROTO_RSN                       WPA_BIT(1)

/* RSN capabilities */
#define WPA_CAPABILITY_MFPR                 WPA_BIT(6)
#define WPA_CAPABILITY_MFPC                 WPA_BIT(7)

/* EAPOL-Key data encapsulation (KDE), 12.7.2 Table 12-9 */
#define WPA_KDE_TYPE                        WLAN_EID_VENDOR_SPECIFIC
#define WPA_KDE_HDR_LEN                     6U      /* type, length, OUI, data type */
#define WPA_KDE_GTK                         RSN_OUI_00_0F_AC(1U)
#define WPA_KDE_GTK_INFO_LEN                2U      /* KeyID/Tx byte + reserved byte */
#define WPA_KDE_GTK_KEYID_MASK              0x03U
#define WPA_KDE_GTK_TX_BIT                  0x04U
#define WPA_KDE_GTK_MAX_LEN                 32U

/* Parse result, same meaning as ESP-IDF struct wpa_ie_data */
typedef struct {
    uint32_t       proto;
    uint32_t       pairwise_cipher;
    uint32_t       group_cipher;
    uint32_t       key_mgmt;
    uint16_t       capabilities;
    size_t         num_pmkid;
    const uint8_t *pmkid;
    uint32_t       mgmt_group_cipher;
    uint8_t        rsnxe_capa;
} wpa_ie_data_t;

typedef enum {
    WPA_IE_OK               =  0,
    WPA_IE_ERR_EMPTY        = -1,
    WPA_IE_ERR_MALFORMED    = -2,
    WPA_IE_ERR_GROUP        = -3,
    WPA_IE_ERR_PAIRWISE     = -4,
    WPA_IE_ERR_SHORT_AKM    = -5,
    WPA_IE_ERR_AKM          = -6,
    WPA_IE_ERR_SHORT_CAPAB  = -7,
    WPA_IE_ERR_PMKID        = -9,
    WPA_IE_ERR_MGMT_GROUP   = -10
} wpa_ie_status_t;

/* Decrypted key data of message 3/4 or group message 1/2 */
typedef struct {
    const uint8_t *rsn_ie;
    size_t         rsn_ie_len;
    const uint8_t *gtk;
    size_t         gtk_len;
    uint8_t        gtk_keyidx;
    bool           gtk_tx;
} wpa_kde_t;

/*
 * Parse an RSN, RSNX or WPA (vendor) element including its 2-byte header.
 * strict_mgmt_group: reject unknown management group ciphers (association
 * path); scan results pass false.
 */
int wpa_ie_parse(const uint8_t *ie, size_t ie_len, wpa_ie_data_t *out, bool strict_mgmt_group);

/* Build the RSN element we send in the association request and message 2/4.
 * Returns its length including the header, or 0 if buf is too small. */
size_t wpa_ie_build_rsn(uint8_t *buf, size_t buf_len, uint32_t pairwise_cipher,
                        uint32_t group_cipher, uint32_t key_mgmt, uint16_t capabilities);

/* Walk decrypted EAPOL-Key data; stops at the 0xDD 0x00 padding. */
int wpa_kde_parse(const uint8_t *buf, size_t len, wpa_kde_t *out);

#endif /* IRON_V_WPA_IE_H */
