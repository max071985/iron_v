/*
 * Iron V - Main-loop latency telemetry and per-client time budgets (REV-16)
 */
#include "looptime.h"
#include "string.h"
#include "section.h"

#if defined(__riscv)
#include "console.h"
#include "utils.h"
#include "systimer.h"
#endif

static loop_stats_t s_loop;
static uint64_t     s_iter_start_us;
static uint64_t     s_mark_us;
static bool         s_in_iter;
static uint32_t     s_iter_client_us[LOOP_CLIENT_COUNT];

static uint32_t     s_report_overruns;
static uint8_t      s_report_worst_client;
static uint32_t     s_report_worst_us;
static uint64_t     s_report_last_us;

static const uint32_t s_hist_bounds_us[LOOP_HIST_BUCKETS - 1U] = {
    LOOP_HIST_BOUND_0_US, LOOP_HIST_BOUND_1_US, LOOP_HIST_BOUND_2_US,
    LOOP_HIST_BOUND_3_US, LOOP_HIST_BOUND_4_US
};

static uint32_t clamp_us(uint64_t us)
{
    return (us > UINT32_MAX) ? UINT32_MAX : (uint32_t)us;
}

void looptime_reset(uint64_t now_us)
{
    memset(&s_loop, 0, sizeof(s_loop));
    memset(s_iter_client_us, 0, sizeof(s_iter_client_us));
    s_loop.since_us = now_us;
    s_in_iter = false;
    s_report_overruns = 0U;
    s_report_worst_us = 0U;
    s_report_worst_client = 0U;
    s_report_last_us = now_us;
}

void looptime_iter_begin(uint64_t now_us)
{
    s_iter_start_us = now_us;
    s_mark_us = now_us;
    s_in_iter = true;
    memset(s_iter_client_us, 0, sizeof(s_iter_client_us));
}

void looptime_mark(loop_client_t client, uint64_t now_us)
{
    if (!s_in_iter || (uint32_t)client >= (uint32_t)LOOP_CLIENT_COUNT)
    {
        return;
    }
    uint32_t dur = clamp_us((now_us >= s_mark_us) ? (now_us - s_mark_us) : 0U);
    s_mark_us = now_us;

    loop_client_stats_t *c = &s_loop.clients[client];
    c->calls++;
    c->total_us += dur;
    if (dur > c->max_us)
    {
        c->max_us = dur;
        c->max_at_us = now_us;
    }
    s_iter_client_us[client] += dur;

    uint32_t budget = looptime_budget_us(client);
    if (budget != 0U && dur > budget)
    {
        c->overruns++;
        s_report_overruns++;
        if (dur > s_report_worst_us)
        {
            s_report_worst_us = dur;
            s_report_worst_client = (uint8_t)client;
        }
    }
}

void looptime_iter_end(uint64_t now_us)
{
    if (!s_in_iter)
    {
        return;
    }
    s_in_iter = false;
    uint32_t dur = clamp_us((now_us >= s_iter_start_us) ? (now_us - s_iter_start_us) : 0U);

    s_loop.iterations++;
    uint32_t b = 0U;
    while (b < (LOOP_HIST_BUCKETS - 1U) && dur >= s_hist_bounds_us[b])
    {
        b++;
    }
    s_loop.hist[b]++;
    if (dur > LOOP_ITER_BUDGET_US)
    {
        s_loop.iter_overruns++;
    }

    uint32_t shell_us = s_iter_client_us[LOOP_CLIENT_SHELL];
    uint32_t no_shell = (dur > shell_us) ? (dur - shell_us) : 0U;
    if (no_shell > s_loop.iter_max_no_shell_us)
    {
        s_loop.iter_max_no_shell_us = no_shell;
    }
    if (dur > s_loop.iter_max_us)
    {
        s_loop.iter_max_us = dur;
        s_loop.iter_max_at_us = now_us;
        uint32_t top = 0U;
        for (uint32_t i = 1U; i < (uint32_t)LOOP_CLIENT_COUNT; i++)
        {
            if (s_iter_client_us[i] > s_iter_client_us[top])
            {
                top = i;
            }
        }
        s_loop.iter_max_client = (uint8_t)top;
        s_loop.iter_max_client_us = s_iter_client_us[top];
    }
}

/* Called by the IRAM_ATTR flash routines right after interrupts come back on */
IRAM_ATTR void looptime_irqoff_record(loop_irqoff_t kind, uint32_t duration_us)
{
    if ((uint32_t)kind >= (uint32_t)LOOP_IRQOFF_COUNT)
    {
        return;
    }
    s_loop.irqoff[kind].count++;
    if (duration_us > s_loop.irqoff[kind].max_us)
    {
        s_loop.irqoff[kind].max_us = duration_us;
    }
}

const loop_stats_t *looptime_get(void)
{
    return &s_loop;
}

uint32_t looptime_budget_us(loop_client_t client)
{
    switch (client)
    {
        case LOOP_CLIENT_PROV:      return LOOP_BUDGET_PROV_US;
        case LOOP_CLIENT_NVS:       return LOOP_BUDGET_NVS_US;
        case LOOP_CLIENT_WIFI_POLL: return LOOP_BUDGET_WIFI_POLL_US;
        case LOOP_CLIENT_RX:        return LOOP_BUDGET_RX_US;
        case LOOP_CLIENT_SHELL:     return LOOP_BUDGET_SHELL_US;
        case LOOP_CLIENT_YIELD:     return LOOP_BUDGET_YIELD_US;
        default:                    return LOOP_BUDGET_DEFAULT_US;
    }
}

