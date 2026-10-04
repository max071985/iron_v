/*
 * src/wpa2_client.c
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

#include "wpa2_client.h"
#include "section.h"
#include "wifi.h"
#include "net.h"
#include "dhcp.h"
#include "mdns.h"
#include "systimer.h"
#include "wdt.h"
#include "string.h"

#if defined(__riscv)
#include "console.h"
#include "utils.h"
#endif

/* ========================================================================= */
/* Static Storage (Zero Heap Allocation)                                     */
/* ========================================================================= */
static wpa2_state_t     s_wpa2_state = WPA2_STATE_DISCONNECTED;
static wpa2_telemetry_t s_wpa2_telem;
static uint8_t          s_pmk[WPA2_PMK_LEN];
static wpa2_ptk_t       s_ptk;
static uint8_t          s_gtk[WPA2_GTK_LEN];
static uint8_t          s_anonce[WPA2_NONCE_LEN];
static uint8_t          s_snonce[WPA2_NONCE_LEN];
static uint8_t          s_last_replay[WPA2_REPLAY_LEN];
static uint8_t          s_ap_bssid[WPA2_MAC_ADDR_LEN];
static uint8_t          s_sta_mac[WPA2_MAC_ADDR_LEN];
static char             s_configured_ssid[WPA2_MAX_SSID_LEN + 1U];
static char             s_configured_pass[WPA2_MAX_PASS_LEN + 1U];
static uint64_t         s_handshake_start_us = 0ULL;
static bool             s_wpa2_initialized = false;

/* ========================================================================= */
/* Flash XIP Constant Tables (.flash.rodata)                                 */
/* ========================================================================= */
static const uint8_t s_aes_sbox[256] FLASH_RODATA_ATTR = {
    0x63U, 0x7CU, 0x77U, 0x7BU, 0xF2U, 0x6BU, 0x6FU, 0xC5U, 0x30U, 0x01U, 0x67U, 0x2BU, 0xFEU, 0xD7U, 0xABU, 0x76U,
    0xCAU, 0x82U, 0xC9U, 0x7DU, 0xFAU, 0x59U, 0x47U, 0xF0U, 0xADU, 0xD4U, 0xA2U, 0xAFU, 0x9CU, 0xA4U, 0x72U, 0xC0U,
    0xB7U, 0xFDU, 0x93U, 0x26U, 0x36U, 0x3FU, 0xF7U, 0xCCU, 0x34U, 0xA5U, 0xE5U, 0xF1U, 0x71U, 0xD8U, 0x31U, 0x15U,
    0x04U, 0xC7U, 0x23U, 0xC3U, 0x18U, 0x96U, 0x05U, 0x9AU, 0x07U, 0x12U, 0x80U, 0xE2U, 0xEBU, 0x27U, 0xB2U, 0x75U,
    0x09U, 0x83U, 0x2CU, 0x1AU, 0x1BU, 0x6EU, 0x5AU, 0xA0U, 0x52U, 0x3BU, 0xD6U, 0xB3U, 0x29U, 0xE3U, 0x2FU, 0x84U,
    0x53U, 0xD1U, 0x00U, 0xEDU, 0x20U, 0xFCU, 0xB1U, 0x5BU, 0x6AU, 0xCBU, 0xBEU, 0x39U, 0x4AU, 0x4CU, 0x58U, 0xCFU,
    0xD0U, 0xEFU, 0xAAU, 0xFBU, 0x43U, 0x4DU, 0x33U, 0x85U, 0x45U, 0xF9U, 0x02U, 0x7FU, 0x50U, 0x3CU, 0x9FU, 0xA8U,
    0x51U, 0xA3U, 0x40U, 0x8FU, 0x92U, 0x9DU, 0x38U, 0xF5U, 0xBCU, 0xB6U, 0xDAU, 0x21U, 0x10U, 0xFFU, 0xF3U, 0xD2U,
    0xCDU, 0x0CU, 0x13U, 0xECU, 0x5FU, 0x97U, 0x44U, 0x17U, 0xC4U, 0xA7U, 0x7EU, 0x3DU, 0x64U, 0x5DU, 0x19U, 0x73U,
    0x60U, 0x81U, 0x4FU, 0xDCU, 0x22U, 0x2AU, 0x90U, 0x88U, 0x46U, 0xEEU, 0xB8U, 0x14U, 0xDEU, 0x5EU, 0x0BU, 0xDBU,
    0xE0U, 0x32U, 0x3AU, 0x0AU, 0x49U, 0x06U, 0x24U, 0x5CU, 0xC2U, 0xD3U, 0xACU, 0x62U, 0x91U, 0x95U, 0xE4U, 0x79U,
    0xE7U, 0xC8U, 0x37U, 0x6DU, 0x8DU, 0xD5U, 0x4EU, 0xA9U, 0x6CU, 0x56U, 0xF4U, 0xEAU, 0x65U, 0x7AU, 0xAEU, 0x08U,
    0xBAU, 0x78U, 0x25U, 0x2EU, 0x1CU, 0xA6U, 0xB4U, 0xC6U, 0xE8U, 0xDDU, 0x74U, 0x1FU, 0x4BU, 0xBDU, 0x8BU, 0x8AU,
    0x70U, 0x3EU, 0xB5U, 0x66U, 0x48U, 0x03U, 0xF6U, 0x0EU, 0x61U, 0x35U, 0x57U, 0xB9U, 0x86U, 0xC1U, 0x1DU, 0x9EU,
    0xE1U, 0xF8U, 0x98U, 0x11U, 0x69U, 0xD9U, 0x8EU, 0x94U, 0x9BU, 0x1EU, 0x87U, 0xE9U, 0xCEU, 0x55U, 0x28U, 0xDFU,
    0x8CU, 0xA1U, 0x89U, 0x0DU, 0xBFU, 0xE6U, 0x42U, 0x68U, 0x41U, 0x99U, 0x2DU, 0x0FU, 0xB0U, 0x54U, 0xBBU, 0x16U
};

