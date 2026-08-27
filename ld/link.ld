ENTRY(_start)

MEMORY
{
    hp_sram (rwx) : ORIGIN = 0x40800000, LENGTH = 512K
}

SECTIONS
{
    . = ORIGIN(hp_sram);

    .text : ALIGN(16) {
        _stext = .;
        KEEP(*(.text.entry))
        *(.text .text.*)
        . = ALIGN(16);
        _etext = .;
    } > hp_sram

    .rodata : ALIGN(16) {
        _srodata = .;
        *(.rodata .rodata.*)
        . = ALIGN(16);
        _erodata = .;
    } > hp_sram

    .data : ALIGN(16) {
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(16);
        _edata = .;
        __global_pointer$ = _sdata + 0x800;
    } > hp_sram

    .bss (NOLOAD) : ALIGN(16) {
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(16);
        _ebss = .;
    } > hp_sram

    _stack_top = ORIGIN(hp_sram) + LENGTH(hp_sram);
}