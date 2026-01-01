.section .text.entry
.global _start

_start:
    _stack_top dw 0x40880000
    la sp, _stack_top