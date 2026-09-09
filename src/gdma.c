/*
 * src/gdma.c
 *
 * ESP32-C6 GDMA Multi-Channel Engine & Circular Buffer Descriptor Rings Driver
 * TRM Chapter 4 (GDMA Controller, §4.1-§4.8)
 *
 * Implements multi-channel DMA configuration, circular descriptor chain creation,
 * Inlink/Outlink lifecycle controls, ownership handshakes, and status telemetry.
 */

#include "gdma.h"

/* ========================================================================= */
/* Internal Driver State & Emulation Support                                 */
/* ========================================================================= */
static bool s_gdma_initialized = false;

#if defined(__riscv)

static inline void gdma_fence(void)
{
    asm volatile("fence rw, rw" ::: "memory");
}

#else

/* Host Emulation Storage */
static uint32_t s_mock_pcr_gdma_conf = 0U;
static uint32_t s_mock_misc_conf = 0U;
static uint32_t s_mock_date = GDMA_HARDWARE_DATE_EXPECTED;

static uint32_t s_mock_in_conf0[GDMA_CHANNEL_COUNT];
static uint32_t s_mock_in_link[GDMA_CHANNEL_COUNT];
static uint32_t s_mock_in_state[GDMA_CHANNEL_COUNT];
static uint32_t s_mock_in_dscr[GDMA_CHANNEL_COUNT];
static uint32_t s_mock_in_int_raw[GDMA_CHANNEL_COUNT];

static uint32_t s_mock_out_conf0[GDMA_CHANNEL_COUNT];
static uint32_t s_mock_out_link[GDMA_CHANNEL_COUNT];
static uint32_t s_mock_out_state[GDMA_CHANNEL_COUNT];
static uint32_t s_mock_out_dscr[GDMA_CHANNEL_COUNT];
static uint32_t s_mock_out_int_raw[GDMA_CHANNEL_COUNT];

static inline void gdma_fence(void)
{
    __sync_synchronize();
}

#endif /* __riscv */

/* ========================================================================= */
/* Subsystem Initialization                                                  */
/* ========================================================================= */

gdma_status_t gdma_init(void)
{
#if defined(__riscv)
    /* 1. Enable PCR GDMA peripheral clock and release hardware reset */
    *PCR_GDMA_CONF_REG |= PCR_GDMA_CLK_EN_BIT;
    *PCR_GDMA_CONF_REG &= ~PCR_GDMA_RST_EN_BIT;
    gdma_fence();

    /* 2. Enable internal GDMA peripheral clock in MISC register */
    *GDMA_MISC_CONF_REG |= GDMA_MISC_CONF_CLK_EN_BIT;
    gdma_fence();

    /* 3. Initialize each DMA channel pair to clean default state */
    for (uint32_t ch = 0; ch < GDMA_CHANNEL_COUNT; ch++)
    {
        gdma_channel_init(ch);
    }
#else
    s_mock_pcr_gdma_conf = PCR_GDMA_CLK_EN_BIT;
    s_mock_misc_conf = GDMA_MISC_CONF_CLK_EN_BIT;
    s_mock_date = GDMA_HARDWARE_DATE_EXPECTED;

    for (uint32_t ch = 0; ch < GDMA_CHANNEL_COUNT; ch++)
    {
        s_mock_in_conf0[ch] = 0U;
        s_mock_in_link[ch] = 0U;
        s_mock_in_state[ch] = 0U;
        s_mock_in_dscr[ch] = 0U;
        s_mock_in_int_raw[ch] = 0U;

        s_mock_out_conf0[ch] = 0U;
        s_mock_out_link[ch] = 0U;
        s_mock_out_state[ch] = 0U;
        s_mock_out_dscr[ch] = 0U;
        s_mock_out_int_raw[ch] = 0U;
    }
#endif

    s_gdma_initialized = true;
    return GDMA_OK;
}

gdma_status_t gdma_channel_init(uint32_t channel)
{
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }

    /* Reset FIFO and state machines for this channel */
    gdma_channel_reset(channel);

#if defined(__riscv)
    /* Clear pending interrupt flags */
    *GDMA_IN_INT_CLR_REG(channel) = 0xFFFFFFFFU;
    *GDMA_OUT_INT_CLR_REG(channel) = 0xFFFFFFFFU;

    /* Disable all interrupts initially */
    *GDMA_IN_INT_ENA_REG(channel) = 0U;
    *GDMA_OUT_INT_ENA_REG(channel) = 0U;
    gdma_fence();
