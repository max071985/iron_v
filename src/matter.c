/*
 * matter.c
 *
 * Iron V Google Home Matter-over-Thread/Wi-Fi Readiness & Commissioning Bridge
 *
 * Implements Matter Specification v1.2 setup payloads, manual pairing code generator
 * and parser with Verhoeff error detection, Base38 QR code serialization,
 * data model clusters (Basic Info, General Commissioning, On/Off), commissioning
 * lifecycle FSM, and ESP32-C6 hardware crypto accelerator (SHA/ECC) interfacing.
 */

#include "matter.h"
#include "string.h"

#if defined(__riscv)
#include "regs/sha.h"
#include "regs/ecc.h"
#include "regs/pcr.h"
#endif

/* ========================================================================= */
/* Zero Magic Numbers: File-Internal Constants                               */
/* ========================================================================= */

#define VERHOEFF_TABLE_ROWS                     10U
#define VERHOEFF_TABLE_COLS                     10U
#define VERHOEFF_PERM_ROWS                      8U
#define VERHOEFF_PERM_COLS                      10U
#define VERHOEFF_INV_LEN                        10U

#define BASE38_ALPHABET_LEN                     38U
#define BASE38_CHUNK3_CHARS                     5U
#define BASE38_CHUNK2_CHARS                     4U
#define BASE38_CHUNK1_CHARS                     2U
#define BASE38_CHUNK3_BYTES                     3U
#define BASE38_CHUNK2_BYTES                     2U
#define BASE38_CHUNK1_BYTES                     1U

#define MATTER_SETUP_PAYLOAD_RAW_BYTES          11U

#define MATTER_CHUNK1_DIGITS                    1U
#define MATTER_CHUNK2_DIGITS                    5U
#define MATTER_CHUNK3_DIGITS                    4U
#define MATTER_CHUNK4_DIGITS                    5U
#define MATTER_CHUNK5_DIGITS                    5U
#define MATTER_CHECK_DIGITS                     1U

#define MATTER_PASSCODE_LOW_MASK                0x3FFFU /* 14 bits */
#define MATTER_PASSCODE_LOW_SHIFT               14U
#define MATTER_PASSCODE_HIGH_MASK               0x1FFFU /* 13 bits */

#define MATTER_DISC_SHORT_SHIFT                 8U
#define MATTER_DISC_SHORT_MASK                  0x0FU
#define MATTER_DISC_SHORT_HIGH_SHIFT            2U
#define MATTER_DISC_SHORT_HIGH_MASK             0x03U
#define MATTER_DISC_SHORT_LOW_MASK              0x03U

#define MATTER_VID_PID_FLAG_SHIFT               2U
#define MATTER_VID_PID_PRESENT_FLAG             1U

#define MATTER_DEFAULT_FAILSAFE_SEC             60U
#define MATTER_DEFAULT_NODE_ID                  0x0000000000010001ULL

/* SHA-256 Constants */
#define SHA256_ROUNDS                           64U
#define SHA256_STATE_WORDS                      8U

/* Hardware Busy Timeout Cycles */
#define SHA_HARDWARE_TIMEOUT_CYCLES             100000U
#define ECC_HARDWARE_TIMEOUT_CYCLES             100000U

/* ========================================================================= */
/* Static Data Tables                                                        */
/* ========================================================================= */

/* Verhoeff Checksum Tables */
static const uint8_t s_verhoeff_d[VERHOEFF_TABLE_ROWS][VERHOEFF_TABLE_COLS] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
    {1, 2, 3, 4, 0, 6, 7, 8, 9, 5},
    {2, 3, 4, 0, 1, 7, 8, 9, 5, 6},
    {3, 4, 0, 1, 2, 8, 9, 5, 6, 7},
    {4, 0, 1, 2, 3, 9, 5, 6, 7, 8},
    {5, 9, 8, 7, 6, 0, 4, 3, 2, 1},
    {6, 5, 9, 8, 7, 1, 0, 4, 3, 2},
    {7, 6, 5, 9, 8, 2, 1, 0, 4, 3},
    {8, 7, 6, 5, 9, 3, 2, 1, 0, 4},
    {9, 8, 7, 6, 5, 4, 3, 2, 1, 0}
};

static const uint8_t s_verhoeff_p[VERHOEFF_PERM_ROWS][VERHOEFF_PERM_COLS] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
    {1, 5, 7, 6, 2, 8, 3, 0, 9, 4},
    {5, 8, 0, 3, 7, 9, 6, 1, 4, 2},
    {8, 9, 1, 6, 0, 4, 3, 5, 2, 7},
    {9, 4, 5, 3, 1, 2, 6, 8, 7, 0},
    {4, 2, 8, 6, 5, 7, 3, 9, 0, 1},
    {2, 7, 9, 3, 8, 0, 6, 4, 1, 5},
    {7, 0, 4, 6, 9, 1, 3, 2, 5, 8}
};

static const uint8_t s_verhoeff_inv[VERHOEFF_INV_LEN] = {
    0, 4, 3, 2, 1, 5, 6, 7, 8, 9
};

