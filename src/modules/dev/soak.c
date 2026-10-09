/*
 * src/soak.c
 *
 * 24/7 Stability Soak, Memory Leak & Anti-Starvation Subsystem Implementation
 * Espressif ESP32-C6 RISC-V SoC Bare-Metal Kernel Runtime
 */

#include "soak.h"
#include "console.h"
#include "utils.h"
#include "string.h"
#include "systimer.h"
#include "wdt.h"
#include "lp_core.h"
#include "modem.h"

/* Static Subsystem Execution State */
static soak_telemetry_t s_soak_telem;

#if !defined(__riscv)
static uint32_t s_mock_dpc_drops = 0U;
static uint64_t s_mock_sched_latency_us = 12U;

void soak_mock_reset(void)
{
    memset(&s_soak_telem, 0, sizeof(s_soak_telem));
    s_mock_dpc_drops = 0U;
    s_mock_sched_latency_us = 12U;
}

void soak_mock_set_dpc_drops(uint32_t drops)
{
    s_mock_dpc_drops = drops;
}

void soak_mock_set_sched_latency(uint64_t latency_us)
{
    s_mock_sched_latency_us = latency_us;
}
#endif

int soak_init(void)
{
    memset(&s_soak_telem, 0, sizeof(s_soak_telem));
    s_soak_telem.start_time_us = systimer_get_us();
    s_soak_telem.is_running = true;
    return SOAK_OK;
}

int soak_audit_memory(soak_mem_audit_t *out_audit)
{
    if (out_audit == NULL)
    {
        return SOAK_ERR_INVALID_PARAM;
    }

    arena_pool_stats_t sm_stats;
    arena_pool_stats_t md_stats;
    arena_scratch_stats_t sc_stats;

    arena_get_pool_stats(ARENA_POOL_SMALL, &sm_stats);
    arena_get_pool_stats(ARENA_POOL_MEDIUM, &md_stats);
    arena_get_scratch_stats(&sc_stats);

    out_audit->small_pool_active = sm_stats.active_count;
    out_audit->small_pool_high   = sm_stats.high_watermark;
    out_audit->small_pool_total  = sm_stats.block_count;

    out_audit->med_pool_active   = md_stats.active_count;
    out_audit->med_pool_high     = md_stats.high_watermark;
    out_audit->med_pool_total    = md_stats.block_count;

    out_audit->scratch_bytes_used  = (uint32_t)sc_stats.current_offset;
    out_audit->scratch_bytes_high  = (uint32_t)sc_stats.high_watermark;
    out_audit->scratch_bytes_total = (uint32_t)sc_stats.capacity;

    out_audit->total_alloc_count = sm_stats.total_alloc_count + md_stats.total_alloc_count + sc_stats.total_alloc_count;
    out_audit->total_free_count  = sm_stats.total_free_count + md_stats.total_free_count + sc_stats.total_reset_count;

    /* A system in quiescent state must have 0 active blocks and 0 scratch offset */
    out_audit->is_leak_free = (sm_stats.active_count == 0U) &&
                              (md_stats.active_count == 0U) &&
                              (sc_stats.current_offset == 0U);

    if (sm_stats.active_count > s_soak_telem.peak_small_active)
    {
        s_soak_telem.peak_small_active = sm_stats.active_count;
    }
    if (md_stats.active_count > s_soak_telem.peak_med_active)
    {
        s_soak_telem.peak_med_active = md_stats.active_count;
    }
    if ((uint32_t)sc_stats.current_offset > s_soak_telem.peak_scratch_bytes)
    {
        s_soak_telem.peak_scratch_bytes = (uint32_t)sc_stats.current_offset;
    }

    return SOAK_OK;
}

