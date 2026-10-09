#include "interrupt.h"
#include "io_constants.h"
#include "memstat.h"

.section .text.entry, "ax"
.global _start
.global _hang

_start:
    /* 1. Main stack below the ROM-reserved top of SRAM (ld/link.ld: _main_stack_top) */
    la sp, _main_stack_top

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
    /* Set mstatus MPP to Machine Mode, MIE = 0 */
    li t0, MSTATUS_MPP_MACHINE_MODE
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
    /* 5. Paint the main stack (nothing is on it yet) so `mem` finds its deepest use (REV-32) */
    la t0, _ebss
    la t1, _main_stack_top
    li t2, MEMSTAT_STACK_FILL_WORD

3:
    bgeu t0, t1, 4f
    sw t2, 0(t0)
    addi t0, t0, 4
    j 3b

4:
    /* 6. Memory barrier to guarantee BSS clearing and stack painting are committed */
    fence rw, rw

    /* 7. Jump into runtime main() */
    call main

    /* Safety Net infinite loop in case main() ever returns */
_hang:
    wfi
    j _hang
