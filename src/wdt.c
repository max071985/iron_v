#include "wdt.h"
#include "io_constants.h"
#include "utils.h"

static wdt_supervisor_t g_wdt_supervisor = {
    .feed_interval_ms = 0,
    .feed_count = 0,
    .last_epoch_feeds = 0,
    .epoch_count = 0,
    .max_feeds_per_epoch = WDT_MAX_FEEDS_PER_EPOCH,
    .timg0_timeout_ticks = 0,
    .active = 0
};

static uint64_t g_wdt_epoch_start_ticks = 0;
static uint64_t g_wdt_last_feed_ticks = 0;

static inline uint64_t wdt_get_systimer_ticks(void)
{
    *SYSTIMER_UNIT0_OP_REG = SYSTIMER_UNIT0_OP_TIMER_UNIT0_UPDATE_M;
    FENCE();
    uint32_t lo = *SYSTIMER_UNIT0_VALUE_LO_REG;
    uint32_t hi = *SYSTIMER_UNIT0_VALUE_HI_REG & SYSTIMER_UNIT0_VALUE_HI_TIMER_UNIT0_VALUE_HI_M;
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

void wdt_init(uint32_t timeout_ms)
{
    /* Apply default timeout if 0 provided */
    if (timeout_ms == 0)
    {
        timeout_ms = WDT_DEFAULT_TIMEOUT_MS;
    }

    /* 1. Stop Super Watchdog (SWD) to prevent analog domain resets (TRM §15.3) */
    *RTC_WDT_SWD_WPROTECT_REG = TIMG_WDT_WKEY;
    FENCE();
    *RTC_WDT_SWD_CONFIG_REG |= LP_WDT_SWD_CONF_SWD_DISABLE_M;
    FENCE();
    *RTC_WDT_SWD_WPROTECT_REG = 0;
    FENCE();

    /* 2. Stop RTC Watchdog (RWDT) and clear its flash boot protection (TRM §15.2.2.4) */
    *RTC_WDT_WPROTECT_REG = TIMG_WDT_WKEY;
    FENCE();
    *RTC_WDT_CONFIG0_REG = 0;
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

    /* 4. Stop TIMG0 Flash Boot Mode and clear boot state (TRM §15.2.2.4) */
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();
    *TIMG0_WDTCONFIG0 = TIMG0_WDTCONFIG0_WDT_CONF_UPDATE_EN_M;
    FENCE();

    /* 5. Configure TIMG0 WDT functional clock in PCR (PLL_F80M_CLK @ 80 MHz, TRM §8.4) */
    *PCR_TIMERGROUP0_WDT_CLK_CONF_REG = (1U << PCR_TIMERGROUP0_WDT_CLK_CONF_TG0_WDT_CLK_SEL_S) |
                                        PCR_TIMERGROUP0_WDT_CLK_CONF_TG0_WDT_CLK_EN_M;
    FENCE();

    /* 6. Configure TIMG0 MWDT Prescaler (80 MHz / 80 = 1 MHz tick rate = 1 us/tick) */
    *TIMG0_WDTCONFIG1_REG = (WDT_PRESCALER_DIV << TIMG0_WDTCONFIG1_WDT_CLK_PRESCALE_S) |
                            TIMG0_WDTCONFIG1_WDT_DIVCNT_RST_M;
    FENCE();

    /* 7. Configure Stage 0 timeout hold cycles */
    uint32_t timeout_ticks = timeout_ms * WDT_TICKS_PER_MS;
    g_wdt_supervisor.timg0_timeout_ticks = timeout_ticks;
    *TIMG0_WDTCONFIG2_REG = timeout_ticks;
    FENCE();

    /* 8. Configure Stage 0 action and enable MWDT
     * Setting TIMG0_WDTCONFIG0_WDT_CONF_UPDATE_EN_M latches all WDTCONFIG registers.
     * TIMG0_WDTCONFIG0_WDT_FLASHBOOT_MOD_EN_M is 0 (flash boot mode terminated).
     */
    uint32_t wdt_cfg = TIMG0_WDTCONFIG0_WDT_EN_M |
                       (WDT_ACTION_RESET_SYSTEM << TIMG0_WDTCONFIG0_WDT_STG0_S) |
                       TIMG0_WDTCONFIG0_WDT_PROCPU_RESET_EN_M |
                       (WDT_RESET_LENGTH_CYCLES << TIMG0_WDTCONFIG0_WDT_SYS_RESET_LENGTH_S) |
                       TIMG0_WDTCONFIG0_WDT_CONF_UPDATE_EN_M;
    *TIMG0_WDTCONFIG0 = wdt_cfg;
    FENCE();

    /* 9. Initial reload/feed and leave write protection unlocked for reliable servicing */
    *TIMG0_WDTFEED = 1;
    FENCE();
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();

    uint64_t now = wdt_get_systimer_ticks();
    g_wdt_epoch_start_ticks = now;
    g_wdt_last_feed_ticks = now;

    g_wdt_supervisor.feed_interval_ms = timeout_ms;
    g_wdt_supervisor.feed_count = 1;
    g_wdt_supervisor.last_epoch_feeds = 0;
    g_wdt_supervisor.epoch_count = 0;
    g_wdt_supervisor.max_feeds_per_epoch = WDT_MAX_FEEDS_PER_EPOCH;
    g_wdt_supervisor.active = 1;

    uart_puts("[WDT] Active windowed epoch supervisor armed (TIMG0 MWDT, SYSTIMER epoch 1000ms).\r\n");
}

void wdt_feed(void)
{
    if (!g_wdt_supervisor.active) return;

    /* Enforce bounded feed limit per epoch (protects against runaway spin-loops) */
    if (g_wdt_supervisor.feed_count < g_wdt_supervisor.max_feeds_per_epoch)
    {
        *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
        FENCE();
        *TIMG0_WDTFEED = 1;
        FENCE();

        g_wdt_supervisor.feed_count++;
        g_wdt_last_feed_ticks = wdt_get_systimer_ticks();
    }
}

void wdt_supervisor_tick(void)
{
    if (!g_wdt_supervisor.active) return;

    uint64_t current_ticks = wdt_get_systimer_ticks();

    /* 1. Check if 1-second supervisory clock epoch has elapsed */
    if (current_ticks - g_wdt_epoch_start_ticks >= WDT_EPOCH_PERIOD_TICKS)
    {
        /* Check if minimum liveness feed requirement was satisfied during epoch */
        if (g_wdt_supervisor.feed_count >= WDT_MIN_FEEDS_PER_EPOCH)
        {
            /* Healthy epoch: refresh hardware watchdog */
            *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
            FENCE();
            *TIMG0_WDTFEED = 1;
            FENCE();
            g_wdt_last_feed_ticks = current_ticks;
        }

        /* Snapshot current epoch feeds and reset counter to zero for next epoch */
        g_wdt_supervisor.last_epoch_feeds = g_wdt_supervisor.feed_count;
        g_wdt_supervisor.feed_count = 0;
        g_wdt_supervisor.epoch_count++;
        g_wdt_epoch_start_ticks = current_ticks;
    }
    /* 2. Check periodic feed interval within active epoch */
    else if (current_ticks - g_wdt_last_feed_ticks >= WDT_FEED_INTERVAL_TICKS)
    {
        wdt_feed();
    }
}

void wdt_get_status(wdt_supervisor_t *status)
{
    if (!status) return;
    *status = g_wdt_supervisor;
}

soc_reset_cause_t wdt_get_reset_cause(void)
{
    return (soc_reset_cause_t)(*LP_CLKRST_RESET_CAUSE_REG & LP_CLKRST_RESET_CAUSE_RESET_CAUSE_M);
}

const char *wdt_get_reset_cause_desc(soc_reset_cause_t cause)
{
    switch (cause)
    {
        case RESET_CAUSE_CHIP_POWER_ON:   return "Power-On / Chip Reset";
        case RESET_CAUSE_SW_SYSTEM:       return "Software System Reset";
        case RESET_CAUSE_DEEP_SLEEP:      return "Deep-Sleep Wakeup Reset";
        case RESET_CAUSE_MWDT0_CORE:      return "MWDT0 Core Reset";
        case RESET_CAUSE_MWDT1_CORE:      return "MWDT1 Core Reset";
        case RESET_CAUSE_RWDT_CORE:       return "RWDT Core Reset";
        case RESET_CAUSE_MWDT0_CPU:       return "MWDT0 CPU Reset";
        case RESET_CAUSE_SW_CPU:          return "Software CPU Reset";
        case RESET_CAUSE_RWDT_CPU:        return "RWDT CPU Reset";
        case RESET_CAUSE_BROWNOUT:        return "Brownout Reset";
        case RESET_CAUSE_RWDT_SYSTEM:     return "RWDT System Reset";
        case RESET_CAUSE_MWDT1_CPU:       return "MWDT1 CPU Reset";
        case RESET_CAUSE_SUPER_WATCHDOG:  return "Super Watchdog (SWD) Reset";
        case RESET_CAUSE_EFUSE_CRC:       return "eFuse CRC Reset";
        case RESET_CAUSE_USB_UART:        return "USB Serial/UART Reset";
        case RESET_CAUSE_USB_JTAG:        return "USB JTAG Reset";
        default:                          return "Unknown Reset Source";
    }
}
