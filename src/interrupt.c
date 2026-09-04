/*
 * src/interrupt.c
 *
 * ESP32-C6 Interrupt Matrix (INTMTX) and Core Interrupt Controller (INTPRI) Driver
 * TRM Chapter 10 (INTMTX) & Chapter 1 (High-Performance CPU Interrupt Controller)
 */

#include "interrupt.h"
#include "regs/interrupt_core0.h"
#include "regs/intpri.h"
#include "utils.h"

typedef struct {
    isr_handler_t handler;
    void *arg;
    volatile uint32_t count;
} isr_slot_t;

static isr_slot_t g_isr_table[INTERRUPT_CPU_CHANNELS];

void interrupt_init(void)
{
    /* 1. Disable global interrupts and machine-level core interrupt enables */
    interrupt_global_disable();
    asm volatile("csrw mie, zero" ::: "memory");

    /* 2. Enable clock gates for INTMTX and INTPRI */
    *INTERRUPT_CORE0_CLOCK_GATE_REG = 1U;
    *INTPRI_CLOCK_GATE_REG = 1U;

    /* 3. Disable all 77 INTMTX peripheral mapping registers (writing 0 disables source) */
    for (uint32_t s = 0; s < INTERRUPT_SOURCE_COUNT; s++)
    {
        volatile uint32_t *map_reg = (volatile uint32_t *)(INTERRUPT_CORE0_BASE + 4U * s);
        *map_reg = 0U;
    }

    /* 4. Reset INTPRI core interrupt controller registers */
    *INTPRI_CPU_INT_ENABLE_REG = 0U;
    *INTPRI_CPU_INT_TYPE_REG = 0U;   /* Level-triggered by default */
    *INTPRI_CPU_INT_THRESH_REG = 0U; /* Threshold 0: all priorities >= 1 unmasked */

    for (uint32_t c = 0; c < INTERRUPT_CPU_CHANNELS; c++)
    {
        volatile uint32_t *pri_reg = (volatile uint32_t *)(INTPRI_BASE + 0xCU + 4U * c);
        *pri_reg = 0U;
    }

    /* 5. Clear edge-triggered pending states and software interrupts */
    *INTPRI_CPU_INT_CLEAR_REG = 0xFFFFFFFFU;
    *INTPRI_CPU_INT_CLEAR_REG = 0U;

    for (uint32_t sw = 0; sw < 4U; sw++)
    {
        volatile uint32_t *sw_reg = (volatile uint32_t *)(INTPRI_BASE + 0x90U + 4U * sw);
        *sw_reg = 0U;
    }

    /* 6. Clear software ISR registry */
    for (uint32_t c = 0; c < INTERRUPT_CPU_CHANNELS; c++)
    {
        g_isr_table[c].handler = NULL;
        g_isr_table[c].arg = NULL;
        g_isr_table[c].count = 0U;
    }

    asm volatile("fence rw, rw" ::: "memory");
}

int interrupt_route(interrupt_source_t source, uint32_t cpu_channel)
{
    if ((uint32_t)source >= INTERRUPT_SOURCE_COUNT || cpu_channel < 1U || cpu_channel >= INTERRUPT_CPU_CHANNELS)
    {
        return -1;
    }

    uint32_t prev_mstatus = interrupt_global_save_and_disable();
    volatile uint32_t *map_reg = (volatile uint32_t *)(INTERRUPT_CORE0_BASE + 4U * (uint32_t)source);
    *map_reg = (cpu_channel & 0x1FU);
    asm volatile("fence rw, rw" ::: "memory");
    interrupt_global_restore(prev_mstatus);

    return 0;
}

int interrupt_unroute(interrupt_source_t source)
{
    if ((uint32_t)source >= INTERRUPT_SOURCE_COUNT)
    {
        return -1;
    }

    uint32_t prev_mstatus = interrupt_global_save_and_disable();
    volatile uint32_t *map_reg = (volatile uint32_t *)(INTERRUPT_CORE0_BASE + 4U * (uint32_t)source);
    *map_reg = 0U;
    asm volatile("fence rw, rw" ::: "memory");
    interrupt_global_restore(prev_mstatus);

    return 0;
}

uint32_t interrupt_get_map(interrupt_source_t source)
{
    if ((uint32_t)source >= INTERRUPT_SOURCE_COUNT)
    {
        return 0U;
    }

    volatile uint32_t *map_reg = (volatile uint32_t *)(INTERRUPT_CORE0_BASE + 4U * (uint32_t)source);
    return (*map_reg) & 0x1FU;
}

int interrupt_set_priority(uint32_t cpu_channel, uint32_t priority)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS || priority > INTERRUPT_PRIORITY_MAX)
    {
        return -1;
    }

    uint32_t prev_mstatus = interrupt_global_save_and_disable();
    volatile uint32_t *pri_reg = (volatile uint32_t *)(INTPRI_BASE + 0xCU + 4U * cpu_channel);
    *pri_reg = (priority & 0x0FU);
    asm volatile("fence rw, rw" ::: "memory");
    interrupt_global_restore(prev_mstatus);

    return 0;
}

uint32_t interrupt_get_priority(uint32_t cpu_channel)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS)
    {
        return 0U;
    }

    volatile uint32_t *pri_reg = (volatile uint32_t *)(INTPRI_BASE + 0xCU + 4U * cpu_channel);
    return (*pri_reg) & 0x0FU;
}