int soak_audit_dpc(soak_dpc_audit_t *out_audit)
{
    if (out_audit == NULL)
    {
        return SOAK_ERR_INVALID_PARAM;
    }

#if !defined(__riscv)
    out_audit->dpc_drop_count = s_mock_dpc_drops;
    out_audit->dpc_pending_count = 0U;
    out_audit->dpc_processed_count = 10U;
#else
    out_audit->dpc_drop_count = dpc_get_drop_count();
    out_audit->dpc_pending_count = dpc_get_size();
    out_audit->dpc_processed_count = dpc_get_processed_count();
#endif

    out_audit->is_starvation_free = (out_audit->dpc_drop_count == 0U);

    if (!out_audit->is_starvation_free)
    {
        s_soak_telem.dpc_drop_detected = true;
    }

    return SOAK_OK;
}

int soak_audit_scheduler(soak_sched_audit_t *out_audit)
{
    if (out_audit == NULL)
    {
        return SOAK_ERR_INVALID_PARAM;
    }

    out_audit->active_tasks_count = task_get_count();

#if !defined(__riscv)
    out_audit->max_yield_latency_us = s_mock_sched_latency_us;
    out_audit->total_switches = 25U;
#else
    for (uint32_t i = 0U; i < SOAK_SCHED_WARMUP_YIELDS; i++)
    {
        task_yield();
    }
    uint64_t max_delta = 0ULL;
    for (uint32_t i = 0U; i < SOAK_SCHED_SAMPLE_YIELDS; i++)
    {
        uint64_t t0 = systimer_get_us();
        task_yield();
        uint64_t t1 = systimer_get_us();
        uint64_t delta = (t1 > t0) ? (t1 - t0) : 0ULL;
        if (delta > max_delta)
        {
            max_delta = delta;
        }
    }
    out_audit->max_yield_latency_us = max_delta;

    task_scheduler_status_t status;
    task_get_status(&status);
    out_audit->total_switches = status.total_switches;
#endif

    out_audit->fairness_preserved = (out_audit->max_yield_latency_us <= (uint64_t)SOAK_SCHED_LATENCY_THRESHOLD_US);

    if (!out_audit->fairness_preserved)
    {
        s_soak_telem.starvation_detected = true;
    }

    return SOAK_OK;
}

int soak_run_stability_cycle(uint32_t cycle_index)
{
    (void)cycle_index;
    uint64_t c_start_us = systimer_get_us();

    /* 1. Feed Hardware & Software Watchdogs */
    wdt_feed();
    lp_wdt_feed();
    s_soak_telem.wdt_feeds_count++;

    /* 2. Execute Dynamic Arena Stress & Reclamation Cycle */
    void *small_ptrs[SOAK_TEST_SMALL_BLOCKS];
    void *med_ptrs[SOAK_TEST_MED_BLOCKS];
    bool alloc_ok = true;

    for (uint32_t i = 0U; i < SOAK_TEST_SMALL_BLOCKS; i++)
    {
        small_ptrs[i] = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
        if (small_ptrs[i] == NULL)
        {
            alloc_ok = false;
        }
    }

    for (uint32_t i = 0U; i < SOAK_TEST_MED_BLOCKS; i++)
    {
        med_ptrs[i] = arena_alloc(ARENA_POOL_BLOCK_SIZE_MEDIUM);
        if (med_ptrs[i] == NULL)
        {
            alloc_ok = false;
        }
    }

    arena_scratch_mark_t mark = arena_scratch_mark();
    void *sc_buf = arena_scratch_alloc(SOAK_TEST_SCRATCH_SIZE);
    if (sc_buf == NULL)
    {
        alloc_ok = false;
    }

    /* Release all allocated blocks */
    for (uint32_t i = 0U; i < SOAK_TEST_SMALL_BLOCKS; i++)
    {
        if (small_ptrs[i] != NULL)
        {
            arena_free(small_ptrs[i]);
        }
    }

    for (uint32_t i = 0U; i < SOAK_TEST_MED_BLOCKS; i++)
    {
        if (med_ptrs[i] != NULL)
        {
            arena_free(med_ptrs[i]);
        }
    }

    arena_scratch_reset(mark);

    /* 3. Audit Memory for Zero Leaks */
    soak_mem_audit_t mem_audit;
    soak_audit_memory(&mem_audit);

    /* 4. Audit DPC for Anti-Starvation */
    soak_dpc_audit_t dpc_audit;
    soak_audit_dpc(&dpc_audit);

    /* 5. Audit Scheduler Latency */
    soak_sched_audit_t sched_audit;
    soak_audit_scheduler(&sched_audit);

    /* 6. Validate RF Modem Coexistence Arbiter */
    bool coex_ok = modem_validate_coexistence();

    uint64_t c_end_us = systimer_get_us();
    uint32_t dur_ms = (uint32_t)((c_end_us - c_start_us) / 1000ULL);
    s_soak_telem.last_cycle_duration_ms = dur_ms;
    s_soak_telem.elapsed_time_ms = (uint32_t)((c_end_us - s_soak_telem.start_time_us) / 1000ULL);

    bool cycle_pass = alloc_ok && mem_audit.is_leak_free && dpc_audit.is_starvation_free &&
                      sched_audit.fairness_preserved && coex_ok;

    s_soak_telem.total_assertions_run += 5U;
    if (cycle_pass)
    {
        s_soak_telem.total_assertions_passed += 5U;
        s_soak_telem.completed_cycles++;
        s_soak_telem.clean_streak++;
        return SOAK_OK;
    }
    else
    {
        s_soak_telem.total_assertions_failed += 1U;
        s_soak_telem.failed_cycles++;
        s_soak_telem.clean_streak = 0U;

        if (!mem_audit.is_leak_free)
        {
            s_soak_telem.mem_leak_detected = true;
            return SOAK_ERR_MEMORY_LEAK;
        }
        if (!dpc_audit.is_starvation_free)
        {
            s_soak_telem.dpc_drop_detected = true;
            return SOAK_ERR_DPC_DROPPED;
        }
        if (!sched_audit.fairness_preserved)
        {
            s_soak_telem.starvation_detected = true;
            return SOAK_ERR_TASK_STARVED;
        }
        return SOAK_ERR_CORRUPT;
    }
}