/* Base38 Character Set (Matter Specification v1.2) */
static const char s_base38_alphabet[BASE38_ALPHABET_LEN + 1] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.";

/* SHA-256 Round Constants K */
static const uint32_t s_sha256_k[SHA256_ROUNDS] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

/* ========================================================================= */
/* Static State Variables (Deterministic Static Memory Only)                 */
/* ========================================================================= */

static matter_commissioning_info_t s_matter_info = {
    .vendor_id              = MATTER_DEFAULT_VENDOR_ID,
    .product_id             = MATTER_DEFAULT_PRODUCT_ID,
    .discriminator          = MATTER_DEFAULT_DISCRIMINATOR,
    .setup_passcode         = MATTER_DEFAULT_PASSCODE,
    .commissioning_flow     = MATTER_COMMISSIONING_FLOW_STANDARD,
    .discovery_capabilities = (MATTER_DISCOVERY_CAP_BLE | MATTER_DISCOVERY_CAP_ONNETWORK)
};

static matter_telemetry_t s_matter_telem = {
    .state                    = MATTER_COMMISSIONING_STATE_UNINITIALIZED,
    .transport                = MATTER_TRANSPORT_THREAD,
    .fabric_count             = 0U,
    .node_id                  = 0ULL,
    .fabric_index             = 0U,
    .onoff_state              = false,
    .failsafe_armed           = false,
    .failsafe_remaining_sec   = 0U,
    .ble_reclaimed            = false,
    .crypto_hw_accelerated    = false,
    .total_commands_processed = 0U,
    .total_attribute_reads    = 0U,
    .total_attribute_writes   = 0U,
    .sha_date_reg             = 0U,
    .ecc_date_reg             = 0U
};

/* ========================================================================= */
/* Internal Helper Functions                                                 */
/* ========================================================================= */

static inline uint32_t rotr32(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32U - n));
}

