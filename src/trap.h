#ifndef TRAP_H
#define TRAP_H

#include <stdint.h>
#include <stddef.h>

/* RISC-V synchronous exception cause codes (Privileged Spec Table 3.6) */
typedef enum {
    EXC_INSN_ADDR_MISALIGNED = 0,
    EXC_INSN_ACCESS_FAULT    = 1,
    EXC_ILLEGAL_INSN         = 2,
    EXC_BREAKPOINT           = 3,
    EXC_LOAD_ADDR_MISALIGNED = 4,
    EXC_LOAD_ACCESS_FAULT    = 5,
    EXC_STORE_ADDR_MISALIGNED= 6,
    EXC_STORE_ACCESS_FAULT   = 7,
    EXC_ECALL_U_MODE         = 8,
    EXC_ECALL_M_MODE         = 11
} riscv_exception_code_t;

/* 144-byte hardware context saved on trap entry (36 words, 16-byte aligned) */
typedef struct {
    uint32_t ra, sp, gp, tp;
    uint32_t t0, t1, t2;
    uint32_t s0, s1;
    uint32_t a0, a1, a2, a3, a4, a5, a6, a7;
    uint32_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uint32_t t3, t4, t5, t6;
    uint32_t mepc, mcause, mtval, mstatus;
    uint32_t reserved;
} __attribute__((packed, aligned(4))) trapframe_t;

/* External vector table symbol defined in trap_entry.S */
extern void _vector_table(void);

/* Initialize trap handling subsystem and verify mtvec installation */
void trap_init(void);

/* C-level trap dispatcher called from assembly vector */
void trap_handler(trapframe_t *tf);

/* Crash dump and panic routine */
void panic_dump(const trapframe_t *tf);

/* mcause bitfield definitions (Privileged Spec Table 3.6) */
#define MCAUSE_INTERRUPT_FLAG    (1U << 31)
#define MCAUSE_CAUSE_CODE_MASK   0x7FFFFFFFU

/* Query controlled exception (ECALL) execution count */
uint32_t trap_get_ecall_count(void);

/* Get human-readable description of exception cause */
const char *trap_get_exception_desc(uint32_t cause);

/* Panic stack preview configuration */
#define PANIC_STACK_WORDS        8U
#define PANIC_SP_ALIGNMENT_MASK  3U

/* Panic stack preview dump (prints up to 8 words within DRAM bounds) */
void panic_print_stack_preview(const trapframe_t *tf);

#endif // TRAP_H
