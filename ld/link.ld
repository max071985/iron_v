ENTRY(_start)

MEMORY
{
    hp_sram (rwx) : ORIGIN = 0x40800000, LENGTH = 512K
}

SECTIONS
{
    .text : {
        KEEP(*(.text.entry))
        *(.text)
    } > hp_sram
    .rodata : {
        *(.rodata)
    } > hp_sram
    .data : {
        _sdata = .;
        *(.data)
        _edata = .;
    } > hp_sram
    .bss : {
        _sbss = .;
        *(.bss)
        _ebss = .;
    } > hp_sram
    _stack_top = ORIGIN(hp_sram) + LENGTH(hp_sram);
}