static const uint8_t s_aes_inv_sbox[256] FLASH_RODATA_ATTR = {
    0x52U, 0x09U, 0x6AU, 0xD5U, 0x30U, 0x36U, 0xA5U, 0x38U, 0xBFU, 0x40U, 0xA3U, 0x9EU, 0x81U, 0xF3U, 0xD7U, 0xFBU,
    0x7CU, 0xE3U, 0x39U, 0x82U, 0x9BU, 0x2FU, 0xFFU, 0x87U, 0x34U, 0x8EU, 0x43U, 0x44U, 0xC4U, 0xDEU, 0xE9U, 0xCBU,
    0x54U, 0x7BU, 0x94U, 0x32U, 0xA6U, 0xC2U, 0x23U, 0x3DU, 0xEEU, 0x4CU, 0x95U, 0x0BU, 0x42U, 0xFAU, 0xC3U, 0x4EU,
    0x08U, 0x2EU, 0xA1U, 0x66U, 0x28U, 0xD9U, 0x24U, 0xB2U, 0x76U, 0x5BU, 0xA2U, 0x49U, 0x6DU, 0x8BU, 0xD1U, 0x25U,
    0x72U, 0xF8U, 0xF6U, 0x64U, 0x86U, 0x68U, 0x98U, 0x16U, 0xD4U, 0xA4U, 0x5CU, 0xCCU, 0x5DU, 0x65U, 0xB6U, 0x92U,
    0x6CU, 0x70U, 0x48U, 0x50U, 0xFDU, 0xEDU, 0xB9U, 0xDAU, 0x5EU, 0x15U, 0x46U, 0x57U, 0xA7U, 0x8DU, 0x9DU, 0x84U,
    0x90U, 0xD8U, 0xABU, 0x00U, 0x8CU, 0xBCU, 0xD3U, 0x0AU, 0xF7U, 0xE4U, 0x58U, 0x05U, 0xB8U, 0xB3U, 0x45U, 0x06U,
    0xD0U, 0x2CU, 0x1EU, 0x8FU, 0xCAU, 0x3FU, 0x0FU, 0x02U, 0xC1U, 0xAFU, 0xBDU, 0x03U, 0x01U, 0x13U, 0x8AU, 0x6BU,
    0x3AU, 0x91U, 0x11U, 0x41U, 0x4FU, 0x67U, 0xDCU, 0xEAU, 0x97U, 0xF2U, 0xCFU, 0xCEU, 0xF0U, 0xB4U, 0xE6U, 0x73U,
    0x96U, 0xACU, 0x74U, 0x22U, 0xE7U, 0xADU, 0x35U, 0x85U, 0xE2U, 0xF9U, 0x37U, 0xE8U, 0x1CU, 0x75U, 0xDFU, 0x6EU,
    0x47U, 0xF1U, 0x1AU, 0x71U, 0x1DU, 0x29U, 0xC5U, 0x89U, 0x6FU, 0xB7U, 0x62U, 0x0EU, 0xAAU, 0x18U, 0xBEU, 0x1BU,
    0xFCU, 0x56U, 0x3EU, 0x4BU, 0xC6U, 0xD2U, 0x79U, 0x20U, 0x9AU, 0xDBU, 0xC0U, 0xFEU, 0x78U, 0xCDU, 0x5AU, 0xF4U,
    0x1FU, 0xDDU, 0xA8U, 0x33U, 0x88U, 0x07U, 0xC7U, 0x31U, 0xB1U, 0x12U, 0x10U, 0x59U, 0x27U, 0x80U, 0xECU, 0x5FU,
    0x60U, 0x51U, 0x7FU, 0xA9U, 0x19U, 0xB5U, 0x4AU, 0x0DU, 0x2DU, 0xE5U, 0x7AU, 0x9FU, 0x93U, 0xC9U, 0x9CU, 0xEFU,
    0xA0U, 0xE0U, 0x3BU, 0x4DU, 0xAEU, 0x2AU, 0xF5U, 0xB0U, 0xC8U, 0xEBU, 0xBBU, 0x3CU, 0x83U, 0x53U, 0x99U, 0x61U,
    0x17U, 0x2BU, 0x04U, 0x7EU, 0xBAU, 0x77U, 0xD6U, 0x26U, 0xE1U, 0x69U, 0x14U, 0x63U, 0x55U, 0x21U, 0x0CU, 0x7DU
};

static const uint8_t s_aes_rcon[11] FLASH_RODATA_ATTR = {
    0x00U, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x80U, 0x1BU, 0x36U
};

/* Standard 802.11i RSN IE (WPA2 CCMP, PSK) */
static const uint8_t s_wpa2_rsn_ie[] FLASH_RODATA_ATTR = {
    0x30U, 0x14U,                   /* Element ID = 48 (RSN), Length = 20 */
    0x01U, 0x00U,                   /* Version = 1 */
    0x00U, 0x0FU, 0xACU, 0x04U,     /* Group Cipher Suite: 00-0F-AC-04 (CCMP) */
    0x01U, 0x00U,                   /* Pairwise Cipher Suite Count = 1 */
    0x00U, 0x0FU, 0xACU, 0x04U,     /* Pairwise Cipher Suite: 00-0F-AC-04 (CCMP) */
    0x01U, 0x00U,                   /* AKM Suite Count = 1 */
    0x00U, 0x0FU, 0xACU, 0x02U,     /* AKM Suite: 00-0F-AC-02 (PSK) */
    0x00U, 0x00U                    /* RSN Capabilities = 0 */
};

/* ========================================================================= */
/* Cryptographic Subsystem: Freestanding SHA-1 & HMAC-SHA1 (.flash.text)     */
/* ========================================================================= */
typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t  buffer[64];
} wpa2_sha1_ctx_t;

#define SHA1_ROTL(x, n) (((x) << (n)) | ((x) >> (32U - (n))))