const char *looptime_client_name(loop_client_t client)
{
    switch (client)
    {
        case LOOP_CLIENT_DPC:       return "dpc";
        case LOOP_CLIENT_TCP:       return "tcp";
        case LOOP_CLIENT_DHCP:      return "dhcp";
        case LOOP_CLIENT_BUTTON:    return "button";
        case LOOP_CLIENT_PROV:      return "prov";
        case LOOP_CLIENT_MDNS:      return "mdns";
        case LOOP_CLIENT_NVS:       return "nvs";
        case LOOP_CLIENT_WIFI_POLL: return "wifi-poll";
        case LOOP_CLIENT_RX:        return "rx";
        case LOOP_CLIENT_SHELL:     return "shell";
        case LOOP_CLIENT_YIELD:     return "yield";
        default:                    return "?";
    }
}

bool looptime_take_report(uint64_t now_us, loop_report_t *out)
{
    if (out == NULL || s_report_overruns == 0U ||
        (now_us - s_report_last_us) < LOOP_REPORT_INTERVAL_US)
    {
        return false;
    }
    out->overruns = s_report_overruns;
    out->worst_client = s_report_worst_client;
    out->worst_us = s_report_worst_us;
    s_report_overruns = 0U;
    s_report_worst_us = 0U;
    s_report_last_us = now_us;
    return true;
}

#if defined(__riscv)
#define LOOP_LINE_LEN 112U

void looptime_print(void)
{
    char line[LOOP_LINE_LEN];
    const loop_stats_t *s = &s_loop;

    console_puts("\r\n=== MAIN LOOP LATENCY (REV-16) ===\r\n");
    snprintf(line, sizeof(line), "Iterations: %u  Max: %u us (%s %u us)  Max without shell: %u us\r\n",
             (unsigned)s->iterations, (unsigned)s->iter_max_us,
             looptime_client_name((loop_client_t)s->iter_max_client), (unsigned)s->iter_max_client_us,
             (unsigned)s->iter_max_no_shell_us);
    console_puts(line);
    snprintf(line, sizeof(line), "Iterations over %u us: %u\r\n",
             (unsigned)LOOP_ITER_BUDGET_US, (unsigned)s->iter_overruns);
    console_puts(line);
    snprintf(line, sizeof(line), "Histogram: <0.1ms %u  <1ms %u  <10ms %u  <100ms %u  <1s %u  >=1s %u\r\n",
             (unsigned)s->hist[0], (unsigned)s->hist[1], (unsigned)s->hist[2],
             (unsigned)s->hist[3], (unsigned)s->hist[4], (unsigned)s->hist[5]);
    console_puts(line);
    snprintf(line, sizeof(line), "Window: %u s\r\n", (unsigned)((systimer_get_us() - s->since_us) / US_PER_SECOND));
    console_puts(line);
    console_puts("Client      budget_us    max_us   avg_us  overruns  max_at_s\r\n");
    for (uint32_t i = 0U; i < (uint32_t)LOOP_CLIENT_COUNT; i++)
    {
        const loop_client_stats_t *c = &s->clients[i];
        uint32_t avg = (c->calls != 0U) ? (uint32_t)(c->total_us / c->calls) : 0U;
        snprintf(line, sizeof(line), "%-10s %10u %9u %8u %9u %9u\r\n",
                 looptime_client_name((loop_client_t)i), (unsigned)looptime_budget_us((loop_client_t)i),
                 (unsigned)c->max_us, (unsigned)avg, (unsigned)c->overruns,
                 (unsigned)(c->max_at_us / US_PER_SECOND));
        console_puts(line);
    }
    snprintf(line, sizeof(line), "Flash IRQ-off max: read %u us (%u)  write %u us (%u)  erase %u us (%u)\r\n",
             (unsigned)s->irqoff[LOOP_IRQOFF_FLASH_READ].max_us, (unsigned)s->irqoff[LOOP_IRQOFF_FLASH_READ].count,
             (unsigned)s->irqoff[LOOP_IRQOFF_FLASH_WRITE].max_us, (unsigned)s->irqoff[LOOP_IRQOFF_FLASH_WRITE].count,
             (unsigned)s->irqoff[LOOP_IRQOFF_FLASH_ERASE].max_us, (unsigned)s->irqoff[LOOP_IRQOFF_FLASH_ERASE].count);
    console_puts(line);
}

void looptime_print_report(const loop_report_t *rep)
{
    char line[LOOP_LINE_LEN];
    snprintf(line, sizeof(line), "[LOOP] %u budget overruns; worst %s %u us (budget %u us)\r\n",
             (unsigned)rep->overruns, looptime_client_name((loop_client_t)rep->worst_client),
             (unsigned)rep->worst_us, (unsigned)looptime_budget_us((loop_client_t)rep->worst_client));
    console_puts(line);
}
#else
void looptime_print(void) {}
void looptime_print_report(const loop_report_t *rep) { (void)rep; }
#endif
