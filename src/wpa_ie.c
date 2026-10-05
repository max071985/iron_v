/*
 * src/wpa_ie.c
 *
 * WPA/RSN information element and KDE parsing. Follows ESP-IDF 66ab063a9a7f
 * wpa_common.c (wpa_parse_wpa_ie_rsn_common, wpa_parse_wpa_ie_wpa) and
 * wpa_ie.c (wpa_supplicant_parse_ies) so the blob sees the values it expects.
 */
#include "wpa_ie.h"
#include "string.h"

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static uint32_t rsn_cipher_bit(uint32_t sel)
{
    switch (sel)
    {
        case RSN_CIPHER_SUITE_NONE:               return WPA_CIPHER_NONE;
        case RSN_CIPHER_SUITE_WEP40:              return WPA_CIPHER_WEP40;
        case RSN_CIPHER_SUITE_TKIP:               return WPA_CIPHER_TKIP;
        case RSN_CIPHER_SUITE_CCMP:               return WPA_CIPHER_CCMP;
        case RSN_CIPHER_SUITE_WEP104:             return WPA_CIPHER_WEP104;
        case RSN_CIPHER_SUITE_GCMP:               return WPA_CIPHER_GCMP;
        case RSN_CIPHER_SUITE_GCMP_256:           return WPA_CIPHER_GCMP_256;
        case RSN_CIPHER_SUITE_AES_128_CMAC:       return WPA_CIPHER_AES_128_CMAC;
        case RSN_CIPHER_SUITE_BIP_GMAC_128:       return WPA_CIPHER_BIP_GMAC_128;
        case RSN_CIPHER_SUITE_BIP_GMAC_256:       return WPA_CIPHER_BIP_GMAC_256;
        case RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED: return WPA_CIPHER_GTK_NOT_USED;
        default:                                  return 0U;
    }
}

static uint32_t rsn_akm_bit(uint32_t sel)
{
    switch (sel)
    {
        case RSN_AKM_8021X:             return WPA_KEY_MGMT_IEEE8021X;
        case RSN_AKM_PSK:               return WPA_KEY_MGMT_PSK;
        case RSN_AKM_FT_8021X:          return WPA_KEY_MGMT_FT_IEEE8021X;
        case RSN_AKM_FT_PSK:            return WPA_KEY_MGMT_FT_PSK;
        case RSN_AKM_8021X_SHA256:      return WPA_KEY_MGMT_IEEE8021X_SHA256;
        case RSN_AKM_PSK_SHA256:        return WPA_KEY_MGMT_PSK_SHA256;
        case RSN_AKM_SAE:               return WPA_KEY_MGMT_SAE;
        case RSN_AKM_FT_SAE:            return WPA_KEY_MGMT_FT_SAE;
        case RSN_AKM_8021X_SUITE_B:     return WPA_KEY_MGMT_IEEE8021X_SUITE_B;
        case RSN_AKM_8021X_SUITE_B_192: return WPA_KEY_MGMT_IEEE8021X_SUITE_B_192;
        case RSN_AKM_OWE:               return WPA_KEY_MGMT_OWE;
        case RSN_AKM_SAE_EXT_KEY:       return WPA_KEY_MGMT_SAE_EXT_KEY;
        default:                        return 0U;
    }
}

static uint32_t wpa_cipher_bit(uint32_t sel)
{
    switch (sel)
    {
        case WPA_CIPHER_SUITE_NONE:   return WPA_CIPHER_NONE;
        case WPA_CIPHER_SUITE_WEP40:  return WPA_CIPHER_WEP40;
        case WPA_CIPHER_SUITE_TKIP:   return WPA_CIPHER_TKIP;
        case WPA_CIPHER_SUITE_CCMP:   return WPA_CIPHER_CCMP;
        case WPA_CIPHER_SUITE_WEP104: return WPA_CIPHER_WEP104;
        default:                      return 0U;
    }
}

static uint32_t wpa_akm_bit(uint32_t sel)
{
    switch (sel)
    {
        case WPA_AKM_8021X: return WPA_KEY_MGMT_IEEE8021X;
        case WPA_AKM_PSK:   return WPA_KEY_MGMT_PSK;
        case WPA_AKM_NONE:  return WPA_KEY_MGMT_WPA_NONE;
        default:            return 0U;
    }
}

static bool mgmt_group_valid(uint32_t c)
{
    return c == WPA_CIPHER_GTK_NOT_USED || c == WPA_CIPHER_AES_128_CMAC ||
           c == WPA_CIPHER_BIP_GMAC_128 || c == WPA_CIPHER_BIP_GMAC_256;
}

/*
 * Shared body of the RSN and WPA parsers: group cipher, pairwise list, AKM list,
 * capabilities. pos/left point after the version field.
 */
