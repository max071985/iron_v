/*
 * src/systimer.c
 *
 * ESP32-C6 High-Resolution 64-Bit System Timer (SYSTIMER) Driver
 * TRM Chapter 13 (System Timer, §13.1-§13.7)
 */

#include "systimer.h"
#include "interrupt.h"
#include "io_constants.h"
#include "utils.h"

static systimer_alarm_cb_t   s_alarm_callback   = NULL;
static uint32_t              s_alarm_period_us  = 0U;
static uint8_t               s_alarm_mode       = (uint8_t)SYSTIMER_ALARM_MODE_TARGET;
static volatile uint8_t      s_alarm_active     = 0U;
static volatile uint32_t     s_alarm_count      = 0U;
static uint64_t              s_start_time_us    = 0ULL;

void systimer_init(void)
{
    /* 1. Configure PCR clock gating for SYSTIMER (TRM §8.4 & §13.3) */
    *PCR_SYSTIMER_CONF_REG |= PCR_SYSTIMER_CONF_SYSTIMER_CLK_EN_M;
    *PCR_SYSTIMER_CONF_REG &= ~PCR_SYSTIMER_CONF_SYSTIMER_RST_EN_M;
    FENCE();

    /* 2. Configure PCR functional clock: XTAL 40 MHz source, functional clock enabled */
    *PCR_SYSTIMER_FUNC_CLK_CONF_REG = (SYSTIMER_PCR_CLK_SRC_XTAL << PCR_SYSTIMER_FUNC_CLK_CONF_SYSTIMER_FUNC_CLK_SEL_S) |
                                      PCR_SYSTIMER_FUNC_CLK_CONF_SYSTIMER_FUNC_CLK_EN_M;
    FENCE();

    /* 3. Configure SYSTIMER peripheral registers (TRM §13.4.1)
     * - Enable register file clock gating (CLK_EN_M)
     * - Enable Unit 0 counter (TIMER_UNIT0_WORK_EN_M)
     * - Keep counting when CPU stalls (CORE0_STALL_EN cleared)
     */
    *SYSTIMER_CONF_REG |= (SYSTIMER_CONF_CLK_EN_M | SYSTIMER_CONF_TIMER_UNIT0_WORK_EN_M);
    *SYSTIMER_CONF_REG &= ~SYSTIMER_CONF_TIMER_UNIT0_CORE0_STALL_EN_M;
    FENCE();

    /* 4. Record boot baseline time */
    s_start_time_us = systimer_get_us();
}

uint64_t systimer_get_ticks(void)
{
    /* Atomic critical section: prevents concurrent interrupt latches from corrupting register read */
    uint32_t prev_mstatus = interrupt_global_save_and_disable();

    /* 1. Request hardware counter latch into VALUE_HI and VALUE_LO registers */
    *SYSTIMER_UNIT0_OP_REG = SYSTIMER_UNIT0_OP_TIMER_UNIT0_UPDATE_M;
    FENCE();

    /* 2. Poll synchronization flag until latched value is valid (non-blocking with cycle timeout) */
    uint32_t timeout = SYSTIMER_VALID_TIMEOUT_CYCLES;
    while (!(*SYSTIMER_UNIT0_OP_REG & SYSTIMER_UNIT0_OP_TIMER_UNIT0_VALUE_VALID_M))
    {
        if (--timeout == 0U)
        {
            break;
        }
    }

    /* 3. Read latched 52-bit counter value */
    uint32_t lo = *SYSTIMER_UNIT0_VALUE_LO_REG;
    uint32_t hi = *SYSTIMER_UNIT0_VALUE_HI_REG & SYSTIMER_UNIT0_VALUE_HI_TIMER_UNIT0_VALUE_HI_M;

    interrupt_global_restore(prev_mstatus);

    return (((uint64_t)hi) << 32) | (uint64_t)lo;
}

uint64_t systimer_get_us(void)
{
    return systimer_get_ticks() >> SYSTIMER_TICKS_TO_US_SHIFT;
}

uint64_t systimer_get_ms(void)
{
    return systimer_get_us() / US_PER_MS;
}

void systimer_delay_us(uint32_t us)
{
    uint64_t start_ticks = systimer_get_ticks();
    uint64_t wait_ticks = (uint64_t)us * SYSTIMER_TICKS_PER_US;
    while ((systimer_get_ticks() - start_ticks) < wait_ticks)
    {
        /* Busy wait with hardware monotonic timebase */
    }
}