static void wpa2_sha1_transform(uint32_t state[5], const uint8_t buffer[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t w[80];

    for (size_t i = 0; i < 16; i++)
    {
        w[i] = ((uint32_t)buffer[i * 4U] << 24U) |
               ((uint32_t)buffer[i * 4U + 1U] << 16U) |
               ((uint32_t)buffer[i * 4U + 2U] << 8U) |
               ((uint32_t)buffer[i * 4U + 3U]);
    }
    for (size_t i = 16; i < 80; i++)
    {
        w[i] = SHA1_ROTL(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1U);
    }

    /* Round 1 (0..19) */
    for (size_t i = 0; i < 20; i++)
    {
        uint32_t f = (b & c) | ((~b) & d);
        uint32_t temp = SHA1_ROTL(a, 5U) + f + e + 0x5A827999U + w[i];
        e = d; d = c; c = SHA1_ROTL(b, 30U); b = a; a = temp;
    }
    /* Round 2 (20..39) */
    for (size_t i = 20; i < 40; i++)
    {
        uint32_t f = b ^ c ^ d;
        uint32_t temp = SHA1_ROTL(a, 5U) + f + e + 0x6ED9EBA1U + w[i];
        e = d; d = c; c = SHA1_ROTL(b, 30U); b = a; a = temp;
    }
    /* Round 3 (40..59) */
    for (size_t i = 40; i < 60; i++)
    {
        uint32_t f = (b & c) | (b & d) | (c & d);
        uint32_t temp = SHA1_ROTL(a, 5U) + f + e + 0x8F1BBCDCU + w[i];
        e = d; d = c; c = SHA1_ROTL(b, 30U); b = a; a = temp;
    }
    /* Round 4 (60..79) */
    for (size_t i = 60; i < 80; i++)
    {
        uint32_t f = b ^ c ^ d;
        uint32_t temp = SHA1_ROTL(a, 5U) + f + e + 0xCA62C1D6U + w[i];
        e = d; d = c; c = SHA1_ROTL(b, 30U); b = a; a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void wpa2_sha1_init(wpa2_sha1_ctx_t *ctx)
{
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xEFCDAB89U;
    ctx->state[2] = 0x98BADCFEU;
    ctx->state[3] = 0x10325476U;
    ctx->state[4] = 0xC3D2E1F0U;
    ctx->count[0] = 0U;
    ctx->count[1] = 0U;
}

static void wpa2_sha1_update(wpa2_sha1_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i = 0U;
    size_t j = (ctx->count[0] >> 3U) & 63U;
    if ((ctx->count[0] += (uint32_t)(len << 3U)) < (uint32_t)(len << 3U))
    {
        ctx->count[1]++;
    }
    ctx->count[1] += (uint32_t)(len >> 29U);

    if ((j + len) > 63U)
    {
        memcpy(&ctx->buffer[j], data, (i = 64U - j));
        wpa2_sha1_transform(ctx->state, ctx->buffer);
        for (; i + 63U < len; i += 64U)
        {
            wpa2_sha1_transform(ctx->state, &data[i]);
        }
        j = 0U;
    }
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void wpa2_sha1_final(wpa2_sha1_ctx_t *ctx, uint8_t digest[20])
{
    uint8_t finalcount[8];
    for (size_t i = 0; i < 8; i++)
    {
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255U);
    }
    wpa2_sha1_update(ctx, (const uint8_t *)"\200", 1U);
    while ((ctx->count[0] & 504U) != 448U)
    {
        wpa2_sha1_update(ctx, (const uint8_t *)"\0", 1U);
    }
    wpa2_sha1_update(ctx, finalcount, 8U);
    for (size_t i = 0; i < 20; i++)
    {
        digest[i] = (uint8_t)((ctx->state[i >> 2U] >> ((3 - (i & 3)) * 8)) & 255U);
    }
}

static void wpa2_hmac_sha1(const uint8_t *key, size_t key_len,
                           const uint8_t *data, size_t data_len,
                           uint8_t digest[20])
{
    uint8_t k_pad[64];
    uint8_t tk[20];
    wpa2_sha1_ctx_t ctx;

    if (key_len > 64U)
    {
        wpa2_sha1_init(&ctx);
        wpa2_sha1_update(&ctx, key, key_len);
        wpa2_sha1_final(&ctx, tk);
        key = tk;
        key_len = 20U;
    }

    /* Inner pad */
    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < key_len; i++)
    {
        k_pad[i] ^= key[i];
    }
    wpa2_sha1_init(&ctx);
    wpa2_sha1_update(&ctx, k_pad, 64U);
    wpa2_sha1_update(&ctx, data, data_len);
    wpa2_sha1_final(&ctx, digest);

    /* Outer pad */
    memset(k_pad, 0x5C, 64);
    for (size_t i = 0; i < key_len; i++)
    {
        k_pad[i] ^= key[i];
    }
    wpa2_sha1_init(&ctx);
    wpa2_sha1_update(&ctx, k_pad, 64U);
    wpa2_sha1_update(&ctx, digest, 20U);
    wpa2_sha1_final(&ctx, digest);
}

/* ========================================================================= */
/* Cryptographic Subsystem: Freestanding AES-128 ECB (.flash.text)           */
/* ========================================================================= */
static uint8_t aes_xtime(uint8_t b)
{
    return (uint8_t)((b << 1U) ^ ((b & 0x80U) ? 0x1BU : 0x00U));
}

static void aes_key_expand(const uint8_t key[16], uint8_t w[176])
{
    memcpy(w, key, 16);
    for (size_t i = 4; i < 44; i++)
    {
        uint8_t temp[4];
        memcpy(temp, &w[(i - 1) * 4], 4);
        if ((i % 4) == 0)
        {
            uint8_t k0 = temp[0];
            temp[0] = s_aes_sbox[temp[1]] ^ s_aes_rcon[i / 4];
            temp[1] = s_aes_sbox[temp[2]];
            temp[2] = s_aes_sbox[temp[3]];
            temp[3] = s_aes_sbox[k0];
        }
        for (size_t j = 0; j < 4; j++)
        {
            w[i * 4 + j] = w[(i - 4) * 4 + j] ^ temp[j];
        }
    }
}

static void __attribute__((unused)) aes_128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    uint8_t w[176];
    aes_key_expand(key, w);

    uint8_t s[16];
    for (size_t i = 0; i < 16; i++) s[i] = in[i] ^ w[i];

    for (size_t round = 1; round <= 10; round++)
    {
        /* SubBytes */
        for (size_t i = 0; i < 16; i++) s[i] = s_aes_sbox[s[i]];

        /* ShiftRows */
        uint8_t t;
        t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;

        /* MixColumns (rounds 1..9) */
        if (round < 10)
        {
            for (size_t c = 0; c < 4; c++)
            {
                size_t idx = c * 4;
                uint8_t a0 = s[idx], a1 = s[idx + 1], a2 = s[idx + 2], a3 = s[idx + 3];
                uint8_t h = a0 ^ a1 ^ a2 ^ a3;
                s[idx]     ^= h ^ aes_xtime((uint8_t)(a0 ^ a1));
                s[idx + 1] ^= h ^ aes_xtime((uint8_t)(a1 ^ a2));
                s[idx + 2] ^= h ^ aes_xtime((uint8_t)(a2 ^ a3));
                s[idx + 3] ^= h ^ aes_xtime((uint8_t)(a3 ^ a0));
            }
        }

        /* AddRoundKey */
        for (size_t i = 0; i < 16; i++) s[i] ^= w[round * 16 + i];
    }
    memcpy(out, s, 16);
}

static inline uint8_t aes_mul9(uint8_t x)
{
    return aes_xtime(aes_xtime(aes_xtime(x))) ^ x;
}

static inline uint8_t aes_mulb(uint8_t x)
{
    uint8_t x2 = aes_xtime(x);
    return aes_xtime(aes_xtime(x2)) ^ x2 ^ x;
}

static inline uint8_t aes_muld(uint8_t x)
{
    uint8_t x2 = aes_xtime(x);
    uint8_t x4 = aes_xtime(x2);
    return aes_xtime(x4) ^ x4 ^ x;
}

static inline uint8_t aes_mule(uint8_t x)
{
    uint8_t x2 = aes_xtime(x);
    uint8_t x4 = aes_xtime(x2);
    return aes_xtime(x4) ^ x4 ^ x2;
}

static void aes_128_decrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16])
{
    uint8_t w[176];
    aes_key_expand(key, w);

    uint8_t s[16];
    for (size_t i = 0; i < 16; i++) s[i] = in[i] ^ w[160 + i];

    for (int round = 9; round >= 0; round--)
    {
        /* InvShiftRows */
        uint8_t t;
        t = s[13]; s[13] = s[9]; s[9] = s[5]; s[5] = s[1]; s[1] = t;
        t = s[2]; s[2] = s[10]; s[10] = t; t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[3]; s[3] = s[7]; s[7] = s[11]; s[11] = s[15]; s[15] = t;

        /* InvSubBytes */
        for (size_t i = 0; i < 16; i++) s[i] = s_aes_inv_sbox[s[i]];

        /* AddRoundKey */
        for (size_t i = 0; i < 16; i++) s[i] ^= w[round * 16 + i];

        /* InvMixColumns (rounds 9..1) */
        if (round > 0)
        {
            for (size_t c = 0; c < 4; c++)
            {
                size_t idx = c * 4;
                uint8_t a = s[idx], b = s[idx + 1], c_byte = s[idx + 2], d = s[idx + 3];
                s[idx]     = aes_mule(a) ^ aes_mulb(b) ^ aes_muld(c_byte) ^ aes_mul9(d);
                s[idx + 1] = aes_mul9(a) ^ aes_mule(b) ^ aes_mulb(c_byte) ^ aes_muld(d);
                s[idx + 2] = aes_muld(a) ^ aes_mul9(b) ^ aes_mule(c_byte) ^ aes_mulb(d);
                s[idx + 3] = aes_mulb(a) ^ aes_muld(b) ^ aes_mul9(c_byte) ^ aes_mule(d);
            }
        }
    }
    memcpy(out, s, 16);
}

