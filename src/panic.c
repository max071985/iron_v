#include "trap.h"
#include "utils.h"

const char *trap_get_exception_desc(uint32_t cause)
{
    switch (cause)
    {
        case EXC_INSN_ADDR_MISALIGNED:  return "Instruction Address Misaligned";
        case EXC_INSN_ACCESS_FAULT:     return "Instruction Access Fault";
        case EXC_ILLEGAL_INSN:          return "Illegal Instruction";
        case EXC_BREAKPOINT:            return "Breakpoint / EBREAK";
        case EXC_LOAD_ADDR_MISALIGNED:  return "Load Address Misaligned";
        case EXC_LOAD_ACCESS_FAULT:     return "Load Access Fault";
        case EXC_STORE_ADDR_MISALIGNED: return "Store/AMO Address Misaligned";
        case EXC_STORE_ACCESS_FAULT:    return "Store/AMO Access Fault";
        case EXC_ECALL_U_MODE:          return "Environment Call from U-Mode";
        case EXC_ECALL_M_MODE:          return "Environment Call from M-Mode";
        default:                        return "Reserved / Unknown Exception";
    }
}

static void print_reg(const char *name, uint32_t val)
{
    uart_puts("  ");
    uart_puts(name);
    uart_puts(" = ");
    put_hex(val);
}

void panic_dump(const trapframe_t *tf)
{
    uart_puts("\r\n======================================================================\r\n");
    uart_puts("                     !!! FATAL KERNEL PANIC !!!                       \r\n");
    uart_puts("======================================================================\r\n");

    if (!tf)
    {
        uart_puts("Panic called with NULL trapframe.\r\n");
        while (1) { asm volatile("wfi"); }
    }

    uint32_t is_interrupt = (tf->mcause >> 31) & 1U;
    uint32_t code = tf->mcause & 0x7FFFFFFFU;

    uart_puts(" Trap Type:  ");
    if (is_interrupt)
    {
        uart_puts("INTERRUPT (ID: ");
        put_dec(code);
        uart_puts(")\r\n");
    }
    else
    {
        uart_puts("SYNCHRONOUS EXCEPTION\r\n");
        uart_puts(" Cause:      ");
        uart_puts(trap_get_exception_desc(code));
        uart_puts(" (0x");
        put_hex(code);
        uart_puts(")\r\n");
    }

    uart_puts(" MEPC:       0x");
    put_hex(tf->mepc);
    uart_puts(" (Faulting Instruction PC)\r\n");

    uart_puts(" MTVAL:      0x");
    put_hex(tf->mtval);
    uart_puts(" (Bad Address / Value)\r\n");

    uart_puts(" MSTATUS:    0x");
    put_hex(tf->mstatus);
    uart_puts("\r\n");

    uart_puts("----------------------------------------------------------------------\r\n");
    uart_puts(" General Purpose Register Dump:\r\n");
    print_reg("ra", tf->ra); print_reg("sp", tf->sp); print_reg("gp", tf->gp); uart_puts("\r\n");
    print_reg("tp", tf->tp); print_reg("t0", tf->t0); print_reg("t1", tf->t1); uart_puts("\r\n");
    print_reg("t2", tf->t2); print_reg("s0", tf->s0); print_reg("s1", tf->s1); uart_puts("\r\n");
    print_reg("a0", tf->a0); print_reg("a1", tf->a1); print_reg("a2", tf->a2); uart_puts("\r\n");
    print_reg("a3", tf->a3); print_reg("a4", tf->a4); print_reg("a5", tf->a5); uart_puts("\r\n");
    print_reg("a6", tf->a6); print_reg("a7", tf->a7); print_reg("s2", tf->s2); uart_puts("\r\n");
    print_reg("s3", tf->s3); print_reg("s4", tf->s4); print_reg("s5", tf->s5); uart_puts("\r\n");
    print_reg("s6", tf->s6); print_reg("s7", tf->s7); print_reg("s8", tf->s8); uart_puts("\r\n");
    print_reg("s9", tf->s9); print_reg("s10", tf->s10); print_reg("s11", tf->s11); uart_puts("\r\n");
    print_reg("t3", tf->t3); print_reg("t4", tf->t4); print_reg("t5", tf->t5); uart_puts("\r\n");
    print_reg("t6", tf->t6); uart_puts("\r\n");

    uart_puts("----------------------------------------------------------------------\r\n");
    uart_puts(" Stack Memory Preview (SP = 0x");
    put_hex(tf->sp);
    uart_puts("):\r\n");

    uint32_t *sp_ptr = (uint32_t *)tf->sp;
    if (tf->sp >= 0x40820000 && tf->sp <= 0x40880000)
    {
        for (int i = 0; i < 8; i++)
        {
            uart_puts("  [0x");
            put_hex((uint32_t)(sp_ptr + i));
            uart_puts("] = 0x");
            put_hex(sp_ptr[i]);
            uart_puts("\r\n");
        }
    }
    else
    {
        uart_puts("  (SP out of DRAM bounds)\r\n");
    }

    uart_puts("======================================================================\r\n");
    uart_puts(" SYSTEM HALTED. RESET REQUIRED.\r\n");
    uart_puts("======================================================================\r\n");

    while (1)
    {
        asm volatile("wfi");
    }
}
