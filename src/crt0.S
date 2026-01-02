.section .text.entry
.global _start

_start:
    la sp, _stack_top

    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    la t0, _sbss
    la t1, _ebss

_zero_bss:
    bge t0, t1, _zero_end
    sw x0, 0(t0)
    addi t0, t0, 4
    j _zero_bss

_zero_end:
    call main

_hang:
    j _hang