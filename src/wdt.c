#include "wdt.h"
#include "io_constants.h"
#include "utils.h"

static wdt_supervisor_t g_wdt_supervisor = {
    .feed_interval_ms = 0,
    .feed_count = 0,
    .timg0_timeout_ticks = 0,
    .active = 0
};

void wdt_init(uint32_t timeout_ms)
{
    /* Default to 5000ms if 0 provided */
    if (timeout_ms == 0)
    {
        timeout_ms = 5000;
    }

    /* 1. Stop Super Watchdog (SWD) to prevent analog domain resets */
    *RTC_WDT_SWD_WPROTECT_REG = TIMG_WDT_WKEY;
    FENCE();
    *RTC_WDT_SWD_CONFIG_REG |= (1u << 30); // Disable SWD
    FENCE();
    *RTC_WDT_SWD_WPROTECT_REG = 0;
    FENCE();

    /* 2. Stop RTC Watchdog (RWDT) and clear its flash boot protection */
    *RTC_WDT_WPROTECT_REG = TIMG_WDT_WKEY;
    FENCE();
    *RTC_WDT_CONFIG0_REG = 0; // Clears RTC_WDT_EN and FLASHBOOT_MOD_EN
    FENCE();
    *RTC_WDT_WPROTECT_REG = 0;
    FENCE();

    /* 3. Stop TIMG1 watchdog to prevent conflicting resets */
    *TIMG1_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();
    *TIMG1_WDTCONFIG0 = TIMG1_WDTCONFIG0_WDT_CONF_UPDATE_EN_M;
    FENCE();
    *TIMG1_WDTWPROTECT = 0;
    FENCE();

    /* 4. Stop TIMG0 Flash Boot Mode and clear boot state */
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();
    *TIMG0_WDTCONFIG0 = TIMG0_WDTCONFIG0_WDT_CONF_UPDATE_EN_M;
    FENCE();

    /* 5. Configure TIMG0 WDT functional clock in PCR (PLL_F80M_CLK @ 80 MHz) */
    *PCR_TIMERGROUP0_WDT_CLK_CONF_REG = (1U << PCR_TIMERGROUP0_WDT_CLK_CONF_TG0_WDT_CLK_SEL_S) |
                                        PCR_TIMERGROUP0_WDT_CLK_CONF_TG0_WDT_CLK_EN_M;
    FENCE();

    /* 6. Configure TIMG0 MWDT Prescaler (80 MHz / 80 = 1 MHz tick rate = 1 us/tick) */
    *TIMG0_WDTCONFIG1_REG = (80U << TIMG0_WDTCONFIG1_WDT_CLK_PRESCALE_S) |
                            TIMG0_WDTCONFIG1_WDT_DIVCNT_RST_M;
    FENCE();

    /* 7. Configure Stage 0 timeout hold cycles */
    uint32_t timeout_ticks = timeout_ms * 1000U;
    g_wdt_supervisor.timg0_timeout_ticks = timeout_ticks;
    *TIMG0_WDTCONFIG2_REG = timeout_ticks;
    FENCE();

    /* 8. Configure Stage 0 action (System Reset = 3) and enable MWDT
     * Setting TIMG0_WDTCONFIG0_WDT_CONF_UPDATE_EN_M latches all WDTCONFIG registers.
     * TIMG0_WDTCONFIG0_WDT_FLASHBOOT_MOD_EN_M is 0 (flash boot mode terminated).
     */
    uint32_t wdt_cfg = TIMG0_WDTCONFIG0_WDT_EN_M |
                       (3U << TIMG0_WDTCONFIG0_WDT_STG0_S) |
                       TIMG0_WDTCONFIG0_WDT_PROCPU_RESET_EN_M |
                       (3U << TIMG0_WDTCONFIG0_WDT_SYS_RESET_LENGTH_S) |
                       TIMG0_WDTCONFIG0_WDT_CONF_UPDATE_EN_M;
    *TIMG0_WDTCONFIG0 = wdt_cfg;
    FENCE();

    /* 9. Initial reload/feed and leave write protection unlocked for reliable servicing */
    *TIMG0_WDTFEED = 1;
    FENCE();
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();

    g_wdt_supervisor.feed_interval_ms = timeout_ms;
    g_wdt_supervisor.feed_count = 1;
    g_wdt_supervisor.active = 1;

    uart_puts("[WDT] Active supervisor armed (TIMG0 MWDT 5000ms, SWD disabled, Flashboot cleared).\r\n");
}

void wdt_feed(void)
{
    if (!g_wdt_supervisor.active) return;

    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();
    *TIMG0_WDTFEED = 1;
    FENCE();

    g_wdt_supervisor.feed_count++;
}

void wdt_supervisor_tick(void)
{
    /* Rate-limit hardware MMIO feeds to prevent APB bus saturation (~1 ms interval) */
    static uint32_t s_poll_ticks = 0;
    if (++s_poll_ticks >= 50000U)
    {
        s_poll_ticks = 0;
        wdt_feed();
    }
}

void wdt_get_status(wdt_supervisor_t *status)
{
    if (!status) return;
    *status = g_wdt_supervisor;
}

uint32_t wdt_get_reset_cause(void)
{
    return *LP_CLKRST_RESET_CAUSE_REG & LP_CLKRST_RESET_CAUSE_RESET_CAUSE_M;
}

const char *wdt_get_reset_cause_desc(uint32_t cause)
{
    switch (cause)
    {
        case 0x01: return "Power-On / Chip Reset";
        case 0x03: return "Software System Reset";
        case 0x05: return "Deep-Sleep Wakeup Reset";
        case 0x07: return "MWDT0 Core Reset";
        case 0x08: return "MWDT1 Core Reset";
        case 0x09: return "RWDT Core Reset";
        case 0x0B: return "MWDT0 CPU Reset";
        case 0x0C: return "Software CPU Reset";
        case 0x0D: return "RWDT CPU Reset";
        case 0x0F: return "Brownout Reset";
        case 0x10: return "RWDT System Reset";
        case 0x11: return "MWDT1 CPU Reset";
        case 0x12: return "Super Watchdog (SWD) Reset";
        case 0x14: return "eFuse CRC Reset";
        case 0x15: return "USB Serial/UART Reset";
        case 0x16: return "USB JTAG Reset";
        default:   return "Unknown Reset Source";
    }
}