/* ========================================================================= */
/* Public Cryptographic Engine APIs (.flash.text)                            */
/* ========================================================================= */
wpa2_status_t wpa2_crypto_pbkdf2_sha1(const char *passphrase, const char *ssid,
                                     uint32_t iterations, uint8_t *out_pmk)
{
    if (passphrase == NULL || ssid == NULL || out_pmk == NULL || iterations == 0U)
    {
        return WPA2_ERR_INVALID_ARG;
    }

    size_t pass_len = strlen(passphrase);
    size_t ssid_len = strlen(ssid);
    if (pass_len < WPA2_MIN_PASS_LEN || pass_len > WPA2_MAX_PASS_LEN || ssid_len == 0U)
    {
        return WPA2_ERR_INVALID_ARG;
    }

    /* Buffer for Salt || INT_32_BE(i) */
    uint8_t salt_block[WPA2_MAX_SSID_LEN + 4U];
    memcpy(salt_block, ssid, ssid_len);

    uint8_t u_prev[20];
    uint8_t u_cur[20];
    uint8_t t_block[20];

    /* Block 1 (bytes 0..19) and Block 2 (bytes 20..31) */
    for (uint32_t block_idx = 1U; block_idx <= 2U; block_idx++)
    {
        salt_block[ssid_len]     = 0x00U;
        salt_block[ssid_len + 1] = 0x00U;
        salt_block[ssid_len + 2] = 0x00U;
        salt_block[ssid_len + 3] = (uint8_t)block_idx;

        /* U_1 = HMAC(passphrase, salt || INT_32_BE(block_idx)) */
        wpa2_hmac_sha1((const uint8_t *)passphrase, pass_len, salt_block, ssid_len + 4U, u_prev);
        memcpy(t_block, u_prev, 20);

        for (uint32_t iter = 2U; iter <= iterations; iter++)
        {
            wpa2_hmac_sha1((const uint8_t *)passphrase, pass_len, u_prev, 20U, u_cur);
            for (size_t b = 0; b < 20; b++)
            {
                t_block[b] ^= u_cur[b];
                u_prev[b] = u_cur[b];
            }

            /* Periodically feed watchdog during heavy computation */
            if ((iter & 0x01FFU) == 0U)
            {
                wdt_feed();
                lp_wdt_feed();
            }
        }

        if (block_idx == 1U)
        {
            memcpy(out_pmk, t_block, 20);
        }
        else
        {
            memcpy(out_pmk + 20, t_block, 12);
        }
    }

    return WPA2_OK;
}

wpa2_status_t wpa2_crypto_prf512(const uint8_t *pmk, const uint8_t *mac1, const uint8_t *mac2,
                                 const uint8_t *nonce1, const uint8_t *nonce2,
                                 wpa2_ptk_t *out_ptk)
{
    if (pmk == NULL || mac1 == NULL || mac2 == NULL || nonce1 == NULL || nonce2 == NULL || out_ptk == NULL)
    {
        return WPA2_ERR_INVALID_ARG;
    }

    /* Format B = Min(mac) || Max(mac) || Min(nonce) || Max(nonce) */
    uint8_t data_b[76];
    if (memcmp(mac1, mac2, WPA2_MAC_ADDR_LEN) < 0)
    {
        memcpy(data_b, mac1, WPA2_MAC_ADDR_LEN);
        memcpy(data_b + 6, mac2, WPA2_MAC_ADDR_LEN);
    }
    else
    {
        memcpy(data_b, mac2, WPA2_MAC_ADDR_LEN);
        memcpy(data_b + 6, mac1, WPA2_MAC_ADDR_LEN);
    }

    if (memcmp(nonce1, nonce2, WPA2_NONCE_LEN) < 0)
    {
        memcpy(data_b + 12, nonce1, WPA2_NONCE_LEN);
        memcpy(data_b + 44, nonce2, WPA2_NONCE_LEN);
    }
    else
    {
        memcpy(data_b + 12, nonce2, WPA2_NONCE_LEN);
        memcpy(data_b + 44, nonce1, WPA2_NONCE_LEN);
    }

    const char prf_label[] = "Pairwise key expansion";
    size_t label_len = strlen(prf_label);

    /* Construct message: Label || 0x00 || B || counter */
    uint8_t prf_msg[100];
    memcpy(prf_msg, prf_label, label_len);
    prf_msg[label_len] = 0x00U;
    memcpy(prf_msg + label_len + 1U, data_b, 76U);

    uint8_t ptk_raw[80];
    for (uint8_t i = 0; i < 4; i++)
    {
        prf_msg[label_len + 1U + 76U] = i;
        wpa2_hmac_sha1(pmk, WPA2_PMK_LEN, prf_msg, label_len + 1U + 76U + 1U, &ptk_raw[i * 20U]);
    }

    /* Assign PTK fields */
    memcpy(out_ptk->kck, ptk_raw, WPA2_KCK_LEN);
    memcpy(out_ptk->kek, ptk_raw + 16, WPA2_KEK_LEN);
    memcpy(out_ptk->tk,  ptk_raw + 32, WPA2_TK_LEN);
    memcpy(out_ptk->tx_mic, ptk_raw + 48, 8);
    memcpy(out_ptk->rx_mic, ptk_raw + 56, 8);

    return WPA2_OK;
}