static void sha256_transform(uint32_t state[SHA256_STATE_WORDS], const uint8_t block[MATTER_SHA256_BLOCK_SIZE])
{
    uint32_t w[SHA256_ROUNDS];

    for (uint32_t t = 0; t < 16U; t++)
    {
        w[t] = ((uint32_t)block[t * 4U] << 24) |
               ((uint32_t)block[t * 4U + 1U] << 16) |
               ((uint32_t)block[t * 4U + 2U] << 8) |
               ((uint32_t)block[t * 4U + 3U]);
    }

    for (uint32_t t = 16U; t < SHA256_ROUNDS; t++)
    {
        uint32_t s0 = rotr32(w[t - 15U], 7) ^ rotr32(w[t - 15U], 18) ^ (w[t - 15U] >> 3);
        uint32_t s1 = rotr32(w[t - 2U], 17) ^ rotr32(w[t - 2U], 19) ^ (w[t - 2U] >> 10);
        w[t] = w[t - 16U] + s0 + w[t - 7U] + s1;
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];

    for (uint32_t t = 0; t < SHA256_ROUNDS; t++)
    {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + s_sha256_k[t] + w[t];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

/* ========================================================================= */
/* Hardware Crypto Accelerator Interfacing                                   */
/* ========================================================================= */

matter_status_t matter_crypto_hw_init(void)
{
#if defined(__riscv)
    /* 1. Enable SHA Peripheral Clock & Clear Reset in PCR */
    *PCR_SHA_CONF_REG |= PCR_SHA_CONF_SHA_CLK_EN_M;
    *PCR_SHA_CONF_REG &= ~PCR_SHA_CONF_SHA_RST_EN_M;

    /* Read SHA Date Register */
    s_matter_telem.sha_date_reg = *SHA_DATE_REG;

    /* 2. Enable ECC Peripheral Clock & Power in PCR */
    *PCR_ECC_CONF_REG |= PCR_ECC_CONF_ECC_CLK_EN_M;
    *PCR_ECC_CONF_REG &= ~PCR_ECC_CONF_ECC_RST_EN_M;
    *PCR_ECC_PD_CTRL_REG |= PCR_ECC_PD_CTRL_ECC_MEM_FORCE_PU_M;
    *PCR_ECC_PD_CTRL_REG &= ~PCR_ECC_PD_CTRL_ECC_MEM_PD_M;

    /* Configure ECC Accelerator: P-256 Curve Mode */
    *ECC_MULT_CONF_REG |= ECC_MULT_CONF_CLK_EN_M | ECC_MULT_CONF_KEY_LENGTH_M;

    /* Read ECC Date Register */
    s_matter_telem.ecc_date_reg = *ECC_MULT_DATE_REG;

    s_matter_telem.crypto_hw_accelerated = true;
#else
    /* Host environment mock */
    s_matter_telem.sha_date_reg = MATTER_SHA_DATE_EXPECTED;
    s_matter_telem.ecc_date_reg = MATTER_ECC_DATE_EXPECTED;
    s_matter_telem.crypto_hw_accelerated = false;
#endif

    return MATTER_OK;
}

uint32_t matter_get_sha_date(void)
{
    return s_matter_telem.sha_date_reg;
}

uint32_t matter_get_ecc_date(void)
{
    return s_matter_telem.ecc_date_reg;
}

matter_status_t matter_crypto_sha256(
    const uint8_t *data,
    size_t len,
    uint8_t *digest_out
)
{
    if (digest_out == NULL || (data == NULL && len > 0U))
    {
        return MATTER_ERR_INVALID_PARAM;
    }

    uint32_t state[SHA256_STATE_WORDS] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };

    uint8_t block[MATTER_SHA256_BLOCK_SIZE];
    size_t offset = 0U;

    /* Process full 64-byte blocks */
    while ((len - offset) >= MATTER_SHA256_BLOCK_SIZE)
    {
#if defined(__riscv)
        if (s_matter_telem.crypto_hw_accelerated)
        {
            /* Write block to hardware message memory */
            volatile uint32_t *m_mem = (volatile uint32_t *)SHA_M_MEM_REG;
            const uint32_t *src_w = (const uint32_t *)(data + offset);
            for (uint32_t i = 0; i < 16U; i++)
            {
                m_mem[i] = src_w[i];
            }

            *SHA_MODE_REG = 2U; /* SHA-256 */
            if (offset == 0U)
            {
                *SHA_START_REG = 1U;
            }
            else
            {
                *SHA_CONTINUE_REG = 1U;
            }

            uint32_t timeout = SHA_HARDWARE_TIMEOUT_CYCLES;
            while ((*SHA_BUSY_REG & SHA_BUSY_STATE_M) && --timeout) {}
        }
#endif
        memcpy(block, data + offset, MATTER_SHA256_BLOCK_SIZE);
        sha256_transform(state, block);
        offset += MATTER_SHA256_BLOCK_SIZE;
    }

    /* Padding block */
    size_t rem = len - offset;
    memset(block, 0, MATTER_SHA256_BLOCK_SIZE);
    if (rem > 0U)
    {
        memcpy(block, data + offset, rem);
    }
    block[rem] = 0x80U;

    if (rem >= 56U)
    {
        sha256_transform(state, block);
        memset(block, 0, MATTER_SHA256_BLOCK_SIZE);
    }

    uint64_t total_bits = (uint64_t)len * 8ULL;
    for (uint32_t i = 0; i < 8U; i++)
    {
        block[63U - i] = (uint8_t)((total_bits >> (i * 8U)) & 0xFFU);
    }
    sha256_transform(state, block);

    /* Output digest */
    for (uint32_t i = 0; i < SHA256_STATE_WORDS; i++)
    {
        digest_out[i * 4U]      = (uint8_t)((state[i] >> 24) & 0xFFU);
        digest_out[i * 4U + 1U]  = (uint8_t)((state[i] >> 16) & 0xFFU);
        digest_out[i * 4U + 2U]  = (uint8_t)((state[i] >> 8) & 0xFFU);
        digest_out[i * 4U + 3U]  = (uint8_t)(state[i] & 0xFFU);
    }

    return MATTER_OK;
}

/* ========================================================================= */
/* Verhoeff Error-Detection Checksum                                         */
/* ========================================================================= */

uint8_t matter_verhoeff_compute(const char *digits)
{
    if (digits == NULL)
    {
        return 0U;
    }

    size_t len = strlen(digits);
    uint8_t c = 0U;

    for (size_t i = 0U; i < len; i++)
    {
        char ch = digits[len - 1U - i];
        if (ch < '0' || ch > '9')
        {
            continue;
        }
        uint8_t digit = (uint8_t)(ch - '0');
        uint8_t p = s_verhoeff_p[(i + 1U) % VERHOEFF_PERM_ROWS][digit];
        c = s_verhoeff_d[c][p];
    }

    return s_verhoeff_inv[c];
}

bool matter_verhoeff_validate(const char *digits)
{
    if (digits == NULL)
    {
        return false;
    }

    size_t len = strlen(digits);
    if (len == 0U)
    {
        return false;
    }

    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++)
    {
        char ch = digits[len - 1U - i];
        if (ch < '0' || ch > '9')
        {
            return false;
        }
        uint8_t digit = (uint8_t)(ch - '0');
        uint8_t p = s_verhoeff_p[i % VERHOEFF_PERM_ROWS][digit];
        c = s_verhoeff_d[c][p];
    }

    return (c == 0U);
}

/* ========================================================================= */
/* Matter Manual Pairing Code Generation & Parsing                           */
/* ========================================================================= */

static void uint32_to_padded_str(uint32_t val, uint32_t digits, char *out)
{
    for (int32_t i = (int32_t)digits - 1; i >= 0; i--)
    {
        out[i] = (char)('0' + (val % 10U));
        val /= 10U;
    }
    out[digits] = '\0';
}

