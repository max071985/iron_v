/*
 * Iron V - Hardware random numbers (REV-20). See hw_rng.h.
 */
#include "hw_rng.h"
#include "io_constants.h"
#include "section.h"
#include "string.h"
#include "systimer.h"
#include "regs/lp_peri.h"
#include "regs/lp_timer.h"

/* Read spacing in systimer ticks: HW_RNG_READ_WAIT_APB_CYCLES at the APB clock (64 us) */
#define HW_RNG_READ_WAIT_TICKS \
    (((uint64_t)HW_RNG_READ_WAIT_APB_CYCLES * SYSTIMER_FREQ_HZ) / (uint64_t)DEFAULT_APB_FREQ_HZ)

static uint64_t s_hw_rng_last_ticks = 0ULL;

void hw_rng_init(void)
{
    *LP_PERI_CLK_EN_REG |= LP_PERI_CLK_EN_RNG_CK_EN_M;
}

/* Low byte of the LP timer counter (IDF rtc_timer_hal_get_cycle_count(0) & 0xFF) */
static IRAM_ATTR uint32_t hw_rng_lp_timer_byte(void)
{
    *LP_TIMER_UPDATE_REG = LP_TIMER_UPDATE_MAIN_TIMER_UPDATE_M;
    return *LP_TIMER_MAIN_BUF0_LOW_REG & HW_RNG_LP_TIMER_BYTE_MASK;
}

IRAM_ATTR uint32_t hw_rng_u32(void)
{
    uint32_t result = 0U;
    uint64_t now = 0ULL;
    for (uint32_t i = 0U; i < sizeof(result); i++)
    {
        /* Keep folding in register reads until the spacing since the previous call has passed */
        do
        {
            now = systimer_get_ticks();
            result ^= *LP_PERI_RNG_DATA_REG;
        } while ((now - s_hw_rng_last_ticks) < HW_RNG_READ_WAIT_TICKS);
        result ^= hw_rng_lp_timer_byte() << (i * HW_RNG_BITS_PER_BYTE);
    }
    s_hw_rng_last_ticks = now;
    return result ^ *LP_PERI_RNG_DATA_REG;
}

IRAM_ATTR void hw_rng_fill(void *buf, size_t len)
{
    uint8_t *out = (uint8_t *)buf;
    while (len > 0U)
    {
        uint32_t word = hw_rng_u32();
        size_t n = (len < sizeof(word)) ? len : sizeof(word);
        memcpy(out, &word, n);
        out += n;
        len -= n;
    }
}