wpa2_status_t wpa2_crypto_compute_mic(const uint8_t *kck, const uint8_t *eapol_frame,
                                      uint16_t frame_len, uint8_t *out_mic)
{
    if (kck == NULL || eapol_frame == NULL || out_mic == NULL || frame_len < (sizeof(eapol_ethernet_hdr_t) + sizeof(eapol_key_header_t)))
    {
        return WPA2_ERR_INVALID_ARG;
    }

    /* Buffer must be copied to zero out MIC field */
    uint8_t temp_buf[NET_MAX_FRAME_SIZE];
    if (frame_len > sizeof(temp_buf))
    {
        return WPA2_ERR_BUFFER_SMALL;
    }
    memcpy(temp_buf, eapol_frame, frame_len);

    /* Zero the MIC field in the EAPOL key header (offset 14 bytes into Ethernet header) */
    eapol_key_header_t *key_hdr = (eapol_key_header_t *)(temp_buf + sizeof(eapol_ethernet_hdr_t));
    memset(key_hdr->key_mic, 0, WPA2_MIC_LEN);

    /* EAPOL MIC is computed over 802.1X header + key descriptor (from offset 14) */
    uint8_t digest[20];
    const uint8_t *eapol_payload = temp_buf + ETH_HDR_LEN;
    uint16_t eapol_len = (uint16_t)(frame_len - ETH_HDR_LEN);

    wpa2_hmac_sha1(kck, WPA2_KCK_LEN, eapol_payload, eapol_len, digest);
    memcpy(out_mic, digest, WPA2_MIC_LEN);

    return WPA2_OK;
}

wpa2_status_t wpa2_crypto_aes_unwrap(const uint8_t *kek, const uint8_t *wrapped,
                                     uint16_t wrapped_len, uint8_t *out_plain,
                                     uint16_t *out_plain_len)
{
    if (kek == NULL || wrapped == NULL || out_plain == NULL || out_plain_len == NULL)
    {
        return WPA2_ERR_INVALID_ARG;
    }
    if ((wrapped_len % 8U) != 0U || wrapped_len < 24U)
    {
        return WPA2_ERR_INVALID_ARG;
    }

    size_t n = (wrapped_len / 8U) - 1U;
    uint8_t a[8];
    memcpy(a, wrapped, 8);

    uint8_t r[8 * 16]; /* Up to 16 blocks (128 bytes) */
    if (n > 16) return WPA2_ERR_BUFFER_SMALL;

    for (size_t i = 0; i < n; i++)
    {
        memcpy(&r[i * 8], &wrapped[(i + 1) * 8], 8);
    }

    for (int j = 5; j >= 0; j--)
    {
        for (int i = (int)n; i >= 1; i--)
        {
            uint32_t t = (uint32_t)(n * (size_t)j + (size_t)i);
            uint8_t b_in[16];
            uint8_t b_out[16];

            /* A ^ t */
            memcpy(b_in, a, 8);
            b_in[7] ^= (uint8_t)(t & 0xFFU);
            b_in[6] ^= (uint8_t)((t >> 8U) & 0xFFU);
            b_in[5] ^= (uint8_t)((t >> 16U) & 0xFFU);
            b_in[4] ^= (uint8_t)((t >> 24U) & 0xFFU);

            /* R[i] */
            memcpy(&b_in[8], &r[(i - 1) * 8], 8);

            aes_128_decrypt_block(kek, b_in, b_out);

            memcpy(a, b_out, 8);
            memcpy(&r[(i - 1) * 8], &b_out[8], 8);
        }
    }

    /* Verify IV (RFC 3394 standard A6A6A6A6A6A6A6A6) */
    for (size_t k = 0; k < 8; k++)
    {
        if (a[k] != 0xA6U)
        {
            return WPA2_ERR_DECRYPT_FAIL;
        }
    }

    memcpy(out_plain, r, n * 8);
    *out_plain_len = (uint16_t)(n * 8);
    return WPA2_OK;
}

wpa2_status_t wpa2_crypto_aes_wrap(const uint8_t *kek, const uint8_t *plain,
                                   uint16_t plain_len, uint8_t *out_wrapped,
                                   uint16_t *out_wrapped_len)
{
    if (kek == NULL || plain == NULL || out_wrapped == NULL || out_wrapped_len == NULL)
    {
        return WPA2_ERR_INVALID_ARG;
    }
    if ((plain_len % 8U) != 0U || plain_len < 16U)
    {
        return WPA2_ERR_INVALID_ARG;
    }

    size_t n = plain_len / 8U;
    if (n > 16) return WPA2_ERR_BUFFER_SMALL;

    uint8_t a[8];
    memset(a, 0xA6U, 8);

    uint8_t r[8 * 16];
    memcpy(r, plain, plain_len);

    for (size_t j = 0; j <= 5; j++)
    {
        for (size_t i = 1; i <= n; i++)
        {
            uint8_t b_in[16];
            uint8_t b_out[16];

            memcpy(b_in, a, 8);
            memcpy(&b_in[8], &r[(i - 1) * 8], 8);

            aes_128_encrypt_block(kek, b_in, b_out);

            uint32_t t = (uint32_t)(n * j + i);
            memcpy(a, b_out, 8);
            a[7] ^= (uint8_t)(t & 0xFFU);
            a[6] ^= (uint8_t)((t >> 8U) & 0xFFU);
            a[5] ^= (uint8_t)((t >> 16U) & 0xFFU);
            a[4] ^= (uint8_t)((t >> 24U) & 0xFFU);

            memcpy(&r[(i - 1) * 8], &b_out[8], 8);
        }
    }

    memcpy(out_wrapped, a, 8);
    memcpy(out_wrapped + 8, r, n * 8);
    *out_wrapped_len = (uint16_t)((n + 1) * 8);
    return WPA2_OK;
}

wpa2_status_t wpa2_client_get_ptk(wpa2_ptk_t *out_ptk)
{
    if (out_ptk == NULL || !s_wpa2_telem.has_ptk)
    {
        return WPA2_ERR_INVALID_ARG;
    }
    memcpy(out_ptk, &s_ptk, sizeof(wpa2_ptk_t));
    return WPA2_OK;
}

/* ========================================================================= */
/* 802.11i 4-Way Handshake Processing Logic (.flash.text)                    */
/* ========================================================================= */
static void wpa2_generate_snonce(uint8_t snonce[WPA2_NONCE_LEN])
{
    uint64_t t = systimer_get_us();
    for (size_t i = 0; i < WPA2_NONCE_LEN; i++)
    {
        t = t * 6364136223846793005ULL + 1442695040888963407ULL;
        snonce[i] = (uint8_t)(t >> (i % 32));
    }
}