matter_status_t matter_generate_manual_pairing_code(
    const matter_commissioning_info_t *info,
    char *out_str,
    size_t out_max,
    bool formatted
)
{
    if (info == NULL || out_str == NULL)
    {
        return MATTER_ERR_INVALID_PARAM;
    }

    uint8_t short_disc = (uint8_t)((info->discriminator >> MATTER_DISC_SHORT_SHIFT) & MATTER_DISC_SHORT_MASK);
    uint32_t passcode_low = info->setup_passcode & MATTER_PASSCODE_LOW_MASK;
    uint32_t passcode_high = (info->setup_passcode >> MATTER_PASSCODE_LOW_SHIFT) & MATTER_PASSCODE_HIGH_MASK;

    char digits_buf[32];
    size_t pos = 0U;

    if (info->commissioning_flow == MATTER_COMMISSIONING_FLOW_STANDARD)
    {
        /* 11-digit Standard Manual Code */
        uint32_t chunk1 = (uint32_t)((short_disc >> MATTER_DISC_SHORT_HIGH_SHIFT) & MATTER_DISC_SHORT_HIGH_MASK);
        uint32_t chunk2 = ((uint32_t)(short_disc & MATTER_DISC_SHORT_LOW_MASK) << MATTER_PASSCODE_LOW_SHIFT) | passcode_low;
        uint32_t chunk3 = passcode_high;

        digits_buf[pos++] = (char)('0' + chunk1);
        uint32_to_padded_str(chunk2, MATTER_CHUNK2_DIGITS, &digits_buf[pos]);
        pos += MATTER_CHUNK2_DIGITS;
        uint32_to_padded_str(chunk3, MATTER_CHUNK3_DIGITS, &digits_buf[pos]);
        pos += MATTER_CHUNK3_DIGITS;
        digits_buf[pos] = '\0';

        uint8_t check_digit = matter_verhoeff_compute(digits_buf);
        digits_buf[pos++] = (char)('0' + check_digit);
        digits_buf[pos] = '\0';

        if (formatted)
        {
            /* Format: XXXX-XXX-XXXX (13 chars + null) */
            if (out_max < 14U)
            {
                return MATTER_ERR_BUFFER_TOO_SMALL;
            }
            out_str[0]  = digits_buf[0];
            out_str[1]  = digits_buf[1];
            out_str[2]  = digits_buf[2];
            out_str[3]  = digits_buf[3];
            out_str[4]  = '-';
            out_str[5]  = digits_buf[4];
            out_str[6]  = digits_buf[5];
            out_str[7]  = digits_buf[6];
            out_str[8]  = '-';
            out_str[9]  = digits_buf[7];
            out_str[10] = digits_buf[8];
            out_str[11] = digits_buf[9];
            out_str[12] = digits_buf[10];
            out_str[13] = '\0';
        }
        else
        {
            if (out_max < 12U)
            {
                return MATTER_ERR_BUFFER_TOO_SMALL;
            }
            memcpy(out_str, digits_buf, 12U);
        }
    }
    else
    {
        /* 21-digit Extended Manual Code (with VID & PID) */
        uint32_t chunk1 = (MATTER_VID_PID_PRESENT_FLAG << MATTER_VID_PID_FLAG_SHIFT) |
                          ((short_disc >> MATTER_DISC_SHORT_HIGH_SHIFT) & MATTER_DISC_SHORT_HIGH_MASK);
        uint32_t chunk2 = ((uint32_t)(short_disc & MATTER_DISC_SHORT_LOW_MASK) << MATTER_PASSCODE_LOW_SHIFT) | passcode_low;
        uint32_t chunk3 = passcode_high;
        uint32_t chunk4 = info->vendor_id;
        uint32_t chunk5 = info->product_id;

        digits_buf[pos++] = (char)('0' + chunk1);
        uint32_to_padded_str(chunk2, MATTER_CHUNK2_DIGITS, &digits_buf[pos]);
        pos += MATTER_CHUNK2_DIGITS;
        uint32_to_padded_str(chunk3, MATTER_CHUNK3_DIGITS, &digits_buf[pos]);
        pos += MATTER_CHUNK3_DIGITS;
        uint32_to_padded_str(chunk4, MATTER_CHUNK4_DIGITS, &digits_buf[pos]);
        pos += MATTER_CHUNK4_DIGITS;
        uint32_to_padded_str(chunk5, MATTER_CHUNK5_DIGITS, &digits_buf[pos]);
        pos += MATTER_CHUNK5_DIGITS;
        digits_buf[pos] = '\0';

        uint8_t check_digit = matter_verhoeff_compute(digits_buf);
        digits_buf[pos++] = (char)('0' + check_digit);
        digits_buf[pos] = '\0';

        if (formatted)
        {
            /* Format: XXXX-XXXX-XXXX-XXXX-XXXXX (25 chars + null) */
            if (out_max < 26U)
            {
                return MATTER_ERR_BUFFER_TOO_SMALL;
            }
            size_t out_idx = 0U;
            for (size_t i = 0U; i < pos; i++)
            {
                if (i > 0U && (i % 4U) == 0U && out_idx < 25U && i < 16U)
                {
                    out_str[out_idx++] = '-';
                }
                else if (i == 16U && out_idx < 25U)
                {
                    out_str[out_idx++] = '-';
                }
                out_str[out_idx++] = digits_buf[i];
            }
            out_str[out_idx] = '\0';
        }
        else
        {
            if (out_max < 22U)
            {
                return MATTER_ERR_BUFFER_TOO_SMALL;
            }
            memcpy(out_str, digits_buf, 22U);
        }
    }

    return MATTER_OK;
}

