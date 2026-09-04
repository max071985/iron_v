#include "trap.h"
#include "utils.h"

static volatile uint32_t g_ecall_count = 0;

void trap_init(void)
{
    /*
     * Install trap vector table address into mtvec.
     * ESP32-C6 TRM Register 1.9 dictates:
     * - bits 31:8: 256-byte aligned BASE
     * - bits 1:0:  RO hardwired to 0x1 (Vectored Mode)
     */
    uint32_t vec_addr = ((uint32_t)_vector_table);
    asm volatile("csrw mtvec, %0" :: "r"(vec_addr) : "memory");
    asm volatile("fence rw, rw" ::: "memory");
}

void trap_handler(trapframe_t *tf)
{
    if (!tf)
    {
        panic_dump(NULL);
        return;
    }

    uint32_t is_interrupt = (tf->mcause >> 31) & 1U;
    uint32_t cause = tf->mcause & 0x7FFFFFFFU;

    if (is_interrupt)
    {
        /* Interrupt handling will be wired in Task 2.2 via Interrupt Matrix */
        panic_dump(tf);
    }
    else
    {
        /* Synchronous Exceptions */
        if (cause == EXC_ECALL_M_MODE)
        {
            /* Controlled ECALL exception triggered by software test harness */
            g_ecall_count++;

            /* Advance mepc past the 4-byte ecall instruction to resume cleanly */
            tf->mepc += 4;
            return;
        }

        /* Unhandled exception: dump architectural telemetry and halt */
        panic_dump(tf);
    }
}

uint32_t trap_get_ecall_count(void)
{
    return g_ecall_count;
}