wpa2_status_t wpa2_client_rx_eapol(const uint8_t *src_mac, const uint8_t *frame, uint16_t len)
{
    if (src_mac == NULL || frame == NULL || len < (sizeof(eapol_ethernet_hdr_t) + sizeof(eapol_key_header_t)))
    {
        return WPA2_ERR_INVALID_ARG;
    }

    const eapol_ethernet_hdr_t *eth_hdr = (const eapol_ethernet_hdr_t *)frame;
    if (NET_NTOHS(eth_hdr->ethertype) != ETHERTYPE_EAPOL || eth_hdr->type != EAPOL_TYPE_KEY)
    {
        return WPA2_ERR_INVALID_ARG;
    }

    const eapol_key_header_t *key_hdr = (const eapol_key_header_t *)(frame + sizeof(eapol_ethernet_hdr_t));
    uint16_t key_info = NET_NTOHS(key_hdr->key_info);

    /* Verify Pairwise Key */
    if ((key_info & WPA2_KEY_INFO_PAIRWISE) == 0U)
    {
        return WPA2_ERR_INVALID_ARG;
    }

    /* Record AP BSSID */
    memcpy(s_ap_bssid, src_mac, WPA2_MAC_ADDR_LEN);
    memcpy(s_wpa2_telem.target_bssid, src_mac, WPA2_MAC_ADDR_LEN);

    /* --------------------------------------------------------------------- */
    /* Message 1 (AP -> STA): Pairwise=1, Ack=1, MIC=0                       */
    /* --------------------------------------------------------------------- */
    if ((key_info & WPA2_KEY_INFO_ACK) != 0U && (key_info & WPA2_KEY_INFO_MIC) == 0U)
    {
        s_wpa2_telem.m1_rx_count++;
        s_wpa2_telem.handshakes_attempted++;
        s_handshake_start_us = systimer_get_us();

        /* Store ANonce and Replay Counter */
        memcpy(s_anonce, key_hdr->key_nonce, WPA2_NONCE_LEN);
        memcpy(s_last_replay, key_hdr->replay_counter, WPA2_REPLAY_LEN);

        /* Generate fresh SNonce */
        wpa2_generate_snonce(s_snonce);

        /* Derive PTK */
        wpa2_crypto_prf512(s_pmk, s_ap_bssid, s_sta_mac, s_anonce, s_snonce, &s_ptk);
        s_wpa2_telem.has_ptk = true;
        s_wpa2_state = WPA2_STATE_4WAY_M1_RECEIVED;

        /* Synthesize and Transmit Message 2 (STA -> AP) */
        uint8_t tx_frame[NET_MAX_FRAME_SIZE];
        eapol_ethernet_hdr_t *tx_eth = (eapol_ethernet_hdr_t *)tx_frame;
        eapol_key_header_t *tx_key = (eapol_key_header_t *)(tx_frame + sizeof(eapol_ethernet_hdr_t));
        uint8_t *tx_data = tx_frame + sizeof(eapol_ethernet_hdr_t) + sizeof(eapol_key_header_t);

        /* Ethernet Header */
        memcpy(tx_eth->dest_mac, s_ap_bssid, WPA2_MAC_ADDR_LEN);
        memcpy(tx_eth->src_mac, s_sta_mac, WPA2_MAC_ADDR_LEN);
        tx_eth->ethertype = NET_HTONS(ETHERTYPE_EAPOL);
        tx_eth->version = EAPOL_VERSION_1;
        tx_eth->type = EAPOL_TYPE_KEY;

        /* Key Descriptor */
        tx_key->descriptor_type = EAPOL_DESC_TYPE_RSN;
        tx_key->key_info = NET_HTONS(WPA2_MSG2_KEY_INFO_NOMINAL);
        tx_key->key_length = NET_HTONS(0U);
        memcpy(tx_key->replay_counter, s_last_replay, WPA2_REPLAY_LEN);
        memcpy(tx_key->key_nonce, s_snonce, WPA2_NONCE_LEN);
        memset(tx_key->key_iv, 0, 16);
        memset(tx_key->key_rsc, 0, 8);
        memset(tx_key->key_id, 0, 8);
        memset(tx_key->key_mic, 0, WPA2_MIC_LEN);

        /* Append RSN IE */
        uint16_t rsn_len = (uint16_t)sizeof(s_wpa2_rsn_ie);
        memcpy(tx_data, s_wpa2_rsn_ie, rsn_len);
        tx_key->key_data_length = NET_HTONS(rsn_len);

        uint16_t total_len = (uint16_t)(sizeof(eapol_ethernet_hdr_t) + sizeof(eapol_key_header_t) + rsn_len);
        tx_eth->length = NET_HTONS((uint16_t)(sizeof(eapol_key_header_t) + rsn_len));

        /* Calculate MIC over entire EAPOL packet with MIC zeroed */
        wpa2_crypto_compute_mic(s_ptk.kck, tx_frame, total_len, tx_key->key_mic);

        /* Transmit via Wi-Fi stack */
        wifi_tx_packet(WIFI_TX_IF_STA, tx_frame, total_len);
        s_wpa2_telem.m2_tx_count++;
        s_wpa2_state = WPA2_STATE_4WAY_M2_SENT;
        return WPA2_OK;
    }

    /* --------------------------------------------------------------------- */
    /* Message 3 (AP -> STA): Pairwise=1, Install=1, Ack=1, MIC=1            */
    /* --------------------------------------------------------------------- */
    if ((key_info & WPA2_KEY_INFO_MIC) != 0U && (key_info & WPA2_KEY_INFO_ACK) != 0U)
    {
        s_wpa2_telem.m3_rx_count++;

        /* 1. Verify Replay Counter >= last replay counter */
        if (memcmp(key_hdr->replay_counter, s_last_replay, WPA2_REPLAY_LEN) < 0)
        {
            s_wpa2_telem.replay_errors++;
            return WPA2_ERR_REPLAY;
        }
        memcpy(s_last_replay, key_hdr->replay_counter, WPA2_REPLAY_LEN);

        /* 2. Verify AP's Message Integrity Code (MIC) */
        uint8_t expected_mic[WPA2_MIC_LEN];
        wpa2_crypto_compute_mic(s_ptk.kck, frame, len, expected_mic);
        if (memcmp(expected_mic, key_hdr->key_mic, WPA2_MIC_LEN) != 0)
        {
            s_wpa2_telem.mic_failures++;
            return WPA2_ERR_MIC_FAIL;
        }

        /* 3. Decrypt / Unwrap GTK from Key Data if present */
        uint16_t key_data_len = NET_NTOHS(key_hdr->key_data_length);
        if (key_data_len >= 24U)
        {
            const uint8_t *key_data_ptr = frame + sizeof(eapol_ethernet_hdr_t) + sizeof(eapol_key_header_t);
            uint8_t plain_key_data[128];
            uint16_t plain_len = 0U;

            if (wpa2_crypto_aes_unwrap(s_ptk.kek, key_data_ptr, key_data_len, plain_key_data, &plain_len) == WPA2_OK)
            {
                /* Extract GTK payload (skip KDE header if present, or copy 16 bytes) */
                if (plain_len >= 16U)
                {
                    size_t offset = (plain_len >= 24U && plain_key_data[0] == 0xDDU) ? 8U : 0U;
                    memcpy(s_gtk, &plain_key_data[offset], WPA2_GTK_LEN);
                    s_wpa2_telem.has_gtk = true;
                }
            }
        }

        s_wpa2_state = WPA2_STATE_4WAY_M3_RECEIVED;

        /* Synthesize and Transmit Message 4 (STA -> AP) */
        uint8_t tx_frame[NET_MAX_FRAME_SIZE];
        eapol_ethernet_hdr_t *tx_eth = (eapol_ethernet_hdr_t *)tx_frame;
        eapol_key_header_t *tx_key = (eapol_key_header_t *)(tx_frame + sizeof(eapol_ethernet_hdr_t));

        memcpy(tx_eth->dest_mac, s_ap_bssid, WPA2_MAC_ADDR_LEN);
        memcpy(tx_eth->src_mac, s_sta_mac, WPA2_MAC_ADDR_LEN);
        tx_eth->ethertype = NET_HTONS(ETHERTYPE_EAPOL);
        tx_eth->version = EAPOL_VERSION_1;
        tx_eth->type = EAPOL_TYPE_KEY;
        tx_eth->length = NET_HTONS((uint16_t)sizeof(eapol_key_header_t));

        tx_key->descriptor_type = EAPOL_DESC_TYPE_RSN;
        tx_key->key_info = NET_HTONS(WPA2_MSG4_KEY_INFO_NOMINAL);
        tx_key->key_length = NET_HTONS(0U);
        memcpy(tx_key->replay_counter, s_last_replay, WPA2_REPLAY_LEN);
        memset(tx_key->key_nonce, 0, WPA2_NONCE_LEN);
        memset(tx_key->key_iv, 0, 16);
        memset(tx_key->key_rsc, 0, 8);
        memset(tx_key->key_id, 0, 8);
        memset(tx_key->key_mic, 0, WPA2_MIC_LEN);
        tx_key->key_data_length = NET_HTONS(0U);

        uint16_t total_len = (uint16_t)(sizeof(eapol_ethernet_hdr_t) + sizeof(eapol_key_header_t));
        wpa2_crypto_compute_mic(s_ptk.kck, tx_frame, total_len, tx_key->key_mic);

        /* Transmit Message 4 */
        wifi_tx_packet(WIFI_TX_IF_STA, tx_frame, total_len);
        s_wpa2_telem.m4_tx_count++;

        /* Mark Complete & Authenticated */
        s_wpa2_state = WPA2_STATE_AUTHENTICATED;
        s_wpa2_telem.state = WPA2_STATE_AUTHENTICATED;
        s_wpa2_telem.handshakes_completed++;
        s_wpa2_telem.last_handshake_duration_us = systimer_get_us() - s_handshake_start_us;

        /* Start Station DHCP client upon successful 4-Way Handshake */
        dhcp_client_start();

        return WPA2_OK;
    }

    return WPA2_OK;
}

