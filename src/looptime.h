/*
 * Iron V - Main-loop latency telemetry and per-client time budgets (REV-16)
 *
 * The cooperative scheduler is only as responsive as its slowest main-loop client. Each client
 * call is timed (the caller passes timestamps, so the accounting is host-testable): per client the
 * longest call, total time, calls and calls over the client's budget; per iteration the longest
 * iteration, a latency histogram and the client that dominated the worst one. Interrupt-off
 * windows of flash operations are recorded here too (they stall every client and every ISR).
 * REV-17 uses these numbers to decide whether a preemptive kernel is needed.
 */
#ifndef IRON_V_LOOPTIME_H
#define IRON_V_LOOPTIME_H

#include <stdbool.h>
#include <stdint.h>

/* Main-loop clients, in loop order */
typedef enum {
    LOOP_CLIENT_DPC = 0,
    LOOP_CLIENT_TCP,
    LOOP_CLIENT_DHCP,
    LOOP_CLIENT_BUTTON,
    LOOP_CLIENT_PROV,
    LOOP_CLIENT_MDNS,
    LOOP_CLIENT_MQTT,                    /* light (LED, NVS settle) and the MQTT client (REV-23) */
    LOOP_CLIENT_NVS,
    LOOP_CLIENT_WIFI_POLL,               /* blob timers, PHY tracking, RX drain */
    LOOP_CLIENT_RX,
    LOOP_CLIENT_SHELL,
    LOOP_CLIENT_YIELD,                   /* the blob's Wi-Fi task and other coroutines */
    LOOP_CLIENT_COUNT
} loop_client_t;

/* Per-client budgets: a call longer than this counts as an overrun. 0 = no budget (operator
 * commands block by design). Joins call blocking blob APIs and run PBKDF2 once per network. */
#define LOOP_BUDGET_DEFAULT_US           2000U
#define LOOP_BUDGET_PROV_US              20000U
#define LOOP_BUDGET_NVS_US               100000U   /* a deferred commit: one sector erase + write */
#define LOOP_BUDGET_WIFI_POLL_US         5000U
#define LOOP_BUDGET_RX_US                5000U
#define LOOP_BUDGET_SHELL_US             0U
#define LOOP_BUDGET_YIELD_US             5000U
#define LOOP_ITER_BUDGET_US              20000U    /* whole iteration */

/* Iteration latency histogram: bucket i counts iterations shorter than its bound */
#define LOOP_HIST_BUCKETS                6U
#define LOOP_HIST_BOUND_0_US             100U
#define LOOP_HIST_BOUND_1_US             1000U
#define LOOP_HIST_BOUND_2_US             10000U
#define LOOP_HIST_BOUND_3_US             100000U
#define LOOP_HIST_BOUND_4_US             1000000U  /* last bucket: everything longer */

/* Overrun log line on the console at most this often */
#define LOOP_REPORT_INTERVAL_US          10000000ULL

/* Interrupt-off windows recorded by the flash routines */
typedef enum {
    LOOP_IRQOFF_FLASH_READ = 0,
    LOOP_IRQOFF_FLASH_WRITE,
    LOOP_IRQOFF_FLASH_ERASE,
    LOOP_IRQOFF_FLASH_UNLOCK,            /* status-register write before program/erase */
    LOOP_IRQOFF_COUNT
} loop_irqoff_t;

typedef struct {
    uint32_t max_us;
    uint32_t calls;
    uint32_t overruns;
    uint64_t total_us;
    uint64_t max_at_us;                  /* when the longest call ended */
} loop_client_stats_t;

typedef struct {
    uint32_t max_us;
    uint32_t count;
} loop_irqoff_stats_t;

typedef struct {
    loop_client_stats_t clients[LOOP_CLIENT_COUNT];
    loop_irqoff_stats_t irqoff[LOOP_IRQOFF_COUNT];
    uint32_t hist[LOOP_HIST_BUCKETS];
    uint64_t iterations;
    uint32_t iter_max_us;                /* longest iteration, all clients */
    uint32_t iter_max_no_shell_us;       /* longest iteration minus its shell time (operator commands) */
    uint8_t  iter_max_client;            /* client with the largest share of the longest iteration */
    uint32_t iter_max_client_us;
    uint64_t iter_max_at_us;             /* when the longest iteration ended */
    uint32_t iter_overruns;
    uint64_t since_us;                   /* start of the measurement window (boot or reset) */
} loop_stats_t;

/* Overrun summary for the rate-limited console line */
typedef struct {
    uint32_t overruns;                   /* client overruns since the last report */
    uint8_t  worst_client;
    uint32_t worst_us;
} loop_report_t;

void looptime_reset(uint64_t now_us);
void looptime_iter_begin(uint64_t now_us);
/* Ends the current client's call at now_us (the next client starts there) */
void looptime_mark(loop_client_t client, uint64_t now_us);
void looptime_iter_end(uint64_t now_us);
void looptime_irqoff_record(loop_irqoff_t kind, uint32_t duration_us);
const loop_stats_t *looptime_get(void);
uint32_t looptime_budget_us(loop_client_t client);
const char *looptime_client_name(loop_client_t client);
/* True at most once per LOOP_REPORT_INTERVAL_US and only after new overruns */
bool looptime_take_report(uint64_t now_us, loop_report_t *out);

/* Console output (no-ops on the host) */
void looptime_print(void);
void looptime_print_report(const loop_report_t *rep);

#endif /* IRON_V_LOOPTIME_H */