#else
    s_mock_in_int_raw[channel] = 0U;
    s_mock_out_int_raw[channel] = 0U;
#endif

    return GDMA_OK;
}

gdma_status_t gdma_channel_reset(uint32_t channel)
{
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }

#if defined(__riscv)
    /* Assert and release IN channel reset */
    *GDMA_IN_CONF0_REG(channel) |= GDMA_IN_CONF0_IN_RST_BIT;
    gdma_fence();
    *GDMA_IN_CONF0_REG(channel) &= ~GDMA_IN_CONF0_IN_RST_BIT;

    /* Assert and release OUT channel reset */
    *GDMA_OUT_CONF0_REG(channel) |= GDMA_OUT_CONF0_OUT_RST_BIT;
    gdma_fence();
    *GDMA_OUT_CONF0_REG(channel) &= ~GDMA_OUT_CONF0_OUT_RST_BIT;
    gdma_fence();
#else
    s_mock_in_conf0[channel] = 0U;
    s_mock_out_conf0[channel] = 0U;
    s_mock_in_link[channel] &= ~GDMA_IN_LINK_START_BIT;
    s_mock_out_link[channel] &= ~GDMA_OUT_LINK_START_BIT;
#endif

    return GDMA_OK;
}

/* ========================================================================= */
/* IN (Rx) Channel Descriptor Link Operations                                */
/* ========================================================================= */

gdma_status_t gdma_inlink_set(uint32_t channel, const dma_descriptor_t *desc)
{
    if (!s_gdma_initialized)
    {
        return GDMA_ERR_NOT_INITIALIZED;
    }
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }
    if (!desc)
    {
        return GDMA_ERR_INVALID_ARG;
    }
    if (((uintptr_t)desc & DMA_DESC_ALIGN_MASK) != 0U)
    {
        return GDMA_ERR_UNALIGNED;
    }

#if defined(__riscv)
    /* Verify descriptor resides within valid DRAM window */
    if ((uintptr_t)desc < DMA_DRAM_START_ADDR || (uintptr_t)desc >= DMA_DRAM_END_ADDR)
    {
        return GDMA_ERR_INVALID_ARG;
    }

    uint32_t val = *GDMA_IN_LINK_REG(channel);
    val &= ~GDMA_IN_LINK_ADDR_MASK;
    val |= ((uint32_t)(uintptr_t)desc & GDMA_IN_LINK_ADDR_MASK);
    *GDMA_IN_LINK_REG(channel) = val;
    gdma_fence();
#else
    uint32_t val = s_mock_in_link[channel];
    val &= ~GDMA_IN_LINK_ADDR_MASK;
    val |= ((uint32_t)(uintptr_t)desc & GDMA_IN_LINK_ADDR_MASK);
    s_mock_in_link[channel] = val;
    s_mock_in_dscr[channel] = (uint32_t)(uintptr_t)desc;
#endif

    return GDMA_OK;
}

gdma_status_t gdma_inlink_start(uint32_t channel)
{
    if (!s_gdma_initialized)
    {
        return GDMA_ERR_NOT_INITIALIZED;
    }
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }

#if defined(__riscv)
    *GDMA_IN_LINK_REG(channel) |= GDMA_IN_LINK_START_BIT;
    gdma_fence();
#else
    s_mock_in_link[channel] |= GDMA_IN_LINK_START_BIT;
    s_mock_in_link[channel] &= ~GDMA_IN_LINK_STOP_BIT;
#endif

    return GDMA_OK;
}

gdma_status_t gdma_inlink_stop(uint32_t channel)
{
    if (!s_gdma_initialized)
    {
        return GDMA_ERR_NOT_INITIALIZED;
    }
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }

#if defined(__riscv)
    *GDMA_IN_LINK_REG(channel) |= GDMA_IN_LINK_STOP_BIT;
    gdma_fence();
#else
    s_mock_in_link[channel] |= GDMA_IN_LINK_STOP_BIT;
    s_mock_in_link[channel] &= ~GDMA_IN_LINK_START_BIT;
#endif

    return GDMA_OK;
}

