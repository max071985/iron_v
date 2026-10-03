/*
 * src/soak.h
 *
 * 24/7 Stability Soak, Memory Leak & Anti-Starvation Engine
 * Espressif ESP32-C6 RISC-V SoC Bare-Metal Kernel Runtime
 *
 * Enforces zero dynamic heap growth, zero DPC queue drops, bounded coroutine
 * scheduling latencies, and continuous multi-protocol stability monitoring.
 */

#ifndef IRONV_SOAK_H
#define IRONV_SOAK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "arena.h"
#include "dpc.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return Status Codes */
typedef enum {
    SOAK_OK                    =  0,
    SOAK_ERR_INVALID_PARAM     = -1,
    SOAK_ERR_MEMORY_LEAK       = -2,
    SOAK_ERR_DPC_DROPPED       = -3,
    SOAK_ERR_TASK_STARVED      = -4,
    SOAK_ERR_TIMEOUT           = -5,
    SOAK_ERR_CORRUPT           = -6
} soak_status_t;

/* Tuning & Threshold Constants (Zero Magic Numbers) */
#define SOAK_SCHED_LATENCY_THRESHOLD_US   (1000U)
#define SOAK_TEST_SMALL_BLOCKS            (8U)
#define SOAK_TEST_MED_BLOCKS              (4U)
#define SOAK_TEST_SCRATCH_SIZE            (512U)
#define SOAK_DEFAULT_CYCLES               (5U)
#define SOAK_DEFAULT_DWELL_MS             (50U)

/* Static Arena Memory Audit Snapshot */
typedef struct {
    uint32_t small_pool_active;
    uint32_t small_pool_high;
    uint32_t small_pool_total;
    uint32_t med_pool_active;
    uint32_t med_pool_high;
    uint32_t med_pool_total;
    uint32_t scratch_bytes_used;
    uint32_t scratch_bytes_high;
    uint32_t scratch_bytes_total;
    bool     is_leak_free;
    uint32_t total_alloc_count;
    uint32_t total_free_count;
} soak_mem_audit_t;

/* Deferred Procedure Call (DPC) Anti-Starvation Audit */
typedef struct {
    uint32_t dpc_drop_count;
    uint32_t dpc_pending_count;
    uint32_t dpc_processed_count;
    bool     is_starvation_free;
} soak_dpc_audit_t;

/* Coroutine Task Scheduler Latency & Fairness Audit */
typedef struct {
    uint32_t active_tasks_count;
    uint32_t total_switches;
    uint64_t max_yield_latency_us;
    bool     fairness_preserved;
} soak_sched_audit_t;

/* 24/7 Stability Soak Subsystem Telemetry */
typedef struct {
    uint32_t target_cycles;
    uint32_t completed_cycles;
    uint32_t failed_cycles;
    uint32_t clean_streak;
    uint32_t total_assertions_run;
    uint32_t total_assertions_passed;
    uint32_t total_assertions_failed;
    uint64_t start_time_us;
    uint64_t elapsed_time_ms;
    uint32_t last_cycle_duration_ms;
    uint32_t wdt_feeds_count;
    uint32_t peak_small_active;
    uint32_t peak_med_active;
    uint32_t peak_scratch_bytes;
    bool     mem_leak_detected;
    bool     dpc_drop_detected;
    bool     starvation_detected;
    bool     is_running;
} soak_telemetry_t;

/* Core Subsystem Lifecycle & Audit APIs */
int  soak_init(void);
int  soak_audit_memory(soak_mem_audit_t *out_audit);
int  soak_audit_dpc(soak_dpc_audit_t *out_audit);
int  soak_audit_scheduler(soak_sched_audit_t *out_audit);
int  soak_run_stability_cycle(uint32_t cycle_index);
int  soak_get_telemetry(soak_telemetry_t *out_telem);
void soak_reset_telemetry(void);

/* Interactive Console / Diagnostic Visualizers (Flash XIP) */
void soak_print_status(void);
void soak_print_audit(void);

#if !defined(__riscv)
/* Freestanding Host Emulation Hooks */
void soak_mock_reset(void);
void soak_mock_set_dpc_drops(uint32_t drops);
void soak_mock_set_sched_latency(uint64_t latency_us);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IRONV_SOAK_H */