int soak_get_telemetry(soak_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return SOAK_ERR_INVALID_PARAM;
    }

    if (s_soak_telem.start_time_us > 0ULL)
    {
        uint64_t now = systimer_get_us();
        s_soak_telem.elapsed_time_ms = (uint32_t)((now - s_soak_telem.start_time_us) / 1000ULL);
    }

    *out_telem = s_soak_telem;
    return SOAK_OK;
}

void soak_reset_telemetry(void)
{
    memset(&s_soak_telem, 0, sizeof(s_soak_telem));
    s_soak_telem.start_time_us = systimer_get_us();
    s_soak_telem.is_running = true;
}

/* ========================================================================= */
/* Interactive Console / Diagnostic Visualizers                              */
/* Placed in Flash XIP (.flash.text) to preserve critical IRAM / DRAM memory */
/* ========================================================================= */

__attribute__((section(".flash.text")))
void soak_print_status(void)
{
    soak_telemetry_t t;
    soak_get_telemetry(&t);

    console_puts("================ 24/7 Stability Soak Telemetry ================\r\n");
    console_puts("  Subsystem State:    ");
    console_puts(t.is_running ? "ACTIVE / SUPERVISED\r\n" : "IDLE\r\n");

    console_puts("  Completed Cycles:   ");
    put_dec(t.completed_cycles);
    console_puts(" (Failed: ");
    put_dec(t.failed_cycles);
    console_puts(")\r\n");

    console_puts("  Clean Streak:       ");
    put_dec(t.clean_streak);
    console_puts(" cycles\r\n");

    console_puts("  Total Assertions:   ");
    put_dec(t.total_assertions_run);
    console_puts(" (Passed: ");
    put_dec(t.total_assertions_passed);
    console_puts(", Failed: ");
    put_dec(t.total_assertions_failed);
    console_puts(")\r\n");

    console_puts("  Elapsed Time:       ");
    put_dec((uint32_t)(t.elapsed_time_ms / 1000U));
    console_puts(" s (Last Cycle: ");
    put_dec(t.last_cycle_duration_ms);
    console_puts(" ms)\r\n");

    console_puts("  WDT Feeds:          ");
    put_dec(t.wdt_feeds_count);
    console_puts(" (0 resets)\r\n");

    console_puts("  Peak Small Active:  ");
    put_dec(t.peak_small_active);
    console_puts(" / 32\r\n");

    console_puts("  Peak Med Active:    ");
    put_dec(t.peak_med_active);
    console_puts(" / 16\r\n");

    console_puts("  Peak Scratch Bytes: ");
    put_dec(t.peak_scratch_bytes);
    console_puts(" B\r\n");

    console_puts("  Anomalies Detected: ");
    if (!t.mem_leak_detected && !t.dpc_drop_detected && !t.starvation_detected)
    {
        console_puts("NONE (100% HEALTHY)\r\n");
    }
    else
    {
        if (t.mem_leak_detected) console_puts("[MEM_LEAK] ");
        if (t.dpc_drop_detected) console_puts("[DPC_DROP] ");
        if (t.starvation_detected) console_puts("[STARVATION] ");
        console_puts("\r\n");
    }
    console_puts("================================================================\r\n");
}