gdma_status_t gdma_inlink_restart(uint32_t channel)
{
    if (!s_gdma_initialized)
    {
        return GDMA_ERR_NOT_INITIALIZED;
    }
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }

#if defined(__riscv)
    *GDMA_IN_LINK_REG(channel) |= GDMA_IN_LINK_RESTART_BIT;
    gdma_fence();
#else
    s_mock_in_link[channel] |= GDMA_IN_LINK_RESTART_BIT;
#endif

    return GDMA_OK;
}

/* ========================================================================= */
/* OUT (Tx) Channel Descriptor Link Operations                               */
/* ========================================================================= */

gdma_status_t gdma_outlink_set(uint32_t channel, const dma_descriptor_t *desc)
{
    if (!s_gdma_initialized)
    {
        return GDMA_ERR_NOT_INITIALIZED;
    }
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }
    if (!desc)
    {
        return GDMA_ERR_INVALID_ARG;
    }
    if (((uintptr_t)desc & DMA_DESC_ALIGN_MASK) != 0U)
    {
        return GDMA_ERR_UNALIGNED;
    }

#if defined(__riscv)
    /* Verify descriptor resides within valid DRAM window */
    if ((uintptr_t)desc < DMA_DRAM_START_ADDR || (uintptr_t)desc >= DMA_DRAM_END_ADDR)
    {
        return GDMA_ERR_INVALID_ARG;
    }

    uint32_t val = *GDMA_OUT_LINK_REG(channel);
    val &= ~GDMA_OUT_LINK_ADDR_MASK;
    val |= ((uint32_t)(uintptr_t)desc & GDMA_OUT_LINK_ADDR_MASK);
    *GDMA_OUT_LINK_REG(channel) = val;
    gdma_fence();
#else
    uint32_t val = s_mock_out_link[channel];
    val &= ~GDMA_OUT_LINK_ADDR_MASK;
    val |= ((uint32_t)(uintptr_t)desc & GDMA_OUT_LINK_ADDR_MASK);
    s_mock_out_link[channel] = val;
    s_mock_out_dscr[channel] = (uint32_t)(uintptr_t)desc;
#endif

    return GDMA_OK;
}

gdma_status_t gdma_outlink_start(uint32_t channel)
{
    if (!s_gdma_initialized)
    {
        return GDMA_ERR_NOT_INITIALIZED;
    }
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }

#if defined(__riscv)
    *GDMA_OUT_LINK_REG(channel) |= GDMA_OUT_LINK_START_BIT;
    gdma_fence();
#else
    s_mock_out_link[channel] |= GDMA_OUT_LINK_START_BIT;
    s_mock_out_link[channel] &= ~GDMA_OUT_LINK_STOP_BIT;
#endif

    return GDMA_OK;
}

gdma_status_t gdma_outlink_stop(uint32_t channel)
{
    if (!s_gdma_initialized)
    {
        return GDMA_ERR_NOT_INITIALIZED;
    }
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }

#if defined(__riscv)
    *GDMA_OUT_LINK_REG(channel) |= GDMA_OUT_LINK_STOP_BIT;
    gdma_fence();
#else
    s_mock_out_link[channel] |= GDMA_OUT_LINK_STOP_BIT;
    s_mock_out_link[channel] &= ~GDMA_OUT_LINK_START_BIT;
#endif

    return GDMA_OK;
}

gdma_status_t gdma_outlink_restart(uint32_t channel)
{
    if (!s_gdma_initialized)
    {
        return GDMA_ERR_NOT_INITIALIZED;
    }
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }

#if defined(__riscv)
    *GDMA_OUT_LINK_REG(channel) |= GDMA_OUT_LINK_RESTART_BIT;
    gdma_fence();
#else
    s_mock_out_link[channel] |= GDMA_OUT_LINK_RESTART_BIT;
#endif

    return GDMA_OK;
}

/* ========================================================================= */
/* Descriptor Management Helpers                                             */
/* ========================================================================= */