matter_status_t matter_parse_manual_pairing_code(
    const char *in_str,
    matter_commissioning_info_t *out_info
)
{
    if (in_str == NULL || out_info == NULL)
    {
        return MATTER_ERR_INVALID_PARAM;
    }

    char clean[32];
    size_t clean_len = 0U;

    for (size_t i = 0U; in_str[i] != '\0'; i++)
    {
        char c = in_str[i];
        if (c >= '0' && c <= '9')
        {
            if (clean_len >= 31U)
            {
                return MATTER_ERR_INVALID_PARAM;
            }
            clean[clean_len++] = c;
        }
        else if (c == '-' || c == ' ' || c == '\r' || c == '\n')
        {
            continue;
        }
        else
        {
            return MATTER_ERR_INVALID_PARAM;
        }
    }
    clean[clean_len] = '\0';

    if (clean_len != MATTER_MANUAL_CODE_LEN_STANDARD &&
        clean_len != MATTER_MANUAL_CODE_LEN_EXTENDED)
    {
        return MATTER_ERR_INVALID_PARAM;
    }

    if (!matter_verhoeff_validate(clean))
    {
        return MATTER_ERR_CHECKSUM;
    }

    uint32_t chunk1 = (uint32_t)(clean[0] - '0');
    uint32_t chunk2 = 0U;
    for (size_t i = 1U; i <= 5U; i++)
    {
        chunk2 = chunk2 * 10U + (uint32_t)(clean[i] - '0');
    }
    uint32_t chunk3 = 0U;
    for (size_t i = 6U; i <= 9U; i++)
    {
        chunk3 = chunk3 * 10U + (uint32_t)(clean[i] - '0');
    }

    uint8_t short_disc_high = (uint8_t)(chunk1 & MATTER_DISC_SHORT_HIGH_MASK);
    uint8_t short_disc_low  = (uint8_t)((chunk2 >> MATTER_PASSCODE_LOW_SHIFT) & MATTER_DISC_SHORT_LOW_MASK);
    uint8_t short_disc = (short_disc_high << MATTER_DISC_SHORT_HIGH_SHIFT) | short_disc_low;

    out_info->discriminator = ((uint16_t)short_disc) << MATTER_DISC_SHORT_SHIFT;
    out_info->setup_passcode = ((chunk3 & MATTER_PASSCODE_HIGH_MASK) << MATTER_PASSCODE_LOW_SHIFT) |
                               (chunk2 & MATTER_PASSCODE_LOW_MASK);

    if (clean_len == MATTER_MANUAL_CODE_LEN_EXTENDED)
    {
        uint32_t chunk4 = 0U;
        for (size_t i = 10U; i <= 14U; i++)
        {
            chunk4 = chunk4 * 10U + (uint32_t)(clean[i] - '0');
        }
        uint32_t chunk5 = 0U;
        for (size_t i = 15U; i <= 19U; i++)
        {
            chunk5 = chunk5 * 10U + (uint32_t)(clean[i] - '0');
        }
        out_info->vendor_id = (uint16_t)chunk4;
        out_info->product_id = (uint16_t)chunk5;
        out_info->commissioning_flow = MATTER_COMMISSIONING_FLOW_USER_ACTION;
    }
    else
    {
        out_info->vendor_id = s_matter_info.vendor_id;
        out_info->product_id = s_matter_info.product_id;
        out_info->commissioning_flow = MATTER_COMMISSIONING_FLOW_STANDARD;
    }

    out_info->discovery_capabilities = s_matter_info.discovery_capabilities;

    return MATTER_OK;
}

/* ========================================================================= */
/* Base38 QR Code Onboarding Payload Serialization                           */
/* ========================================================================= */