void wpa2_client_on_connected(const uint8_t *bssid)
{
    if (bssid != NULL)
    {
        memcpy(s_ap_bssid, bssid, WPA2_MAC_ADDR_LEN);
        memcpy(s_wpa2_telem.target_bssid, bssid, WPA2_MAC_ADDR_LEN);
    }
    if (s_wpa2_state != WPA2_STATE_AUTHENTICATED)
    {
        s_wpa2_state = WPA2_STATE_CONNECTING;
        s_wpa2_telem.state = WPA2_STATE_CONNECTING;
    }
}

void wpa2_client_on_disconnected(uint8_t reason)
{
    (void)reason;
    s_wpa2_state = WPA2_STATE_DISCONNECTED;
    s_wpa2_telem.state = WPA2_STATE_DISCONNECTED;
    s_wpa2_telem.has_ptk = false;
    s_wpa2_telem.has_gtk = false;
}

wpa2_status_t wpa2_client_configure(const char *ssid, const char *passphrase)
{
    if (ssid == NULL || passphrase == NULL)
    {
        return WPA2_ERR_INVALID_ARG;
    }
    size_t slen = strlen(ssid);
    size_t plen = strlen(passphrase);
    if (slen == 0U || slen > WPA2_MAX_SSID_LEN || plen < WPA2_MIN_PASS_LEN || plen > WPA2_MAX_PASS_LEN)
    {
        return WPA2_ERR_INVALID_ARG;
    }

    memset(s_configured_ssid, 0, sizeof(s_configured_ssid));
    memset(s_configured_pass, 0, sizeof(s_configured_pass));
    strncpy(s_configured_ssid, ssid, WPA2_MAX_SSID_LEN);
    strncpy(s_configured_pass, passphrase, WPA2_MAX_PASS_LEN);
    strncpy(s_wpa2_telem.target_ssid, ssid, WPA2_MAX_SSID_LEN);

    /* Reset handshake telemetry & keys for clean connection attempt */
    memset(s_wpa2_telem.target_bssid, 0, sizeof(s_wpa2_telem.target_bssid));
    s_wpa2_telem.has_ptk = false;
    s_wpa2_telem.has_gtk = false;
    s_wpa2_telem.handshakes_attempted = 0U;
    s_wpa2_telem.handshakes_completed = 0U;
    s_wpa2_telem.m1_rx_count = 0U;
    s_wpa2_telem.m2_tx_count = 0U;
    s_wpa2_telem.m3_rx_count = 0U;
    s_wpa2_telem.m4_tx_count = 0U;
    s_wpa2_telem.mic_failures = 0U;
    s_wpa2_telem.replay_errors = 0U;

    /* Derive PMK immediately */
    wpa2_status_t pst = wpa2_crypto_pbkdf2_sha1(s_configured_pass, s_configured_ssid,
                                                WPA2_PBKDF2_ITERATIONS, s_pmk);
    if (pst == WPA2_OK)
    {
        s_wpa2_telem.has_pmk = true;
    }
    return pst;
}

