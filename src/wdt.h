#ifndef WDT_H
#define WDT_H

#include <stdint.h>

#define WDT_UNLOCK_KEY 0x50D83AA1U

typedef struct {
    uint32_t feed_interval_ms;
    uint32_t feed_count;
    uint32_t timg0_timeout_ticks;
    uint8_t  active;
} wdt_supervisor_t;

/* Initialize multi-tier watchdog supervisor (TIMG0 MWDT enabled, flashboot cleared, SWD quiescent) */
void wdt_init(uint32_t timeout_ms);

/* Reload active watchdog counter */
void wdt_feed(void);

/* Periodic supervisor tick to service watchdogs and maintain liveness */
void wdt_supervisor_tick(void);

/* Query supervisor telemetry */
void wdt_get_status(wdt_supervisor_t *status);

/* Query hardware reset cause register */
uint32_t wdt_get_reset_cause(void);

/* Get human-readable reset cause description */
const char *wdt_get_reset_cause_desc(uint32_t cause);

#endif // WDT_H