matter_status_t matter_generate_qr_code_payload(
    const matter_commissioning_info_t *info,
    char *out_str,
    size_t out_max
)
{
    if (info == NULL || out_str == NULL)
    {
        return MATTER_ERR_INVALID_PARAM;
    }

    if (out_max < (MATTER_QR_PREFIX_LEN + MATTER_SETUP_PAYLOAD_QR_CHARS + 1U))
    {
        return MATTER_ERR_BUFFER_TOO_SMALL;
    }

    /* Bit-pack 84 bits (11 bytes) according to Matter Spec v1.2 §5.1.3:
     * Bits 0..2:   Version (0)
     * Bits 3..18:  Vendor ID (16 bits)
     * Bits 19..34: Product ID (16 bits)
     * Bits 35..36: Commissioning Flow (2 bits)
     * Bits 37..44: Discovery Capabilities (8 bits)
     * Bits 45..56: Discriminator (12 bits)
     * Bits 57..83: Setup Passcode (27 bits)
     */
    uint8_t raw[MATTER_SETUP_PAYLOAD_RAW_BYTES];
    memset(raw, 0, sizeof(raw));

    uint64_t w0 = 0ULL;
    w0 |= ((uint64_t)0U & 0x07ULL);                                                /* Version: 0 */
    w0 |= (((uint64_t)info->vendor_id & 0xFFFFULL) << 3);                           /* VID: bits 3..18 */
    w0 |= (((uint64_t)info->product_id & 0xFFFFULL) << 19);                         /* PID: bits 19..34 */
    w0 |= (((uint64_t)info->commissioning_flow & 0x03ULL) << 35);                  /* Flow: bits 35..36 */
    w0 |= (((uint64_t)info->discovery_capabilities & 0xFFULL) << 37);              /* Caps: bits 37..44 */
    w0 |= (((uint64_t)info->discriminator & 0x0FFFULL) << 45);                      /* Disc: bits 45..56 */
    w0 |= (((uint64_t)info->setup_passcode & 0x7FULL) << 57);                      /* Passcode [6:0]: bits 57..63 */

    uint32_t w1 = (uint32_t)((info->setup_passcode >> 7) & 0xFFFFFU);             /* Passcode [26:7]: bits 64..83 */

    for (uint32_t i = 0U; i < 8U; i++)
    {
        raw[i] = (uint8_t)((w0 >> (i * 8U)) & 0xFFU);
    }
    for (uint32_t i = 0U; i < 3U; i++)
    {
        raw[8U + i] = (uint8_t)((w1 >> (i * 8U)) & 0xFFU);
    }

    /* Prefix with "MT:" */
    memcpy(out_str, MATTER_QR_PREFIX, MATTER_QR_PREFIX_LEN);
    size_t out_idx = MATTER_QR_PREFIX_LEN;

    /* Base38 encode 11 bytes: 3 chunks of 3 bytes (5 chars each) + 1 chunk of 2 bytes (4 chars) */
    size_t in_idx = 0U;
    while (in_idx < MATTER_SETUP_PAYLOAD_RAW_BYTES)
    {
        size_t rem = MATTER_SETUP_PAYLOAD_RAW_BYTES - in_idx;
        uint32_t val = 0U;
        uint32_t char_count = 0U;

        if (rem >= BASE38_CHUNK3_BYTES)
        {
            val = (uint32_t)raw[in_idx] |
                  ((uint32_t)raw[in_idx + 1U] << 8) |
                  ((uint32_t)raw[in_idx + 2U] << 16);
            char_count = BASE38_CHUNK3_CHARS;
            in_idx += BASE38_CHUNK3_BYTES;
        }
        else if (rem == BASE38_CHUNK2_BYTES)
        {
            val = (uint32_t)raw[in_idx] |
                  ((uint32_t)raw[in_idx + 1U] << 8);
            char_count = BASE38_CHUNK2_CHARS;
            in_idx += BASE38_CHUNK2_BYTES;
        }
        else
        {
            val = (uint32_t)raw[in_idx];
            char_count = BASE38_CHUNK1_CHARS;
            in_idx += BASE38_CHUNK1_BYTES;
        }

        for (uint32_t c = 0U; c < char_count; c++)
        {
            out_str[out_idx++] = s_base38_alphabet[val % BASE38_ALPHABET_LEN];
            val /= BASE38_ALPHABET_LEN;
        }
    }

    out_str[out_idx] = '\0';
    return MATTER_OK;
}

matter_status_t matter_parse_qr_code_payload(
    const char *in_str,
    matter_commissioning_info_t *out_info
)
{
    if (in_str == NULL || out_info == NULL)
    {
        return MATTER_ERR_INVALID_PARAM;
    }

    if (strncmp(in_str, MATTER_QR_PREFIX, MATTER_QR_PREFIX_LEN) != 0)
    {
        return MATTER_ERR_INVALID_PARAM;
    }

    const char *b38_payload = in_str + MATTER_QR_PREFIX_LEN;
    size_t payload_len = strlen(b38_payload);
    if (payload_len != MATTER_SETUP_PAYLOAD_QR_CHARS)
    {
        return MATTER_ERR_INVALID_PARAM;
    }

    uint8_t raw[MATTER_SETUP_PAYLOAD_RAW_BYTES];
    size_t in_idx = 0U;
    size_t out_idx = 0U;

    while (in_idx < payload_len)
    {
        size_t rem = payload_len - in_idx;
        uint32_t char_count = 0U;
        uint32_t byte_count = 0U;

        if (rem >= BASE38_CHUNK3_CHARS)
        {
            char_count = BASE38_CHUNK3_CHARS;
            byte_count = BASE38_CHUNK3_BYTES;
        }
        else if (rem == BASE38_CHUNK2_CHARS)
        {
            char_count = BASE38_CHUNK2_CHARS;
            byte_count = BASE38_CHUNK2_BYTES;
        }
        else
        {
            char_count = BASE38_CHUNK1_CHARS;
            byte_count = BASE38_CHUNK1_BYTES;
        }

        uint32_t val = 0U;
        uint32_t mult = 1U;

        for (uint32_t c = 0U; c < char_count; c++)
        {
            char ch = b38_payload[in_idx + c];
            int idx = -1;
            if (ch >= '0' && ch <= '9')
            {
                idx = (int)(ch - '0');
            }
            else if (ch >= 'A' && ch <= 'Z')
            {
                idx = 10 + (int)(ch - 'A');
            }
            else if (ch == '-')
            {
                idx = 36;
            }
            else if (ch == '.')
            {
                idx = 37;
            }

            if (idx < 0)
            {
                return MATTER_ERR_INVALID_PARAM;
            }
            val += (uint32_t)idx * mult;
            mult *= BASE38_ALPHABET_LEN;
        }

        for (uint32_t b = 0U; b < byte_count; b++)
        {
            raw[out_idx++] = (uint8_t)((val >> (b * 8U)) & 0xFFU);
        }
        in_idx += char_count;
    }

    /* Unpack bits */
    uint64_t w0 = 0ULL;
    for (uint32_t i = 0U; i < 8U; i++)
    {
        w0 |= ((uint64_t)raw[i] << (i * 8U));
    }
    uint32_t w1 = 0U;
    for (uint32_t i = 0U; i < 3U; i++)
    {
        w1 |= ((uint32_t)raw[8U + i] << (i * 8U));
    }

    out_info->vendor_id             = (uint16_t)((w0 >> 3) & 0xFFFFULL);
    out_info->product_id            = (uint16_t)((w0 >> 19) & 0xFFFFULL);
    out_info->commissioning_flow    = (uint8_t)((w0 >> 35) & 0x03ULL);
    out_info->discovery_capabilities = (uint8_t)((w0 >> 37) & 0xFFULL);
    out_info->discriminator         = (uint16_t)((w0 >> 45) & 0x0FFFULL);
    out_info->setup_passcode        = (uint32_t)(((w0 >> 57) & 0x7FULL) | ((uint64_t)(w1 & 0xFFFFFU) << 7));

    return MATTER_OK;
}

