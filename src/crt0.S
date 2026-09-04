.section .text.entry, "ax"
.global _start

_start:
    /* 1. Setup Stack Pointer (Top of DRAM, 16-byte aligned) */
    la sp, _stack_top

    /* 2. Setup Global Pointer (gp) safely to optimize linker relaxation */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* 3. Initialize Machine-Mode CSRs */
    /* Disable all interrupts */
    csrw mie, zero
    /* Clear pending interrupts */
    csrw mip, zero
    /* Set mstatus MPP to Machine Mode (bits 12:11 = 2'b11 = 0x1800), MIE = 0 */
    li t0, 0x1800
    csrw mstatus, t0
    /* Clear supervisory/scratch registers */
    csrw mscratch, zero
    csrw mcause, zero
    csrw mtval, zero
    /* Point mtvec to 256-byte aligned vector table in vectored mode (bit 0 = 1, TRM Reg 1.9) */
    la t0, _vector_table
    csrw mtvec, t0

    /* 4. Zero-initialize the .bss section */
    la t0, _sbss
    la t1, _ebss

1:
    bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b

2:
    /* 5. Memory barrier to guarantee BSS clearing is committed */
    fence rw, rw

    /* 6. Jump into runtime main() */
    call main

    /* Safety Net infinite loop in case main() ever returns */
    j _hang

    /*
     * ESP32-C6 Trap Vector Table
     * TRM Register 1.9 mandates:
     * - mtvec.MODE (bits 1:0) is RO and hardwired to 0x1 (Vectored Mode)
     * - mtvec.BASE (bits 31:8) must be 256-byte aligned (.align 8)
     * - Vector 0: Synchronous Exceptions
     * - Vectors 1-31: Interrupts (BASE + 4 * id)
     */
    .align 8
    .global _vector_table
_vector_table:
    j _hang             /* Vector 0: All synchronous exceptions */
    .rept 31
    j _hang             /* Vectors 1-31: Core & external interrupts */
    .endr

    .global _hang
_hang:
    wfi
    j _hang