void systimer_delay_ms(uint32_t ms)
{
    uint64_t start_ticks = systimer_get_ticks();
    uint64_t wait_ticks = (uint64_t)ms * SYSTIMER_TICKS_PER_MS;
    while ((systimer_get_ticks() - start_ticks) < wait_ticks)
    {
        /* Busy wait with hardware monotonic timebase */
    }
}

int systimer_alarm_init(uint32_t period_us, systimer_alarm_cb_t callback)
{
    if (period_us == 0U || period_us > SYSTIMER_MAX_PERIOD_US)
    {
        return -1;
    }

    uint32_t period_ticks = period_us * (uint32_t)SYSTIMER_TICKS_PER_US;
    if (period_ticks > SYSTIMER_MAX_PERIOD_TICKS)
    {
        period_ticks = SYSTIMER_MAX_PERIOD_TICKS;
    }

    s_alarm_callback   = callback;
    s_alarm_period_us  = period_us;
    s_alarm_mode       = (uint8_t)SYSTIMER_ALARM_MODE_PERIOD;

    /* 1. Ensure Target 0 compares against Unit 0 */
    *SYSTIMER_TARGET0_CONF_REG &= ~SYSTIMER_TARGET0_CONF_TARGET0_TIMER_UNIT_SEL_M;
    FENCE();

    /* 2. Configure period bitfield (bits 25:0) */
    *SYSTIMER_TARGET0_CONF_REG = (*SYSTIMER_TARGET0_CONF_REG & ~SYSTIMER_TARGET0_CONF_TARGET0_PERIOD_M) |
                                 (period_ticks & SYSTIMER_TARGET0_CONF_TARGET0_PERIOD_M);
    FENCE();

    /* 3. Synchronize period into COMP0 register */
    *SYSTIMER_COMP0_LOAD_REG = SYSTIMER_COMP0_LOAD_TIMER_COMP0_LOAD_M;
    FENCE();

    /* 4. Enable period mode (clear then set per TRM §13.5.3) */
    *SYSTIMER_TARGET0_CONF_REG &= ~SYSTIMER_TARGET0_CONF_TARGET0_PERIOD_MODE_M;
    FENCE();
    *SYSTIMER_TARGET0_CONF_REG |= SYSTIMER_TARGET0_CONF_TARGET0_PERIOD_MODE_M;
    FENCE();

    /* 5. Clear pending interrupt */
    *SYSTIMER_INT_CLR_REG = SYSTIMER_INT_CLR_TARGET0_INT_CLR_M;
    FENCE();

    /* 6. Route Target 0 interrupt (INT_SRC_SYSTIMER_TARGET0 = 57) to CPU channel 7 */
    interrupt_route(INT_SRC_SYSTIMER_TARGET0, SYSTIMER_CPU_INTR_CHANNEL);
    interrupt_set_priority(SYSTIMER_CPU_INTR_CHANNEL, SYSTIMER_INTR_PRIORITY);
    interrupt_set_type(SYSTIMER_CPU_INTR_CHANNEL, INTR_TYPE_LEVEL);
    interrupt_register_handler(SYSTIMER_CPU_INTR_CHANNEL, systimer_isr, NULL);
    interrupt_enable(SYSTIMER_CPU_INTR_CHANNEL);
    interrupt_global_enable();
    FENCE();

    /* 7. Enable peripheral interrupt gate in SYSTIMER */
    *SYSTIMER_INT_ENA_REG |= SYSTIMER_INT_ENA_TARGET0_INT_ENA_M;
    FENCE();

    /* 8. Activate Target 0 comparator in SYSTIMER_CONF_REG */
    *SYSTIMER_CONF_REG |= SYSTIMER_CONF_TARGET0_WORK_EN_M;
    FENCE();

    s_alarm_active = 1U;
    return 0;
}

