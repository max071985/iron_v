/*
 * src/systimer.h
 *
 * ESP32-C6 High-Resolution 64-Bit System Timer (SYSTIMER) Driver
 * TRM Chapter 13 (System Timer, §13.1-§13.7)
 *
 * Provides register-level SYSTIMER Unit 0 (16 MHz tick base clocked from XTAL/PLL)
 * configuration, atomic 64-bit latching, non-blocking time queries, microsecond/
 * millisecond conversion, Target 0/Comparator 0 alarm engine, and INTMTX routing.
 */

#ifndef IRON_V_SYSTIMER_DRIVER_H
#define IRON_V_SYSTIMER_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include "regs/systimer.h"
#include "regs/pcr.h"

/* Hardware Timebase Constants (TRM §13.2-§13.3) */
#define SYSTIMER_BASE_ADDR               0x6000A000U
#define SYSTIMER_FREQ_HZ                 16000000ULL
#define SYSTIMER_TICKS_PER_US            16ULL
#define SYSTIMER_TICKS_TO_US_SHIFT       4U
#define SYSTIMER_TICKS_PER_MS            16000ULL
#define SYSTIMER_TICKS_PER_SEC           16000000ULL

/* Time Unit Conversion Constants */
#define US_PER_SECOND                    1000000ULL
#define US_PER_MS                        1000ULL
#define MS_PER_SECOND                    1000U
#define SEC_PER_MINUTE                   60U
#define SEC_PER_HOUR                     3600U
#define SEC_PER_DAY                      86400U

/* Counter and Period Bitfield Limits (52-bit counter, 26-bit period) */
#define SYSTIMER_MAX_COUNTER_TICKS       0x000FFFFFFFFFFFFFULL
#define SYSTIMER_MAX_PERIOD_TICKS        0x03FFFFFFU
#define SYSTIMER_MAX_PERIOD_US           (SYSTIMER_MAX_PERIOD_TICKS / (uint32_t)SYSTIMER_TICKS_PER_US)

/* Timeout Cycles for Register Value Synchronization */
#define SYSTIMER_VALID_TIMEOUT_CYCLES    1000U

/* PCR Clock Configuration Constants (TRM §8.4 & §13.3) */
#define SYSTIMER_PCR_CLK_SRC_XTAL        0U
#define SYSTIMER_PCR_CLK_SRC_FOSC        1U

/* Interrupt Allocation Constants (External channel per TRM §1.6.2: 1-2, 5-6, 8-31) */
#define SYSTIMER_CPU_INTR_CHANNEL        8U
#define SYSTIMER_INTR_PRIORITY           9U

/* Status & Error Codes */
#define SYSTIMER_OK                      0
#define SYSTIMER_ERR_INVALID_PARAM       (-1)
#define SYSTIMER_ERR_OVERFLOW            (-2)

/* Parameterized Register Accessor Macros (AGENTS.md Compliance) */
#define SYSTIMER_UNIT_OP_REG(u)          ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x04U + ((uint32_t)(u) * 0x04U)))
#define SYSTIMER_UNIT_LOAD_HI_REG(u)     ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x0CU + ((uint32_t)(u) * 0x08U)))
#define SYSTIMER_UNIT_LOAD_LO_REG(u)     ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x10U + ((uint32_t)(u) * 0x08U)))
#define SYSTIMER_UNIT_VALUE_HI_REG(u)    ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x40U + ((uint32_t)(u) * 0x08U)))
#define SYSTIMER_UNIT_VALUE_LO_REG(u)    ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x44U + ((uint32_t)(u) * 0x08U)))
#define SYSTIMER_UNIT_LOAD_REG(u)        ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x5CU + ((uint32_t)(u) * 0x04U)))

#define SYSTIMER_TARGET_HI_REG(t)        ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x1CU + ((uint32_t)(t) * 0x08U)))
#define SYSTIMER_TARGET_LO_REG(t)        ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x20U + ((uint32_t)(t) * 0x08U)))
#define SYSTIMER_TARGET_CONF_REG(t)      ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x34U + ((uint32_t)(t) * 0x04U)))
#define SYSTIMER_COMP_LOAD_REG(t)        ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x50U + ((uint32_t)(t) * 0x04U)))
#define SYSTIMER_REAL_TARGET_LO_REG(t)   ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x74U + ((uint32_t)(t) * 0x08U)))
#define SYSTIMER_REAL_TARGET_HI_REG(t)   ((volatile uint32_t *)(uintptr_t)(SYSTIMER_BASE_ADDR + 0x78U + ((uint32_t)(t) * 0x08U)))

/* SYSTIMER Units and Targets */
typedef enum {
    SYSTIMER_UNIT_0 = 0,
    SYSTIMER_UNIT_1 = 1
} systimer_unit_t;

typedef enum {
    SYSTIMER_TARGET_0 = 0,
    SYSTIMER_TARGET_1 = 1,
    SYSTIMER_TARGET_2 = 2
} systimer_target_t;

/* SYSTIMER Alarm Modes */
typedef enum {
    SYSTIMER_ALARM_MODE_TARGET = 0,
    SYSTIMER_ALARM_MODE_PERIOD = 1
} systimer_alarm_mode_t;

/* Callback Type */
typedef void (*systimer_alarm_cb_t)(void);

/* Concrete SYSTIMER Configuration Structure (per Roadmap §3.2) */
typedef struct {
    uint64_t ticks_per_us;
    uint64_t start_time_us;
    uint32_t alarm_period_us;
    void (*alarm_callback)(void);
} systimer_config_t;

/* High-Resolution Telemetry Snapshot Structure */
typedef struct {
    uint64_t total_ticks;
    uint64_t total_us;
    uint32_t uptime_sec;
    uint32_t uptime_ms_remainder;
    uint32_t uptime_us_remainder;
    volatile uint32_t alarm_count;
    uint8_t  alarm_active;
    uint8_t  alarm_mode;
    uint32_t alarm_period_us;
} systimer_telemetry_t;

/* Core Lifecycle & Query APIs */
void     systimer_init(void);
uint64_t systimer_get_ticks(void);
uint64_t systimer_get_us(void);
uint64_t systimer_get_ms(void);

/* Precision Delay Utilities */
void     systimer_delay_us(uint32_t us);
void     systimer_delay_ms(uint32_t ms);

/* Alarm & Event Engine APIs (Target 0 / Comparator 0) */
int      systimer_alarm_init(uint32_t period_us, systimer_alarm_cb_t callback);
int      systimer_alarm_set_oneshot(uint64_t target_ticks, systimer_alarm_cb_t callback);
void     systimer_alarm_cancel(void);
void     systimer_isr(void *arg);

/* Telemetry Query */
void     systimer_get_telemetry(systimer_telemetry_t *out_tel);

#endif /* IRON_V_SYSTIMER_DRIVER_H */
