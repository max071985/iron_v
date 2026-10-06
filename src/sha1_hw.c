/*
 * Iron V - SHA-1 block compression on the ESP32-C6 SHA accelerator (REV-16)
 */
#include "sha1_hw.h"

#if defined(__riscv)
#include "regs/sha.h"
#include "regs/pcr.h"

static bool s_sha_hw_ready;

void sha1_hw_init(void)
{
    *PCR_SHA_CONF_REG |= PCR_SHA_CONF_SHA_CLK_EN_M;
    *PCR_SHA_CONF_REG |= PCR_SHA_CONF_SHA_RST_EN_M;
    *PCR_SHA_CONF_REG &= ~PCR_SHA_CONF_SHA_RST_EN_M;
    /* The digital-signature and HMAC peripherals share the SHA core; their reset holds it too
     * (ESP-IDF sha_ll_reset_register) */
    *PCR_DS_CONF_REG &= ~PCR_DS_CONF_DS_RST_EN_M;
    *PCR_HMAC_CONF_REG &= ~PCR_HMAC_CONF_HMAC_RST_EN_M;
    s_sha_hw_ready = true;
}

bool sha1_hw_compress(const sha1_state_t *in, const sha1_block_t *block, sha1_state_t *out)
{
    if (!s_sha_hw_ready)
    {
        sha1_hw_init();
    }
    volatile uint32_t *h_mem = SHA_H_MEM_REG;
    volatile uint32_t *m_mem = SHA_M_MEM_REG;

    *SHA_MODE_REG = SHA_HW_MODE_SHA1;
    if (in != NULL)
    {
        for (uint32_t i = 0U; i < SHA1_STATE_WORDS; i++)
        {
            h_mem[i] = in->w[i];
        }
    }
    for (uint32_t i = 0U; i < SHA1_BLOCK_WORDS; i++)
    {
        m_mem[i] = block->w[i];
    }
    if (in != NULL)
    {
        *SHA_CONTINUE_REG = SHA_HW_TRIGGER;
    }
    else
    {
        *SHA_START_REG = SHA_HW_TRIGGER;
    }

    uint32_t polls = SHA_HW_BUSY_POLL_MAX;
    while ((*SHA_BUSY_REG & SHA_BUSY_STATE_M) != 0U)
    {
        if (--polls == 0U)
        {
            return false;
        }
    }
    for (uint32_t i = 0U; i < SHA1_STATE_WORDS; i++)
    {
        out->w[i] = h_mem[i];
    }
    return true;
}
#endif