/* ========================================================================= */
/* Matter Data Model & Cluster Command Processing                            */
/* ========================================================================= */

matter_status_t matter_process_cluster_command(
    uint16_t endpoint,
    uint32_t cluster_id,
    uint32_t command_id,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *resp_buf,
    size_t *resp_len
)
{
    (void)payload;
    (void)payload_len;

    s_matter_telem.total_commands_processed++;

    if (endpoint == MATTER_ENDPOINT_APPLICATION && cluster_id == MATTER_CLUSTER_ONOFF)
    {
        if (command_id == MATTER_CMD_ONOFF_OFF)
        {
            s_matter_telem.onoff_state = false;
        }
        else if (command_id == MATTER_CMD_ONOFF_ON)
        {
            s_matter_telem.onoff_state = true;
        }
        else if (command_id == MATTER_CMD_ONOFF_TOGGLE)
        {
            s_matter_telem.onoff_state = !s_matter_telem.onoff_state;
        }
        else
        {
            return MATTER_ERR_NOT_FOUND;
        }
        s_matter_telem.total_attribute_writes++;

        if (resp_buf != NULL && resp_len != NULL && *resp_len >= 1U)
        {
            resp_buf[0] = 0U; /* Success */
            *resp_len = 1U;
        }
        return MATTER_OK;
    }
    else if (endpoint == MATTER_ENDPOINT_ROOT && cluster_id == MATTER_CLUSTER_GENERAL_COMMISSIONING)
    {
        if (command_id == MATTER_CMD_GENCOMM_ARM_FAILSAFE)
        {
            s_matter_telem.failsafe_armed = true;
            s_matter_telem.failsafe_remaining_sec = MATTER_DEFAULT_FAILSAFE_SEC;
            s_matter_telem.state = MATTER_COMMISSIONING_STATE_ARMED_FAILSAFE;
        }
        else if (command_id == MATTER_CMD_GENCOMM_SET_REGULATORY_CONFIG)
        {
            /* Regulatory config accepted */
        }
        else if (command_id == MATTER_CMD_GENCOMM_COMMISSIONING_COMPLETE)
        {
            s_matter_telem.state = MATTER_COMMISSIONING_STATE_COMMISSIONED;
            s_matter_telem.fabric_count = 1U;
            s_matter_telem.fabric_index = 1U;
            s_matter_telem.node_id = MATTER_DEFAULT_NODE_ID;
            s_matter_telem.failsafe_armed = false;
            s_matter_telem.failsafe_remaining_sec = 0U;
            s_matter_telem.ble_reclaimed = true;
        }
        else
        {
            return MATTER_ERR_NOT_FOUND;
        }
        s_matter_telem.total_attribute_writes++;

        if (resp_buf != NULL && resp_len != NULL && *resp_len >= 1U)
        {
            resp_buf[0] = 0U; /* Success */
            *resp_len = 1U;
        }
        return MATTER_OK;
    }

    return MATTER_ERR_NOT_FOUND;
}

/* ========================================================================= */
/* Subsystem Lifecycle & Control Functions                                   */
/* ========================================================================= */