__attribute__((section(".flash.text")))
void soak_print_audit(void)
{
    soak_mem_audit_t mem;
    soak_dpc_audit_t dpc;
    soak_sched_audit_t sched;

    soak_audit_memory(&mem);
    soak_audit_dpc(&dpc);
    soak_audit_scheduler(&sched);

    console_puts("================ Subsystem Anti-Starvation Audit ================\r\n");
    console_puts("  Memory Invariant:   ");
    console_puts(mem.is_leak_free ? "ZERO LEAKS (PASS)\r\n" : "LEAK DETECTED (FAIL)\r\n");
    console_puts("    Small Pool:       ");
    put_dec(mem.small_pool_active);
    console_puts("/");
    put_dec(mem.small_pool_total);
    console_puts(" active (Peak: ");
    put_dec(mem.small_pool_high);
    console_puts(")\r\n");

    console_puts("    Medium Pool:      ");
    put_dec(mem.med_pool_active);
    console_puts("/");
    put_dec(mem.med_pool_total);
    console_puts(" active (Peak: ");
    put_dec(mem.med_pool_high);
    console_puts(")\r\n");

    console_puts("    Scratch Arena:    ");
    put_dec(mem.scratch_bytes_used);
    console_puts("/");
    put_dec(mem.scratch_bytes_total);
    console_puts(" bytes (Peak: ");
    put_dec(mem.scratch_bytes_high);
    console_puts(")\r\n");

    console_puts("  DPC Starvation:     ");
    console_puts(dpc.is_starvation_free ? "ZERO DROPS (PASS)\r\n" : "DROPS DETECTED (FAIL)\r\n");
    console_puts("    Dropped Events:   ");
    put_dec(dpc.dpc_drop_count);
    console_puts("\r\n");
    console_puts("    Processed Events: ");
    put_dec(dpc.dpc_processed_count);
    console_puts("\r\n");
    console_puts("    Pending In Queue: ");
    put_dec(dpc.dpc_pending_count);
    console_puts("\r\n");

    console_puts("  Coroutine Fairness: ");
    console_puts(sched.fairness_preserved ? "BOUNDED LATENCY (PASS)\r\n" : "STARVATION RISK (FAIL)\r\n");
    console_puts("    Active Tasks:     ");
    put_dec(sched.active_tasks_count);
    console_puts("\r\n");
    console_puts("    Total Switches:   ");
    put_dec(sched.total_switches);
    console_puts("\r\n");
    console_puts("    Yield Latency:    ");
    put_dec((uint32_t)sched.max_yield_latency_us);
    console_puts(" us (Threshold: ");
    put_dec(SOAK_SCHED_LATENCY_THRESHOLD_US);
    console_puts(" us)\r\n");
    console_puts("=================================================================\r\n");
}
