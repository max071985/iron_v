/*
 * src/timer.c
 *
 * ESP32-C6 Hardware Periodic Timer Driver (TIMG0 Timer 0)
 * TRM Chapter 14 (Timer Group, §14.1-§14.6)
 */

#include "timer.h"
#include "io_constants.h"
#include "interrupt.h"
#include "dpc.h"
#include "console.h"
#include "utils.h"

static volatile timer_status_t g_timer_status = {
    .interval_sec = TIMER_DEFAULT_INTERVAL_SEC,
    .interval_ticks = TIMER_DEFAULT_INTERVAL_TICKS,
    .isr_count = 0U,
    .dpc_count = 0U,
    .active = 0U
};

static uint32_t s_timer_cfg_base = 0U;

void timer_init(uint32_t interval_sec)
{
    if (interval_sec == 0U)
    {
        interval_sec = TIMER_DEFAULT_INTERVAL_SEC;
    }

    /* 1. Ensure PCR clock is enabled for Timer Group 0 */
    *PCR_TIMERGROUP0_CONF_REG |= PCR_TIMERGROUP0_CONF_TG0_CLK_EN_M;
    *PCR_TIMERGROUP0_CONF_REG &= ~PCR_TIMERGROUP0_CONF_TG0_RST_EN_M;
    FENCE();

    /* 2. Select XTAL (40 MHz) clock source and enable timer clock in PCR */
    *PCR_TIMERGROUP0_TIMER_CLK_CONF_REG = (TIMER_PCR_CLK_SEL_XTAL << PCR_TIMERGROUP0_TIMER_CLK_CONF_TG0_TIMER_CLK_SEL_S) |
                                          PCR_TIMERGROUP0_TIMER_CLK_CONF_TG0_TIMER_CLK_EN_M;
    FENCE();

    /* 3. Ensure register clock and timer clock are active in TIMG0 */
    *TIMG0_REGCLK_REG |= (TIMG0_REGCLK_CLK_EN_M | TIMG0_REGCLK_TIMER_CLK_IS_ACTIVE_M);
    FENCE();

    /* 4. Disable timer counter before updating prescaler (TRM §14.3.1) */
    *TIMG0_T0CONFIG_REG = 0U;
    FENCE();

    /* 5. Configure 16-bit prescaler with divider counter reset: 40 MHz / 40 = 1 MHz */
    *TIMG0_T0CONFIG_REG = ((TIMER_PRESCALER_DIV << TIMG0_T0CONFIG_DIVIDER_S) & TIMG0_T0CONFIG_DIVIDER_M) |
                          TIMG0_T0CONFIG_DIVCNT_RST_M |
                          TIMG0_T0CONFIG_USE_XTAL_M;
    FENCE();

    /* 6. Reset timer counter to 0 */
    *TIMG0_T0LOADLO_REG = 0U;
    *TIMG0_T0LOADHI_REG = 0U;
    FENCE();
    *TIMG0_T0LOAD_REG = TIMG0_T0LOAD_LOAD_M;
    FENCE();

    /* 7. Configure alarm threshold value */
    uint64_t target_ticks = (uint64_t)interval_sec * TIMER_TICKS_PER_SEC;
    *TIMG0_T0ALARMLO_REG = (uint32_t)(target_ticks & TIMG0_T0ALARMLO_ALARM_LO_M);
    *TIMG0_T0ALARMHI_REG = (uint32_t)((target_ticks >> 32) & TIMG0_T0ALARMHI_ALARM_HI_M);
    FENCE();

    /* 8. Clear any pending interrupt and enable T0 interrupt in TIMG0 */
    *TIMG0_INT_CLR_TIMERS_REG = TIMG0_INT_CLR_TIMERS_T0_INT_CLR_M;
    FENCE();
    *TIMG0_INT_ENA_TIMERS_REG |= TIMG0_INT_ENA_TIMERS_T0_INT_ENA_M;
    FENCE();

    /* 9. Route TIMG0 Timer 0 interrupt (INT_SRC_TG0_T0 = 51) to CPU channel 6 */
    interrupt_route(INT_SRC_TG0_T0, TIMER_CPU_INTR_CHANNEL);
    interrupt_set_priority(TIMER_CPU_INTR_CHANNEL, TIMER_INTR_PRIORITY);
    interrupt_set_type(TIMER_CPU_INTR_CHANNEL, INTR_TYPE_LEVEL);
    interrupt_register_handler(TIMER_CPU_INTR_CHANNEL, timer_isr, NULL);
    interrupt_enable(TIMER_CPU_INTR_CHANNEL);
    interrupt_global_enable();
    FENCE();

    /* 10. Store base configuration word and enable timer with auto-reload and alarm */
    s_timer_cfg_base = TIMG0_T0CONFIG_EN_M |
                       TIMG0_T0CONFIG_INCREASE_M |
                       TIMG0_T0CONFIG_AUTORELOAD_M |
                       TIMG0_T0CONFIG_USE_XTAL_M |
                       ((TIMER_PRESCALER_DIV << TIMG0_T0CONFIG_DIVIDER_S) & TIMG0_T0CONFIG_DIVIDER_M);

    *TIMG0_T0CONFIG_REG = s_timer_cfg_base | TIMG0_T0CONFIG_ALARM_EN_M;
    FENCE();

    /* 11. Update telemetry status */
    g_timer_status.interval_sec = interval_sec;
    g_timer_status.interval_ticks = target_ticks;
    g_timer_status.isr_count = 0U;
    g_timer_status.dpc_count = 0U;
    g_timer_status.active = 1U;
}

