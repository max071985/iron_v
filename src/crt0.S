.section .text.entry, "ax"
.global _start
.global _hang

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
_hang:
    wfi
    j _hang
