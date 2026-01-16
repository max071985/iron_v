.section .text.entry
.global _start

_start:
    la sp, _stack_top   # Init stack pointer based on linker defined _stack_top

    /* * Initialize gp safely
    * 'push -> norelax -> pop' allows us to avoid predefined optimization shortcuts in order to use them later on with our new gp value.
    */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* * The next section zeros the .bss memory block 
    * fetch _sbss (start of the block) and _ebss (end of the block)
    * iterate over it and zero the memory
    */
    la t0, _sbss
    la t1, _ebss

_zero_bss:
    bge t0, t1, _zero_end
    sw x0, 0(t0)
    addi t0, t0, 4
    j _zero_bss

_zero_end:
    /* Setup trap vector */
    la t0, _trap_handler
    CSRW mtvec, t0
    /* Enable CPU interrupt 18 in MIE */
    li t0, (1 << 18)
    CSRS mie, t0
    /* Enable Global Interrupts in MIE */
    li t0, 0x8
    CSRS mstatus, t0
    call main   # Call main function

# Safety Net infinite loop in case main returns
_hang:
    j _hang

.align 4
_trap_handler:
    j uart_isr