static int parse_suites(const uint8_t *pos, size_t left, wpa_ie_data_t *d,
                        uint32_t (*cipher_bit)(uint32_t), uint32_t (*akm_bit)(uint32_t),
                        const uint8_t **out_pos, size_t *out_left)
{
    if (left >= RSN_SELECTOR_LEN)
    {
        d->group_cipher = cipher_bit(get_be32(pos));
        pos += RSN_SELECTOR_LEN;
        left -= RSN_SELECTOR_LEN;
    }
    else if (left > 0U)
    {
        return WPA_IE_ERR_GROUP;
    }

    if (left >= 2U)
    {
        size_t count = get_le16(pos);
        pos += 2;
        left -= 2U;
        if (count == 0U || left < count * RSN_SELECTOR_LEN)
        {
            return WPA_IE_ERR_PAIRWISE;
        }
        d->pairwise_cipher = 0U;
        for (size_t i = 0U; i < count; i++)
        {
            d->pairwise_cipher |= cipher_bit(get_be32(pos));
            pos += RSN_SELECTOR_LEN;
            left -= RSN_SELECTOR_LEN;
        }
    }
    else if (left == 1U)
    {
        return WPA_IE_ERR_SHORT_AKM;
    }

    if (left >= 2U)
    {
        size_t count = get_le16(pos);
        pos += 2;
        left -= 2U;
        if (count == 0U || left < count * RSN_SELECTOR_LEN)
        {
            return WPA_IE_ERR_AKM;
        }
        d->key_mgmt = 0U;
        for (size_t i = 0U; i < count; i++)
        {
            d->key_mgmt |= akm_bit(get_be32(pos));
            pos += RSN_SELECTOR_LEN;
            left -= RSN_SELECTOR_LEN;
        }
    }
    else if (left == 1U)
    {
        return WPA_IE_ERR_SHORT_CAPAB;
    }

    if (left >= 2U)
    {
        d->capabilities = get_le16(pos);
        pos += 2;
        left -= 2U;
    }

    *out_pos = pos;
    *out_left = left;
    return WPA_IE_OK;
}

static int parse_rsn(const uint8_t *ie, size_t ie_len, wpa_ie_data_t *d, bool strict_mgmt_group)
{
    d->proto = WPA_PROTO_RSN;
    d->pairwise_cipher = WPA_CIPHER_CCMP;
    d->group_cipher = WPA_CIPHER_CCMP;
    d->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
    d->mgmt_group_cipher = WPA_CIPHER_AES_128_CMAC;

    if (ie_len < WPA_IE_HDR_LEN + 2U || ie[1] != ie_len - WPA_IE_HDR_LEN ||
        get_le16(&ie[WPA_IE_HDR_LEN]) != RSN_VERSION)
    {
        return WPA_IE_ERR_MALFORMED;
    }

    const uint8_t *pos = ie + WPA_IE_HDR_LEN + 2U;
    size_t left = ie_len - WPA_IE_HDR_LEN - 2U;
    int st = parse_suites(pos, left, d, rsn_cipher_bit, rsn_akm_bit, &pos, &left);
    if (st != WPA_IE_OK)
    {
        return st;
    }

    if (left >= 2U)
    {
        size_t num = get_le16(pos);
        pos += 2;
        left -= 2U;
        if (num > left / RSN_PMKID_LEN)
        {
            return WPA_IE_ERR_PMKID;
        }
        d->num_pmkid = num;
        d->pmkid = (num > 0U) ? pos : NULL;
        pos += num * RSN_PMKID_LEN;
        left -= num * RSN_PMKID_LEN;
    }

    if (left >= RSN_SELECTOR_LEN)
    {
        d->mgmt_group_cipher = rsn_cipher_bit(get_be32(pos));
        if (strict_mgmt_group && !mgmt_group_valid(d->mgmt_group_cipher))
        {
            return WPA_IE_ERR_MGMT_GROUP;
        }
    }
    return WPA_IE_OK;
}

static int parse_wpa(const uint8_t *ie, size_t ie_len, wpa_ie_data_t *d)
{
    d->proto = WPA_PROTO_WPA;
    d->pairwise_cipher = WPA_CIPHER_TKIP;
    d->group_cipher = WPA_CIPHER_TKIP;
    d->key_mgmt = WPA_KEY_MGMT_IEEE8021X;

    /* header + OUI/type + version */
    const size_t fixed = WPA_IE_HDR_LEN + RSN_SELECTOR_LEN + 2U;
    if (ie_len < fixed || ie[0] != WLAN_EID_VENDOR_SPECIFIC || ie[1] != ie_len - WPA_IE_HDR_LEN ||
        get_be32(&ie[WPA_IE_HDR_LEN]) != WPA_OUI_TYPE ||
        get_le16(&ie[WPA_IE_HDR_LEN + RSN_SELECTOR_LEN]) != WPA_VERSION)
    {
        return WPA_IE_ERR_MALFORMED;
    }

    const uint8_t *pos = ie + fixed;
    size_t left = ie_len - fixed;
    return parse_suites(pos, left, d, wpa_cipher_bit, wpa_akm_bit, &pos, &left);
}

