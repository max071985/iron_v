/*
 * src/timer.h
 *
 * ESP32-C6 Hardware Periodic Timer Driver (TIMG0 Timer 0)
 * TRM Chapter 14 (Timer Group, §14.1-§14.6)
 *
 * Provides register-level TIMG0 Timer 0 configuration, prescaling,
 * auto-reload periodic alarm, INTMTX interrupt routing to CPU channel 6,
 * and asynchronous DPC notification dispatching.
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stddef.h>
#include "regs/timg0.h"

/* Hardware Clock & Prescaler Constants */
#define TIMER_XTAL_FREQ_HZ              40000000U
#define TIMER_PRESCALER_DIV             40U         /* 40 MHz XTAL / 40 = 1 MHz (1 us per tick) */
#define TIMER_TICKS_PER_SEC             1000000U    /* 1,000,000 ticks = 1 second */
#define TIMER_DEFAULT_INTERVAL_SEC      10U         /* 10-second periodic interrupt */
#define TIMER_DEFAULT_INTERVAL_TICKS    ((uint64_t)TIMER_DEFAULT_INTERVAL_SEC * TIMER_TICKS_PER_SEC)

/* PCR Timer Clock Selection (TRM §8.4) */
#define TIMER_PCR_CLK_SEL_XTAL          0U          /* 0 = XTAL clock source */

/* Interrupt Allocation Constants */
#define TIMER_CPU_INTR_CHANNEL          6U          /* CPU interrupt channel 6 */
#define TIMER_INTR_PRIORITY             8U          /* PLIC interrupt priority 8 */

/* Timer Telemetry Structure */
typedef struct {
    uint32_t          interval_sec;
    uint64_t          interval_ticks;
    volatile uint32_t isr_count;
    volatile uint32_t dpc_count;
    volatile uint8_t  active;
} timer_status_t;

/* Lifecycle & Configuration Primitives */
void timer_init(uint32_t interval_sec);
void timer_start(void);
void timer_stop(void);
void timer_isr(void *arg);
void timer_dpc_handler(uint32_t tick_count, uint32_t arg1);

/* Query & Status Telemetry */
uint32_t timer_get_tick_count(void);
void timer_get_status(timer_status_t *out_status);
uint64_t timer_get_current_ticks(void);

#endif /* TIMER_H */