int interrupt_set_threshold(uint32_t threshold)
{
    if (threshold > INTERRUPT_THRESHOLD_MAX)
    {
        return -1;
    }

    uint32_t prev_mstatus = interrupt_global_save_and_disable();
    *INTPRI_CPU_INT_THRESH_REG = (threshold & 0xFFU);
    asm volatile("fence rw, rw" ::: "memory");
    interrupt_global_restore(prev_mstatus);

    return 0;
}

uint32_t interrupt_get_threshold(void)
{
    return (*INTPRI_CPU_INT_THRESH_REG) & 0xFFU;
}

int interrupt_set_type(uint32_t cpu_channel, interrupt_type_t type)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS)
    {
        return -1;
    }

    uint32_t prev_mstatus = interrupt_global_save_and_disable();
    if (type == INTR_TYPE_EDGE)
    {
        *INTPRI_CPU_INT_TYPE_REG |= (1U << cpu_channel);
    }
    else
    {
        *INTPRI_CPU_INT_TYPE_REG &= ~(1U << cpu_channel);
    }
    asm volatile("fence rw, rw" ::: "memory");
    interrupt_global_restore(prev_mstatus);

    return 0;
}

interrupt_type_t interrupt_get_type(uint32_t cpu_channel)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS)
    {
        return INTR_TYPE_LEVEL;
    }

    return ((*INTPRI_CPU_INT_TYPE_REG & (1U << cpu_channel)) != 0U) ? INTR_TYPE_EDGE : INTR_TYPE_LEVEL;
}

int interrupt_enable(uint32_t cpu_channel)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS)
    {
        return -1;
    }

    uint32_t prev_mstatus = interrupt_global_save_and_disable();
    *INTPRI_CPU_INT_ENABLE_REG |= (1U << cpu_channel);
    asm volatile("csrs mie, %0" :: "r"(1U << cpu_channel) : "memory");
    asm volatile("fence rw, rw" ::: "memory");
    interrupt_global_restore(prev_mstatus);

    return 0;
}

int interrupt_disable(uint32_t cpu_channel)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS)
    {
        return -1;
    }

    uint32_t prev_mstatus = interrupt_global_save_and_disable();
    *INTPRI_CPU_INT_ENABLE_REG &= ~(1U << cpu_channel);
    asm volatile("csrc mie, %0" :: "r"(1U << cpu_channel) : "memory");
    asm volatile("fence rw, rw" ::: "memory");
    interrupt_global_restore(prev_mstatus);

    return 0;
}

int interrupt_is_enabled(uint32_t cpu_channel)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS)
    {
        return 0;
    }

    return ((*INTPRI_CPU_INT_ENABLE_REG & (1U << cpu_channel)) != 0U);
}

int interrupt_register_handler(uint32_t cpu_channel, isr_handler_t handler, void *arg)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS || !handler)
    {
        return -1;
    }

    g_isr_table[cpu_channel].handler = handler;
    g_isr_table[cpu_channel].arg = arg;
    return 0;
}

int interrupt_unregister_handler(uint32_t cpu_channel)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS)
    {
        return -1;
    }

    g_isr_table[cpu_channel].handler = NULL;
    g_isr_table[cpu_channel].arg = NULL;
    return 0;
}

uint32_t interrupt_get_count(uint32_t cpu_channel)
{
    if (cpu_channel >= INTERRUPT_CPU_CHANNELS)
    {
        return 0U;
    }

    return g_isr_table[cpu_channel].count;
}

void interrupt_trigger_cpu_intr(uint32_t sw_intr_idx)
{
    if (sw_intr_idx < 4U)
    {
        volatile uint32_t *sw_reg = (volatile uint32_t *)(INTPRI_BASE + 0x90U + 4U * sw_intr_idx);
        *sw_reg = 1U;
        asm volatile("fence rw, rw" ::: "memory");
    }
}

void interrupt_clear_cpu_intr(uint32_t sw_intr_idx)
{
    if (sw_intr_idx < 4U)
    {
        volatile uint32_t *sw_reg = (volatile uint32_t *)(INTPRI_BASE + 0x90U + 4U * sw_intr_idx);
        *sw_reg = 0U;
        asm volatile("fence rw, rw" ::: "memory");
    }
}

void interrupt_global_enable(void)
{
    asm volatile("csrsi mstatus, 0x8" ::: "memory");
}

void interrupt_global_disable(void)
{
    asm volatile("csrci mstatus, 0x8" ::: "memory");
}

uint32_t interrupt_global_save_and_disable(void)
{
    uint32_t prev;
    asm volatile("csrrci %0, mstatus, 0x8" : "=r"(prev) :: "memory");
    return prev;
}

void interrupt_global_restore(uint32_t prev_mstatus)
{
    if (prev_mstatus & 0x8U)
    {
        asm volatile("csrsi mstatus, 0x8" ::: "memory");
    }
}

void interrupt_dispatch(uint32_t channel, trapframe_t *tf)
{
    if (channel < INTERRUPT_CPU_CHANNELS && g_isr_table[channel].handler)
    {
        g_isr_table[channel].count++;
        g_isr_table[channel].handler(g_isr_table[channel].arg);

        /* Acknowledge edge-triggered interrupts in INTPRI */
        if (*INTPRI_CPU_INT_TYPE_REG & (1U << channel))
        {
            *INTPRI_CPU_INT_CLEAR_REG = (1U << channel);
            *INTPRI_CPU_INT_CLEAR_REG = 0U;
        }
    }
    else
    {
        /* Unhandled interrupt: dump architectural telemetry and halt */
        panic_dump(tf);
    }
}
