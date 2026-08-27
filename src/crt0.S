.section .text.entry
.global _start

_start:
    /* 1. Setup Stack Pointer (Top of SRAM) */
    la sp, _stack_top

    /* 2. Setup Global Pointer (gp) safely to optimize relaxation */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* 3. Initialize Machine-Mode CSRs (Disable interrupts & clear pending) */
    csrw mie, zero
    csrw mip, zero

    /* 4. Zero-initialize the .bss section */
    la t0, _sbss
    la t1, _ebss

_zero_bss:
    bge t0, t1, _zero_end
    sw x0, 0(t0)
    addi t0, t0, 4
    j _zero_bss

_zero_end:
    /* 5. Memory barrier before calling main */
    fence
    call main

    /* Safety Net infinite loop in case main returns */
_hang:
    wfi
    j _hang