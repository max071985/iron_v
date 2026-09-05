#ifndef WDT_H
#define WDT_H

#include <stdint.h>
#include "regs/systimer.h"

/* Watchdog register unlock key (TRM §15.2.2.3) */
#define WDT_UNLOCK_KEY                  0x50D83AA1U

/* Watchdog timing and configuration defaults */
#define WDT_DEFAULT_TIMEOUT_MS          5000U
#define WDT_PRESCALER_DIV               80U     /* 80 MHz APB / 80 = 1 MHz (1 us tick) */
#define WDT_TICKS_PER_MS                1000U   /* 1000 ticks per ms at 1 MHz */
#define WDT_RESET_LENGTH_CYCLES         3U      /* Reset pulse duration: 32 clock cycles */

/* Hardware SYSTIMER clock baseline (16 MHz RC oscillator / XTAL) */
#define SYSTIMER_FREQ_HZ                16000000ULL
#define SYSTIMER_TICKS_PER_MS           16000ULL
#define SYSTIMER_TICKS_PER_SEC          16000000ULL

/* Watchdog supervision epoch and window boundaries */
#define WDT_EPOCH_PERIOD_MS             1000U   /* 1-second supervisory epoch */
#define WDT_EPOCH_PERIOD_TICKS          (WDT_EPOCH_PERIOD_MS * SYSTIMER_TICKS_PER_MS)

#define WDT_FEED_INTERVAL_MS            500U    /* Periodic feed interval within epoch (500 ms) */
#define WDT_FEED_INTERVAL_TICKS         (WDT_FEED_INTERVAL_MS * SYSTIMER_TICKS_PER_MS)

#define WDT_MIN_FEEDS_PER_EPOCH         1U      /* Minimum feeds required to maintain liveness */
#define WDT_MAX_FEEDS_PER_EPOCH         20U     /* Maximum allowed feeds per epoch (anti-spamming limit) */

/* Watchdog stage timeout actions (TRM Table 15.2-1) */
typedef enum {
    WDT_ACTION_NO_EFFECT    = 0,
    WDT_ACTION_INTERRUPT    = 1,
    WDT_ACTION_RESET_CPU    = 2,
    WDT_ACTION_RESET_SYSTEM = 3
} wdt_stage_action_t;

/* Silicon reset causes recorded in LP_CLKRST_RESET_CAUSE_REG (TRM Table 8.1-1) */
typedef enum {
    RESET_CAUSE_CHIP_POWER_ON   = 0x01,
    RESET_CAUSE_SW_SYSTEM       = 0x03,
    RESET_CAUSE_DEEP_SLEEP      = 0x05,
    RESET_CAUSE_MWDT0_CORE      = 0x07,
    RESET_CAUSE_MWDT1_CORE      = 0x08,
    RESET_CAUSE_RWDT_CORE       = 0x09,
    RESET_CAUSE_MWDT0_CPU       = 0x0B,
    RESET_CAUSE_SW_CPU          = 0x0C,
    RESET_CAUSE_RWDT_CPU        = 0x0D,
    RESET_CAUSE_BROWNOUT        = 0x0F,
    RESET_CAUSE_RWDT_SYSTEM     = 0x10,
    RESET_CAUSE_MWDT1_CPU       = 0x11,
    RESET_CAUSE_SUPER_WATCHDOG  = 0x12,
    RESET_CAUSE_EFUSE_CRC       = 0x14,
    RESET_CAUSE_USB_UART        = 0x15,
    RESET_CAUSE_USB_JTAG        = 0x16
} soc_reset_cause_t;

typedef struct {
    uint32_t feed_interval_ms;      /* Hardware timeout in ms (e.g. 5000 ms) */
    uint32_t feed_count;            /* Feeds recorded in active clock epoch (resets to 0 each epoch) */
    uint32_t last_epoch_feeds;       /* Snapshot of feeds completed in previous epoch */
    uint32_t epoch_count;           /* Total elapsed 1-second supervisory epochs (uptime seconds) */
    uint32_t max_feeds_per_epoch;   /* Maximum allowable feeds per epoch */
    uint32_t timg0_timeout_ticks;   /* Hardware timeout ticks */
    uint8_t  active;                /* Supervisor state flag */
    uint32_t total_feed_count;      /* Monotonic cumulative feeds across all epochs */
} wdt_supervisor_t;

/* Initialize multi-tier watchdog supervisor with windowed epoch tracking */
void wdt_init(uint32_t timeout_ms);

/* Reload active watchdog counter within current epoch budget */
void wdt_feed(void);

/* Periodic supervisor tick to service watchdogs, advance epochs, and maintain liveness */
void wdt_supervisor_tick(void);

/* Query supervisor telemetry */
void wdt_get_status(wdt_supervisor_t *status);

/* Query hardware reset cause register */
soc_reset_cause_t wdt_get_reset_cause(void);

/* Get human-readable reset cause description */
const char *wdt_get_reset_cause_desc(soc_reset_cause_t cause);

#endif // WDT_H
