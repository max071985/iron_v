.section .text.entry
.global _start

_start:
    la sp, _stack_top

    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
