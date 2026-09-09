/*
 * lp_core/link.ld
 *
 * Standalone Linker Script for ESP32-C6 Low-Power (LP) RV32IMAC Coprocessor
 * TRM Chapter 3 (§3.1-§3.9 Low-Power CPU)
 *
 * ESP32-C6 LP Core Hardware Execution Mapping:
 *   - Base address of vector table is initialized to 0x50000000 on startup (TRM §3.3).
 *   - The core fetches instructions from address 0x50000080 after reset (TRM §3.3).
 *   - Memory cartography within 16 KB LP SRAM (0x50000000 - 0x50003FFF):
 *       0x50000000 - 0x5000007F: 128-byte Vector Table (lp_vectors)
 *       0x50000080 - 0x50001FFF: LP Firmware Code & Read-Only Data (lp_iram)
 *       0x50002000 - 0x500027FF: 2 KB LP Static Data & BSS (lp_dram)
 *       0x50002800 - 0x50002FFF: 2 KB LP Execution Stack (Top at 0x50003000)
 *       0x50003000 - 0x50003FFF: 4 KB Retained Shared Mailbox (lp_shared)
 */

ENTRY(_lp_reset)

MEMORY
{
    lp_vectors (rx) : ORIGIN = 0x50000000, LENGTH = 0x80
    lp_iram    (rx) : ORIGIN = 0x50000080, LENGTH = 0x1F80
    lp_dram    (rw) : ORIGIN = 0x50002000, LENGTH = 2K
    lp_stack   (rw) : ORIGIN = 0x50002800, LENGTH = 0x7F0  /* 2032 bytes, top at 0x50002FF0 */
    lp_shared  (rw) : ORIGIN = 0x50003000, LENGTH = 4K
}

SECTIONS
{
    .vectors :
    {
        . = ALIGN(4);
        KEEP(*(.vectors))
        . = ALIGN(4);
    } > lp_vectors

    .text :
    {
        . = ALIGN(4);
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > lp_iram

    .data :
    {
        . = ALIGN(4);
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > lp_dram

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        __bss_end = .;
        . = ALIGN(4);
    } > lp_dram

    .shared (NOLOAD) :
    {
        . = ALIGN(4);
        *(.shared_mailbox)
        . = ALIGN(4);
    } > lp_shared

    /* Stack symbol top at 0x50002FF0 (leaving 0x50002FF8-0x50002FFC for mailbox) */
    _lp_stack_top = ORIGIN(lp_stack) + LENGTH(lp_stack);
}