matter_status_t matter_init(const matter_commissioning_info_t *cfg)
{
    if (cfg != NULL)
    {
        s_matter_info = *cfg;
    }
    else
    {
        s_matter_info.vendor_id              = MATTER_DEFAULT_VENDOR_ID;
        s_matter_info.product_id             = MATTER_DEFAULT_PRODUCT_ID;
        s_matter_info.discriminator          = MATTER_DEFAULT_DISCRIMINATOR;
        s_matter_info.setup_passcode         = MATTER_DEFAULT_PASSCODE;
        s_matter_info.commissioning_flow     = MATTER_COMMISSIONING_FLOW_STANDARD;
        s_matter_info.discovery_capabilities = (MATTER_DISCOVERY_CAP_BLE | MATTER_DISCOVERY_CAP_ONNETWORK);
    }

    s_matter_telem.state                    = MATTER_COMMISSIONING_STATE_READY;
    s_matter_telem.transport                = MATTER_TRANSPORT_THREAD;
    s_matter_telem.fabric_count             = 0U;
    s_matter_telem.node_id                  = 0ULL;
    s_matter_telem.fabric_index             = 0U;
    s_matter_telem.onoff_state              = false;
    s_matter_telem.failsafe_armed           = false;
    s_matter_telem.failsafe_remaining_sec   = 0U;
    s_matter_telem.ble_reclaimed            = false;
    s_matter_telem.total_commands_processed = 0U;
    s_matter_telem.total_attribute_reads    = 0U;
    s_matter_telem.total_attribute_writes   = 0U;

    matter_crypto_hw_init();

    return MATTER_OK;
}

matter_status_t matter_reset(void)
{
    return matter_init(NULL);
}

matter_status_t matter_get_commissioning_info(matter_commissioning_info_t *out_info)
{
    if (out_info == NULL)
    {
        return MATTER_ERR_INVALID_PARAM;
    }
    *out_info = s_matter_info;
    return MATTER_OK;
}

matter_status_t matter_set_commissioning_info(const matter_commissioning_info_t *info)
{
    if (info == NULL)
    {
        return MATTER_ERR_INVALID_PARAM;
    }
    s_matter_info = *info;
    return MATTER_OK;
}

matter_status_t matter_get_telemetry(matter_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return MATTER_ERR_INVALID_PARAM;
    }
    *out_telem = s_matter_telem;
    return MATTER_OK;
}

matter_commissioning_state_t matter_get_state(void)
{
    return s_matter_telem.state;
}

matter_status_t matter_arm_failsafe(uint16_t expiry_sec)
{
    s_matter_telem.failsafe_armed = true;
    s_matter_telem.failsafe_remaining_sec = (expiry_sec > 0U) ? expiry_sec : MATTER_DEFAULT_FAILSAFE_SEC;
    s_matter_telem.state = MATTER_COMMISSIONING_STATE_ARMED_FAILSAFE;
    return MATTER_OK;
}

matter_status_t matter_complete_commissioning(uint64_t node_id, uint8_t fabric_index)
{
    s_matter_telem.state = MATTER_COMMISSIONING_STATE_COMMISSIONED;
    s_matter_telem.fabric_count = 1U;
    s_matter_telem.node_id = (node_id > 0ULL) ? node_id : MATTER_DEFAULT_NODE_ID;
    s_matter_telem.fabric_index = (fabric_index > 0U) ? fabric_index : 1U;
    s_matter_telem.failsafe_armed = false;
    s_matter_telem.failsafe_remaining_sec = 0U;
    s_matter_telem.ble_reclaimed = true;
    return MATTER_OK;
}

matter_status_t matter_set_transport(matter_transport_mode_t transport)
{
    s_matter_telem.transport = transport;
    return MATTER_OK;
}

matter_transport_mode_t matter_get_transport(void)
{
    return s_matter_telem.transport;
}

matter_status_t matter_set_onoff(bool on)
{
    s_matter_telem.onoff_state = on;
    s_matter_telem.total_attribute_writes++;
    return MATTER_OK;
}

bool matter_get_onoff(void)
{
    s_matter_telem.total_attribute_reads++;
    return s_matter_telem.onoff_state;
}

matter_status_t matter_toggle_onoff(void)
{
    s_matter_telem.onoff_state = !s_matter_telem.onoff_state;
    s_matter_telem.total_attribute_writes++;
    return MATTER_OK;
}

const char *matter_state_to_str(matter_commissioning_state_t state)
{
    switch (state)
    {
        case MATTER_COMMISSIONING_STATE_UNINITIALIZED:
            return "UNINITIALIZED";
        case MATTER_COMMISSIONING_STATE_READY:
            return "READY";
        case MATTER_COMMISSIONING_STATE_PASE_ENGAGED:
            return "PASE_ENGAGED";
        case MATTER_COMMISSIONING_STATE_ARMED_FAILSAFE:
            return "ARMED_FAILSAFE";
        case MATTER_COMMISSIONING_STATE_CONFIGURING_NETWORK:
            return "CONFIGURING_NETWORK";
        case MATTER_COMMISSIONING_STATE_OPERATIONAL_CREDENTIALS:
            return "OPERATIONAL_CREDENTIALS";
        case MATTER_COMMISSIONING_STATE_COMMISSIONED:
            return "COMMISSIONED";
        default:
            return "UNKNOWN";
    }
}

const char *matter_transport_to_str(matter_transport_mode_t transport)
{
    switch (transport)
    {
        case MATTER_TRANSPORT_THREAD:
            return "Matter-over-Thread (802.15.4)";
        case MATTER_TRANSPORT_WIFI:
            return "Matter-over-Wi-Fi (802.11ax)";
        default:
            return "UNKNOWN";
    }
}