wpa2_status_t wpa2_client_handover_chan(const char *ssid, const char *passphrase, uint8_t channel)
{
    wpa2_status_t cst = wpa2_client_configure(ssid, passphrase);
    if (cst != WPA2_OK)
    {
        return cst;
    }

    /* 1. Stop SoftAP if active */
    if (wifi_is_ap_active())
    {
        wifi_stop_ap();
    }

    /* Reset IP configuration until DHCP client binds */
    net_set_ip(0U, 0U, 0U);
    dhcp_client_init();

    /* 2. Configure Station Interface */
    uint8_t mac[WPA2_MAC_ADDR_LEN];
    wifi_get_mac_addr(mac);
    memcpy(s_sta_mac, mac, WPA2_MAC_ADDR_LEN);
    memcpy(s_wpa2_telem.local_mac, mac, WPA2_MAC_ADDR_LEN);

    /* 3. Transition State */
    s_wpa2_state = WPA2_STATE_CONNECTING;
    s_wpa2_telem.state = WPA2_STATE_CONNECTING;

    /* 4. Trigger Station association */
    wifi_start_sta_chan(ssid, passphrase, channel);

    return WPA2_OK;
}

wpa2_status_t wpa2_client_handover(const char *ssid, const char *passphrase)
{
    return wpa2_client_handover_chan(ssid, passphrase, 0U);
}

void wpa2_client_print_status(void)
{
#if defined(__riscv)
    console_puts("\r\n=======================================================\r\n");
    console_puts("        WPA2-PSK 802.11i STATION CLIENT TELEMETRY      \r\n");
    console_puts("=======================================================\r\n");
    console_puts("  State:               ");
    console_puts(wpa2_state_to_str(s_wpa2_state));
    console_puts("\r\n  Target SSID:         ");
    console_puts(s_wpa2_telem.target_ssid);
    console_puts("\r\n  Target BSSID:        ");
    const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++)
    {
        uint8_t b = s_wpa2_telem.target_bssid[i];
        console_putc(hex_chars[(b >> 4) & 0x0F]);
        console_putc(hex_chars[b & 0x0F]);
        if (i < 5) console_puts(":");
    }
    console_puts("\r\n  Keys Established:    PMK=");
    put_dec(s_wpa2_telem.has_pmk);
    console_puts(", PTK=");
    put_dec(s_wpa2_telem.has_ptk);
    console_puts(", GTK=");
    put_dec(s_wpa2_telem.has_gtk);
    console_puts("\r\n  Handshakes:          Completed=");
    put_dec(s_wpa2_telem.handshakes_completed);
    console_puts(", Attempted=");
    put_dec(s_wpa2_telem.handshakes_attempted);
    console_puts("\r\n  Packets Exchanged:   M1_Rx=");
    put_dec(s_wpa2_telem.m1_rx_count);
    console_puts(", M2_Tx=");
    put_dec(s_wpa2_telem.m2_tx_count);
    console_puts(", M3_Rx=");
    put_dec(s_wpa2_telem.m3_rx_count);
    console_puts(", M4_Tx=");
    put_dec(s_wpa2_telem.m4_tx_count);
    console_puts("\r\n  Security Audits:     MIC_Fail=");
    put_dec(s_wpa2_telem.mic_failures);
    console_puts(", Replay_Err=");
    put_dec(s_wpa2_telem.replay_errors);
    console_puts("\r\n=======================================================\r\n");
#endif
}

const char *wpa2_state_to_str(wpa2_state_t state)
{
    switch (state)
    {
        case WPA2_STATE_DISCONNECTED:      return "DISCONNECTED";
        case WPA2_STATE_CONNECTING:        return "CONNECTING";
        case WPA2_STATE_4WAY_M1_RECEIVED:  return "4WAY_M1_RECEIVED";
        case WPA2_STATE_4WAY_M2_SENT:      return "4WAY_M2_SENT";
        case WPA2_STATE_4WAY_M3_RECEIVED:  return "4WAY_M3_RECEIVED";
        case WPA2_STATE_4WAY_M4_SENT:      return "4WAY_M4_SENT";
        case WPA2_STATE_AUTHENTICATED:     return "AUTHENTICATED";
        case WPA2_STATE_FAILED:            return "FAILED";
        default:                           return "UNKNOWN";
    }
}

/* ========================================================================= */
/* Core Lifecycle APIs (.flash.text)                                         */
/* ========================================================================= */
wpa2_status_t wpa2_client_init(void)
{
    s_wpa2_state = WPA2_STATE_DISCONNECTED;
    memset(&s_wpa2_telem, 0, sizeof(s_wpa2_telem));
    memset(s_pmk, 0, sizeof(s_pmk));
    memset(&s_ptk, 0, sizeof(s_ptk));
    memset(s_gtk, 0, sizeof(s_gtk));
    memset(s_anonce, 0, sizeof(s_anonce));
    memset(s_snonce, 0, sizeof(s_snonce));
    memset(s_last_replay, 0, sizeof(s_last_replay));
    memset(s_ap_bssid, 0, sizeof(s_ap_bssid));
    memset(s_configured_ssid, 0, sizeof(s_configured_ssid));
    memset(s_configured_pass, 0, sizeof(s_configured_pass));

    uint8_t mac[WPA2_MAC_ADDR_LEN];
    if (wifi_get_mac_addr(mac) == WIFI_OK)
    {
        memcpy(s_sta_mac, mac, WPA2_MAC_ADDR_LEN);
        memcpy(s_wpa2_telem.local_mac, mac, WPA2_MAC_ADDR_LEN);
    }

    s_wpa2_initialized = true;
    return WPA2_OK;
}

wpa2_status_t wpa2_client_start(void)
{
    if (!s_wpa2_initialized)
    {
        wpa2_client_init();
    }
    s_wpa2_state = WPA2_STATE_CONNECTING;
    s_wpa2_telem.state = WPA2_STATE_CONNECTING;
    return WPA2_OK;
}

wpa2_status_t wpa2_client_stop(void)
{
    s_wpa2_state = WPA2_STATE_DISCONNECTED;
    s_wpa2_telem.state = WPA2_STATE_DISCONNECTED;
    return WPA2_OK;
}

wpa2_state_t wpa2_client_get_state(void)
{
    return s_wpa2_state;
}

bool wpa2_client_is_in_4way(void)
{
    return (s_wpa2_state >= WPA2_STATE_4WAY_M1_RECEIVED && s_wpa2_state <= WPA2_STATE_4WAY_M4_SENT);
}

bool wpa2_client_is_authenticated(void)
{
    return (s_wpa2_state == WPA2_STATE_AUTHENTICATED);
}

wpa2_status_t wpa2_client_get_telemetry(wpa2_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return WPA2_ERR_INVALID_ARG;
    }
    s_wpa2_telem.state = s_wpa2_state;
    memcpy(out_telem, &s_wpa2_telem, sizeof(wpa2_telemetry_t));
    return WPA2_OK;
}
