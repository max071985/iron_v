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

    /* 1. Disable TIMG1 watchdog to prevent conflicting resets */
    *TIMG1_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();
    *TIMG1_WDTCONFIG0 = 0;
    FENCE();
    *TIMG1_WDTWPROTECT = 0;
    FENCE();

    /* 2. Configure RTC / LP Watchdog in quiescent state */
    *RTC_WDT_WPROTECT_REG = TIMG_WDT_WKEY;
    FENCE();
    *RTC_WDT_CONFIG0_REG = 0;
    FENCE();
    *RTC_WDT_WPROTECT_REG = 0;
    FENCE();

    /* 3. Configure TIMG0 Main Watchdog (MWDT) */
    /* Unlock TIMG0 WDT register access */
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();

    /* Set clock prescaler to 80 (80 MHz APB / 80 = 1 MHz tick rate = 1 us/tick) */
    *TIMG0_WDTCONFIG1_REG = (80U << TIMG0_WDTCONFIG1_WDT_CLK_PRESCALE_S);
    FENCE();

    /* Calculate timeout ticks: timeout_ms * 1000 */
    uint32_t timeout_ticks = timeout_ms * 1000U;
    g_wdt_supervisor.timg0_timeout_ticks = timeout_ticks;

    /* Set Stage 0 timeout hold cycles */
    *TIMG0_WDTCONFIG2_REG = timeout_ticks;
    FENCE();

    /* Configure Stage 0 action: System Reset (action = 3)
     * Bits 30:29 WDT_STG0: 1=interrupt, 2=reset CPU, 3=reset system
     * Enable PROCPU reset: bit 13
     * Enable Watchdog: bit 31
     */
    uint32_t wdt_cfg = TIMG0_WDTCONFIG0_WDT_EN_M |
                       (3U << TIMG0_WDTCONFIG0_WDT_STG0_S) |
                       TIMG0_WDTCONFIG0_WDT_PROCPU_RESET_EN_M |
                       (3U << TIMG0_WDTCONFIG0_WDT_SYS_RESET_LENGTH_S);
    *TIMG0_WDTCONFIG0 = wdt_cfg;
    FENCE();

    /* Reload/Feed counter */
    *TIMG0_WDTFEED = 1;
    FENCE();

    /* Relock TIMG0 WDT */
    *TIMG0_WDTWPROTECT = 0;
    FENCE();

    g_wdt_supervisor.feed_interval_ms = timeout_ms;
    g_wdt_supervisor.feed_count = 1;
    g_wdt_supervisor.active = 1;

    uart_puts("[WDT] Active supervisor armed (TIMG0 MWDT 5000ms timeout).\r\n");
}

void wdt_feed(void)
{
    if (!g_wdt_supervisor.active) return;

    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();
    *TIMG0_WDTFEED = 1;
    FENCE();
    *TIMG0_WDTWPROTECT = 0;
    FENCE();

    g_wdt_supervisor.feed_count++;
}

void wdt_supervisor_tick(void)
{
    wdt_feed();
}

void wdt_get_status(wdt_supervisor_t *status)
{
    if (!status) return;
    *status = g_wdt_supervisor;
}