gdma_status_t gdma_desc_init(dma_descriptor_t *desc, void *buf, uint16_t size, uint16_t length, uint8_t owner)
{
    if (!desc)
    {
        return GDMA_ERR_INVALID_ARG;
    }
    if (((uintptr_t)desc & DMA_DESC_ALIGN_MASK) != 0U)
    {
        return GDMA_ERR_UNALIGNED;
    }
    if (size > DMA_DESC_MAX_SIZE || length > DMA_DESC_MAX_SIZE)
    {
        return GDMA_ERR_INVALID_ARG;
    }

    /* Zero dw0 first to ensure reserved24 and reserved29 are cleared */
    desc->dw0        = 0U;
    desc->size       = (uint32_t)size;
    desc->length     = (uint32_t)length;
    desc->err_eof    = 0U;
    desc->suc_eof    = 0U;
    desc->owner      = (owner != 0U) ? DMA_OWNER_DMA : DMA_OWNER_CPU;
    desc->buffer_addr      = (uint32_t)(uintptr_t)buf;
    desc->next_descriptor  = NULL;

    gdma_fence();
    return GDMA_OK;
}

gdma_status_t gdma_desc_link_circular(dma_descriptor_t *desc0, dma_descriptor_t *desc1)
{
    if (!desc0 || !desc1)
    {
        return GDMA_ERR_INVALID_ARG;
    }
    if ((((uintptr_t)desc0 & DMA_DESC_ALIGN_MASK) != 0U) ||
        (((uintptr_t)desc1 & DMA_DESC_ALIGN_MASK) != 0U))
    {
        return GDMA_ERR_UNALIGNED;
    }

    desc0->next_descriptor = desc1;
    desc1->next_descriptor = desc0;

    gdma_fence();
    return GDMA_OK;
}

/* ========================================================================= */
/* Status and Telemetry Queries                                              */
/* ========================================================================= */

uint32_t gdma_get_date_version(void)
{
#if defined(__riscv)
    return *GDMA_DATE_REG;
#else
    return s_mock_date;
#endif
}

gdma_status_t gdma_get_channel_telemetry(uint32_t channel, gdma_channel_telemetry_t *out_telem)
{
    if (channel >= GDMA_CHANNEL_COUNT)
    {
        return GDMA_ERR_OUT_OF_RANGE;
    }
    if (!out_telem)
    {
        return GDMA_ERR_INVALID_ARG;
    }

#if defined(__riscv)
    out_telem->in_state = *GDMA_IN_STATE_REG(channel);
    out_telem->in_dscr_addr = *GDMA_IN_DSCR_REG(channel);
    out_telem->out_state = *GDMA_OUT_STATE_REG(channel);
    out_telem->out_dscr_addr = *GDMA_OUT_DSCR_REG(channel);
    out_telem->in_int_raw = *GDMA_IN_INT_RAW_REG(channel);
    out_telem->out_int_raw = *GDMA_OUT_INT_RAW_REG(channel);
    out_telem->in_active = ((*GDMA_IN_LINK_REG(channel) & GDMA_IN_LINK_START_BIT) != 0U) ? 1U : 0U;
    out_telem->out_active = ((*GDMA_OUT_LINK_REG(channel) & GDMA_OUT_LINK_START_BIT) != 0U) ? 1U : 0U;
#else
    out_telem->in_state = s_mock_in_state[channel];
    out_telem->in_dscr_addr = s_mock_in_dscr[channel];
    out_telem->out_state = s_mock_out_state[channel];
    out_telem->out_dscr_addr = s_mock_out_dscr[channel];
    out_telem->in_int_raw = s_mock_in_int_raw[channel];
    out_telem->out_int_raw = s_mock_out_int_raw[channel];
    out_telem->in_active = ((s_mock_in_link[channel] & GDMA_IN_LINK_START_BIT) != 0U) ? 1U : 0U;
    out_telem->out_active = ((s_mock_out_link[channel] & GDMA_OUT_LINK_START_BIT) != 0U) ? 1U : 0U;
#endif

    return GDMA_OK;
}

gdma_status_t gdma_get_telemetry(gdma_telemetry_t *out_telem)
{
    if (!out_telem)
    {
        return GDMA_ERR_INVALID_ARG;
    }

    out_telem->date_version = gdma_get_date_version();

    for (uint32_t ch = 0; ch < GDMA_CHANNEL_COUNT; ch++)
    {
        gdma_get_channel_telemetry(ch, &out_telem->channels[ch]);
    }

    out_telem->ch0_in_active = out_telem->channels[GDMA_CHANNEL_0].in_active;
    out_telem->ch0_out_active = out_telem->channels[GDMA_CHANNEL_0].out_active;

    return GDMA_OK;
}