int wpa_ie_parse(const uint8_t *ie, size_t ie_len, wpa_ie_data_t *out, bool strict_mgmt_group)
{
    if (out == NULL)
    {
        return WPA_IE_ERR_EMPTY;
    }
    memset(out, 0, sizeof(*out));
    if (ie == NULL || ie_len < WPA_IE_HDR_LEN)
    {
        return WPA_IE_ERR_EMPTY;
    }

    if (ie[0] == WLAN_EID_RSN)
    {
        return parse_rsn(ie, ie_len, out, strict_mgmt_group);
    }
    if (ie[0] == WLAN_EID_RSNX)
    {
        if (ie[1] != ie_len - WPA_IE_HDR_LEN)
        {
            return WPA_IE_ERR_MALFORMED;
        }
        out->rsnxe_capa = (ie[1] > 0U) ? ie[RSNX_CAPA_OFFSET] : 0U;
        return WPA_IE_OK;
    }
    return parse_wpa(ie, ie_len, out);
}

size_t wpa_ie_build_rsn(uint8_t *buf, size_t buf_len, uint32_t pairwise_cipher,
                        uint32_t group_cipher, uint32_t key_mgmt, uint16_t capabilities)
{
    /* version, group, 1 pairwise, 1 AKM, capabilities */
    const size_t body = 2U + RSN_SELECTOR_LEN + 2U + RSN_SELECTOR_LEN + 2U + RSN_SELECTOR_LEN + 2U;
    uint32_t pw_sel = (pairwise_cipher == WPA_CIPHER_CCMP) ? RSN_CIPHER_SUITE_CCMP : 0U;
    uint32_t gr_sel = (group_cipher == WPA_CIPHER_CCMP) ? RSN_CIPHER_SUITE_CCMP :
                      (group_cipher == WPA_CIPHER_TKIP) ? RSN_CIPHER_SUITE_TKIP : 0U;
    uint32_t akm_sel = (key_mgmt == WPA_KEY_MGMT_PSK) ? RSN_AKM_PSK : 0U;

    if (buf == NULL || buf_len < WPA_IE_HDR_LEN + body || pw_sel == 0U || gr_sel == 0U || akm_sel == 0U)
    {
        return 0U;
    }

    uint8_t *p = buf;
    *p++ = WLAN_EID_RSN;
    *p++ = (uint8_t)body;
    put_le16(p, RSN_VERSION);      p += 2;
    put_be32(p, gr_sel);           p += RSN_SELECTOR_LEN;
    put_le16(p, 1U);               p += 2;
    put_be32(p, pw_sel);           p += RSN_SELECTOR_LEN;
    put_le16(p, 1U);               p += 2;
    put_be32(p, akm_sel);          p += RSN_SELECTOR_LEN;
    put_le16(p, capabilities);     p += 2;
    return (size_t)(p - buf);
}

int wpa_kde_parse(const uint8_t *buf, size_t len, wpa_kde_t *out)
{
    if (out == NULL || (buf == NULL && len > 0U))
    {
        return WPA_IE_ERR_EMPTY;
    }
    memset(out, 0, sizeof(*out));

    size_t off = 0U;
    while (off + 1U < len)
    {
        const uint8_t *e = buf + off;
        /* Padding: 0xDD followed by zeros (12.7.2) */
        if (e[0] == WPA_KDE_TYPE && e[1] == 0U)
        {
            break;
        }
        size_t elen = (size_t)e[1] + WPA_IE_HDR_LEN;
        if (off + elen > len)
        {
            return WPA_IE_ERR_MALFORMED;
        }
        if (e[0] == WLAN_EID_RSN && out->rsn_ie == NULL)
        {
            out->rsn_ie = e;
            out->rsn_ie_len = elen;
        }
        else if (e[0] == WPA_KDE_TYPE && elen >= WPA_KDE_HDR_LEN &&
                 get_be32(&e[WPA_IE_HDR_LEN]) == WPA_KDE_GTK)
        {
            if (elen < WPA_KDE_HDR_LEN + WPA_KDE_GTK_INFO_LEN + 1U ||
                elen - WPA_KDE_HDR_LEN - WPA_KDE_GTK_INFO_LEN > WPA_KDE_GTK_MAX_LEN)
            {
                return WPA_IE_ERR_MALFORMED;
            }
            const uint8_t *data = e + WPA_KDE_HDR_LEN;
            out->gtk_keyidx = data[0] & WPA_KDE_GTK_KEYID_MASK;
            out->gtk_tx = (data[0] & WPA_KDE_GTK_TX_BIT) != 0U;
            out->gtk = data + WPA_KDE_GTK_INFO_LEN;
            out->gtk_len = elen - WPA_KDE_HDR_LEN - WPA_KDE_GTK_INFO_LEN;
        }
        off += elen;
    }
    return WPA_IE_OK;
}
