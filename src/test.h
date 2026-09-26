#ifndef TEST_H
#define TEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Linker defined boundary symbols */
extern char _stext[];
extern char _etext[];
extern char _srodata[];
extern char _erodata[];
extern char _sdata[];
extern char _edata[];
extern char _sbss[];
extern char _ebss[];
extern char _stack_top[];

typedef enum {
    MEM_ACCESS_INVALID = 0,
    MEM_ACCESS_READONLY,
    MEM_ACCESS_READWRITE,
    MEM_ACCESS_MMIO
} mem_access_t;

#define TEST_SOAK_DEFAULT_CYCLES        5U
#define TEST_SOAK_DEFAULT_DELAY_MS      50U

typedef struct {
    uint32_t total_tests;
    uint32_t passed_tests;
    uint32_t failed_tests;
    uint32_t success_rate_pct;
} test_suite_result_t;

typedef struct {
    uint32_t target_cycles;
    uint32_t completed_cycles;
    uint32_t failed_cycles;
    uint32_t total_tests_run;
    uint32_t total_tests_passed;
    uint32_t total_tests_failed;
    uint32_t consecutive_clean_cycles;
    uint64_t start_time_us;
    uint64_t elapsed_time_ms;
    uint32_t last_cycle_duration_ms;
    uint32_t wdt_feeds_count;
    uint32_t peak_small_active;
    uint32_t peak_med_active;
    uint32_t peak_scratch_bytes;
    bool is_running;
} test_soak_telemetry_t;

/* Validates address accessibility and permission */
mem_access_t check_mem_access(uint32_t addr);

/* Executes full automated validation test suite (Tests 1 - 33) */
void run_validation_suite(void);

/* Executes full validation test suite and returns structured result */
void run_validation_suite_ex(test_suite_result_t *out_result);

/* Runs automated 24/7 stability soak benchmark for specified cycles (0 = continuous).
 * Verifies zero watchdog resets, zero dropped DPC events, and zero heap memory leaks.
 * Returns true if all cycles complete with 100% test pass. */
bool test_soak_run(uint32_t cycles, uint32_t delay_ms);

/* Queries soak test telemetry */
void test_soak_get_telemetry(test_soak_telemetry_t *out_telem);

#endif // TEST_H