int systimer_alarm_set_oneshot(uint64_t target_ticks, systimer_alarm_cb_t callback)
{
    if (target_ticks > SYSTIMER_MAX_COUNTER_TICKS)
    {
        return -1;
    }

    s_alarm_callback   = callback;
    s_alarm_period_us  = 0U;
    s_alarm_mode       = (uint8_t)SYSTIMER_ALARM_MODE_TARGET;

    /* 1. Ensure Target 0 compares against Unit 0 */
    *SYSTIMER_TARGET0_CONF_REG &= ~SYSTIMER_TARGET0_CONF_TARGET0_TIMER_UNIT_SEL_M;
    FENCE();

    /* 2. Configure target mode (clear period mode) */
    *SYSTIMER_TARGET0_CONF_REG &= ~SYSTIMER_TARGET0_CONF_TARGET0_PERIOD_MODE_M;
    FENCE();

    /* 3. Load target alarm value (lower 32 and upper 20 bits) */
    *SYSTIMER_TARGET0_LO_REG = (uint32_t)(target_ticks & SYSTIMER_TARGET0_LO_TIMER_TARGET0_LO_M);
    *SYSTIMER_TARGET0_HI_REG = (uint32_t)((target_ticks >> 32) & SYSTIMER_TARGET0_HI_TIMER_TARGET0_HI_M);
    FENCE();

    /* 4. Synchronize target value into COMP0 register */
    *SYSTIMER_COMP0_LOAD_REG = SYSTIMER_COMP0_LOAD_TIMER_COMP0_LOAD_M;
    FENCE();

    /* 5. Clear pending interrupt */
    *SYSTIMER_INT_CLR_REG = SYSTIMER_INT_CLR_TARGET0_INT_CLR_M;
    FENCE();

    /* 6. Route Target 0 interrupt to CPU channel 7 */
    interrupt_route(INT_SRC_SYSTIMER_TARGET0, SYSTIMER_CPU_INTR_CHANNEL);
    interrupt_set_priority(SYSTIMER_CPU_INTR_CHANNEL, SYSTIMER_INTR_PRIORITY);
    interrupt_set_type(SYSTIMER_CPU_INTR_CHANNEL, INTR_TYPE_LEVEL);
    interrupt_register_handler(SYSTIMER_CPU_INTR_CHANNEL, systimer_isr, NULL);
    interrupt_enable(SYSTIMER_CPU_INTR_CHANNEL);
    interrupt_global_enable();
    FENCE();

    /* 7. Enable peripheral interrupt gate in SYSTIMER */
    *SYSTIMER_INT_ENA_REG |= SYSTIMER_INT_ENA_TARGET0_INT_ENA_M;
    FENCE();

    /* 8. Activate Target 0 comparator */
    *SYSTIMER_CONF_REG |= SYSTIMER_CONF_TARGET0_WORK_EN_M;
    FENCE();

    s_alarm_active = 1U;
    return 0;
}

void systimer_alarm_cancel(void)
{
    interrupt_disable(SYSTIMER_CPU_INTR_CHANNEL);

    *SYSTIMER_INT_ENA_REG &= ~SYSTIMER_INT_ENA_TARGET0_INT_ENA_M;
    *SYSTIMER_CONF_REG &= ~SYSTIMER_CONF_TARGET0_WORK_EN_M;
    *SYSTIMER_INT_CLR_REG = SYSTIMER_INT_CLR_TARGET0_INT_CLR_M;
    FENCE();

    s_alarm_active = 0U;
    s_alarm_callback = NULL;
}

void systimer_isr(void *arg)
{
    (void)arg;

    /* 1. Clear peripheral interrupt latch */
    *SYSTIMER_INT_CLR_REG = SYSTIMER_INT_CLR_TARGET0_INT_CLR_M;
    FENCE();

    s_alarm_count++;

    /* 2. For one-shot alarm, auto-deactivate comparator */
    if (s_alarm_mode == (uint8_t)SYSTIMER_ALARM_MODE_TARGET)
    {
        *SYSTIMER_CONF_REG &= ~SYSTIMER_CONF_TARGET0_WORK_EN_M;
        *SYSTIMER_INT_ENA_REG &= ~SYSTIMER_INT_ENA_TARGET0_INT_ENA_M;
        s_alarm_active = 0U;
    }

    /* 3. Dispatch registered callback if present */
    if (s_alarm_callback)
    {
        s_alarm_callback();
    }
}

void systimer_get_telemetry(systimer_telemetry_t *out_tel)
{
    if (!out_tel)
    {
        return;
    }

    uint64_t ticks = systimer_get_ticks();
    uint64_t us = ticks >> SYSTIMER_TICKS_TO_US_SHIFT;

    out_tel->total_ticks          = ticks;
    out_tel->total_us             = us;
    out_tel->uptime_sec           = (uint32_t)(us / US_PER_SECOND);
    out_tel->uptime_ms_remainder  = (uint32_t)((us % US_PER_SECOND) / US_PER_MS);
    out_tel->uptime_us_remainder  = (uint32_t)(us % US_PER_MS);
    out_tel->alarm_count          = s_alarm_count;
    out_tel->alarm_active         = s_alarm_active;
    out_tel->alarm_mode           = s_alarm_mode;
    out_tel->alarm_period_us      = s_alarm_period_us;
}
