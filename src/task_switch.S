/*
 * src/task_switch.S
 *
 * RISC-V Cooperative Context Switcher (RV32IMAC / ILP32 ABI)
 * TRM Callee-Saved Register Frame: ra (x1), s0-s11 (x8-x9, x18-x27)
 * Stack alignment: 16 bytes (64-byte frame)
 */

    .section .text
    .global task_switch_asm
    .type task_switch_asm, @function
    .align 4

/*
 * void task_switch_asm(uint32_t *prev_sp_ptr, uint32_t next_sp);
 * a0: Pointer to prev task's saved SP (uint32_t *prev_sp_ptr)
 * a1: Next task's saved SP (uint32_t next_sp)
 */
task_switch_asm:
    /* 1. Allocate 64-byte 16-byte aligned frame on outgoing task's stack */
    addi sp, sp, -64

    /* 2. Preserve return address and callee-saved register state */
    sw ra,  0(sp)
    sw s0,  4(sp)
    sw s1,  8(sp)
    sw s2,  12(sp)
    sw s3,  16(sp)
    sw s4,  20(sp)
    sw s5,  24(sp)
    sw s6,  28(sp)
    sw s7,  32(sp)
    sw s8,  36(sp)
    sw s9,  40(sp)
    sw s10, 44(sp)
    sw s11, 48(sp)

    /* 3. Save outgoing SP into prev_sp_ptr if non-NULL */
    beqz a0, 1f
    sw sp, 0(a0)

1:
    /* 4. Switch hardware stack pointer to incoming task */
    mv sp, a1

    /* 5. Restore callee-saved register state from incoming stack */
    lw ra,  0(sp)
    lw s0,  4(sp)
    lw s1,  8(sp)
    lw s2,  12(sp)
    lw s3,  16(sp)
    lw s4,  20(sp)
    lw s5,  24(sp)
    lw s6,  28(sp)
    lw s7,  32(sp)
    lw s8,  36(sp)
    lw s9,  40(sp)
    lw s10, 44(sp)
    lw s11, 48(sp)

    /* 6. Release stack frame and resume execution */
    addi sp, sp, 64
    ret

    .size task_switch_asm, .-task_switch_asm
