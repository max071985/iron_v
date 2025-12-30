ENTRY(_start)

MEMORY
{
    hp_sram (rwx) : ORIGIN = 0x40800000, LENGTH = 512K
}

SECTIONS
{
    . = 0x40800000;
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(.bss) }
}