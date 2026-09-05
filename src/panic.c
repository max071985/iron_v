#include "trap.h"
#include "utils.h"
#include "console.h"
#include "io_constants.h"

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
    console_puts("  ");
    console_puts(name);
    console_puts(" = ");
    put_hex(val);
}

void panic_print_stack_preview(const trapframe_t *tf)
{
    console_puts("----------------------------------------------------------------------\r\n");
    if (!tf)
    {
        console_puts("  (NULL trapframe)\r\n");
        return;
    }

    console_puts(" Stack Memory Preview (SP = ");
    put_hex(tf->sp);
    console_puts("):\r\n");

    if (((tf->sp & PANIC_SP_ALIGNMENT_MASK) == 0U) &&
        (tf->sp >= HP_DRAM_START_ADDR) &&
        (tf->sp <= (HP_DRAM_END_ADDR - (PANIC_STACK_WORDS * sizeof(uint32_t)))))
    {
        uint32_t *sp_ptr = (uint32_t *)tf->sp;
        for (uint32_t i = 0U; i < PANIC_STACK_WORDS; i++)
        {
            console_puts("  [");
            put_hex((uint32_t)(sp_ptr + i));
            console_puts("] = ");
            put_hex(sp_ptr[i]);
            console_puts("\r\n");
        }
    }
    else
    {
        console_puts("  (SP out of DRAM bounds)\r\n");
    }
}

void panic_dump(const trapframe_t *tf)
{
    console_puts("\r\n======================================================================\r\n");
    console_puts("                     !!! FATAL KERNEL PANIC !!!                       \r\n");
    console_puts("======================================================================\r\n");

    if (!tf)
    {
        console_puts("Panic called with NULL trapframe.\r\n");
        while (1) { asm volatile("wfi"); }
    }

    uint32_t is_interrupt = (tf->mcause & MCAUSE_INTERRUPT_FLAG) != 0U;
    uint32_t code = tf->mcause & MCAUSE_CAUSE_CODE_MASK;

    console_puts(" Trap Type:  ");
    if (is_interrupt)
    {
        console_puts("INTERRUPT (ID: ");
        put_dec(code);
        console_puts(")\r\n");
    }
    else
    {
        console_puts("SYNCHRONOUS EXCEPTION\r\n");
        console_puts(" Cause:      ");
        console_puts(trap_get_exception_desc(code));
        console_puts(" (");
        put_hex(code);
        console_puts(")\r\n");
    }

    console_puts(" MEPC:       ");
    put_hex(tf->mepc);
    console_puts(" (Faulting Instruction PC)\r\n");

    console_puts(" MTVAL:      ");
    put_hex(tf->mtval);
    console_puts(" (Bad Address / Value)\r\n");

    console_puts(" MSTATUS:    ");
    put_hex(tf->mstatus);
    console_puts("\r\n");

    console_puts("----------------------------------------------------------------------\r\n");
    console_puts(" General Purpose Register Dump:\r\n");
    print_reg("ra", tf->ra); print_reg("sp", tf->sp); print_reg("gp", tf->gp); console_puts("\r\n");
    print_reg("tp", tf->tp); print_reg("t0", tf->t0); print_reg("t1", tf->t1); console_puts("\r\n");
    print_reg("t2", tf->t2); print_reg("s0", tf->s0); print_reg("s1", tf->s1); console_puts("\r\n");
    print_reg("a0", tf->a0); print_reg("a1", tf->a1); print_reg("a2", tf->a2); console_puts("\r\n");
    print_reg("a3", tf->a3); print_reg("a4", tf->a4); print_reg("a5", tf->a5); console_puts("\r\n");
    print_reg("a6", tf->a6); print_reg("a7", tf->a7); print_reg("s2", tf->s2); console_puts("\r\n");
    print_reg("s3", tf->s3); print_reg("s4", tf->s4); print_reg("s5", tf->s5); console_puts("\r\n");
    print_reg("s6", tf->s6); print_reg("s7", tf->s7); print_reg("s8", tf->s8); console_puts("\r\n");
    print_reg("s9", tf->s9); print_reg("s10", tf->s10); print_reg("s11", tf->s11); console_puts("\r\n");
    print_reg("t3", tf->t3); print_reg("t4", tf->t4); print_reg("t5", tf->t5); console_puts("\r\n");
    print_reg("t6", tf->t6); console_puts("\r\n");

    panic_print_stack_preview(tf);

    console_puts("======================================================================\r\n");
    console_puts(" SYSTEM HALTED. RESET REQUIRED.\r\n");
    console_puts("======================================================================\r\n");

    console_flush();

    while (1)
    {
        asm volatile("wfi");
    }
}
