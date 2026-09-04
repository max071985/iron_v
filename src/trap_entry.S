/*
 * src/trap_entry.S
 *
 * RISC-V Machine-Mode Trap Vector Table and Context Save/Restore routines.
 * Aligned to 256 bytes per ESP32-C6 TRM Register 1.9 (Vectored Mode).
 */

.section .text.vector, "ax"
.align 8
.global _vector_table

_vector_table:
    j trap_entry_exception      /* Vector 0: Synchronous Exceptions */
    .rept 31
    j trap_entry_interrupt      /* Vectors 1-31: Interrupt Lines */
    .endr

.section .text, "ax"
.global trap_entry_exception
.global trap_entry_interrupt

trap_entry_exception:
trap_entry_interrupt:
    /* 1. Allocate 144-byte trapframe on stack (36 words, 16-byte aligned) */
    addi sp, sp, -144

    /* 2. Save Caller-Saved & Callee-Saved Registers */
    sw ra, 0(sp)
    sw gp, 8(sp)
    sw tp, 12(sp)
    sw t0, 16(sp)
    sw t1, 20(sp)
    sw t2, 24(sp)
    sw s0, 28(sp)
    sw s1, 32(sp)
    sw a0, 36(sp)
    sw a1, 40(sp)
    sw a2, 44(sp)
    sw a3, 48(sp)
    sw a4, 52(sp)
    sw a5, 56(sp)
    sw a6, 60(sp)
    sw a7, 64(sp)
    sw s2, 68(sp)
    sw s3, 72(sp)
    sw s4, 76(sp)
    sw s5, 80(sp)
    sw s6, 84(sp)
    sw s7, 88(sp)
    sw s8, 92(sp)
    sw s9, 96(sp)
    sw s10, 100(sp)
    sw s11, 104(sp)
    sw t3, 108(sp)
    sw t4, 112(sp)
    sw t5, 116(sp)
    sw t6, 120(sp)

    /* 3. Compute and store original pre-trap stack pointer */
    addi t0, sp, 144
    sw t0, 4(sp)

    /* 4. Read Machine CSRs and store into trapframe */
    csrr t0, mepc
    csrr t1, mcause
    csrr t2, mtval
    csrr t3, mstatus
    sw t0, 124(sp)
    sw t1, 128(sp)
    sw t2, 132(sp)
    sw t3, 136(sp)
    sw zero, 140(sp) /* 16-byte alignment padding */

    /* 5. Pass trapframe pointer as first argument (a0) to C trap_handler */
    mv a0, sp
    call trap_handler

    /* 6. Restore CSRs (mepc may have been modified by handler, e.g., mepc += 4 on ECALL) */
    lw t0, 124(sp)
    lw t3, 136(sp)
    csrw mepc, t0
    csrw mstatus, t3

    /* 7. Restore General Purpose Registers */
    lw ra, 0(sp)
    lw gp, 8(sp)
    lw tp, 12(sp)
    lw t0, 16(sp)
    lw t1, 20(sp)
    lw t2, 24(sp)
    lw s0, 28(sp)
    lw s1, 32(sp)
    lw a0, 36(sp)
    lw a1, 40(sp)
    lw a2, 44(sp)
    lw a3, 48(sp)
    lw a4, 52(sp)
    lw a5, 56(sp)
    lw a6, 60(sp)
    lw a7, 64(sp)
    lw s2, 68(sp)
    lw s3, 72(sp)
    lw s4, 76(sp)
    lw s5, 80(sp)
    lw s6, 84(sp)
    lw s7, 88(sp)
    lw s8, 92(sp)
    lw s9, 96(sp)
    lw s10, 100(sp)
    lw s11, 104(sp)
    lw t3, 108(sp)
    lw t4, 112(sp)
    lw t5, 116(sp)
    lw t6, 120(sp)

    /* 8. Deallocate trapframe */
    addi sp, sp, 144

    /* 9. Return from Machine-Mode Trap */
    mret