void timer_isr(void *arg)
{
    (void)arg;

    /* 1. Clear hardware peripheral interrupt status */
    *TIMG0_INT_CLR_TIMERS_REG = TIMG0_INT_CLR_TIMERS_T0_INT_CLR_M;
    FENCE();

    /* 2. Re-arm alarm for next periodic interval */
    *TIMG0_T0CONFIG_REG = s_timer_cfg_base | TIMG0_T0CONFIG_ALARM_EN_M;
    FENCE();

    /* 3. Record hardware interrupt occurrence */
    g_timer_status.isr_count++;

    /* 4. Enqueue deferred procedure call to execute console notification safely in thread context */
    dpc_enqueue(DPC_TYPE_TIMER_TICK, g_timer_status.isr_count, 0U, timer_dpc_handler);
}

void timer_dpc_handler(uint32_t tick_count, uint32_t arg1)
{
    (void)tick_count;
    (void)arg1;
    g_timer_status.dpc_count++;

    /* Asynchronous console notice silenced for clean interactive shell operation.
     * Telemetry remains fully inspectable via 'timer' and 'info' commands. */
}

void timer_start(void)
{
    if (g_timer_status.active) return;

    /* Reload counter to 0 */
    *TIMG0_T0LOADLO_REG = 0U;
    *TIMG0_T0LOADHI_REG = 0U;
    FENCE();
    *TIMG0_T0LOAD_REG = TIMG0_T0LOAD_LOAD_M;
    FENCE();

    /* Clear pending interrupts */
    *TIMG0_INT_CLR_TIMERS_REG = TIMG0_INT_CLR_TIMERS_T0_INT_CLR_M;
    FENCE();
    *TIMG0_INT_ENA_TIMERS_REG |= TIMG0_INT_ENA_TIMERS_T0_INT_ENA_M;
    FENCE();

    /* Re-enable alarm and counter */
    *TIMG0_T0CONFIG_REG = s_timer_cfg_base | TIMG0_T0CONFIG_ALARM_EN_M;
    FENCE();

    interrupt_enable(TIMER_CPU_INTR_CHANNEL);
    g_timer_status.active = 1U;
}

void timer_stop(void)
{
    interrupt_disable(TIMER_CPU_INTR_CHANNEL);

    *TIMG0_T0CONFIG_REG &= ~(TIMG0_T0CONFIG_EN_M | TIMG0_T0CONFIG_ALARM_EN_M);
    *TIMG0_INT_ENA_TIMERS_REG &= ~TIMG0_INT_ENA_TIMERS_T0_INT_ENA_M;
    *TIMG0_INT_CLR_TIMERS_REG = TIMG0_INT_CLR_TIMERS_T0_INT_CLR_M;
    FENCE();

    g_timer_status.active = 0U;
}

uint32_t timer_get_tick_count(void)
{
    return g_timer_status.isr_count;
}

void timer_get_status(timer_status_t *out_status)
{
    if (!out_status) return;

    uint32_t prev_mstatus = interrupt_global_save_and_disable();
    out_status->interval_sec = g_timer_status.interval_sec;
    out_status->interval_ticks = g_timer_status.interval_ticks;
    out_status->isr_count = g_timer_status.isr_count;
    out_status->dpc_count = g_timer_status.dpc_count;
    out_status->active = g_timer_status.active;
    interrupt_global_restore(prev_mstatus);
}

uint64_t timer_get_current_ticks(void)
{
    *TIMG0_T0UPDATE_REG = TIMG0_T0UPDATE_UPDATE_M;
    FENCE();
    while (*TIMG0_T0UPDATE_REG & TIMG0_T0UPDATE_UPDATE_M)
    {
    }
    uint32_t lo = *TIMG0_T0LO_REG & TIMG0_T0LO_LO_M;
    uint32_t hi = *TIMG0_T0HI_REG & TIMG0_T0HI_HI_M;
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}
