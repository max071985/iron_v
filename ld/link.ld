ENTRY(_start)

MEMORY
{
    hp_iram (rx)  : ORIGIN = 0x40800000, LENGTH = 128K
    hp_dram (rw)  : ORIGIN = 0x40820000, LENGTH = 384K
    lp_sram (rw)  : ORIGIN = 0x50000000, LENGTH = 16K
    flash_xip (rx): ORIGIN = 0x42000000, LENGTH = 8M
}

SECTIONS
{
    .text : ALIGN(16) {
        _stext = .;
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.init .init.*)
        . = ALIGN(16);
        _etext = .;
    } > hp_iram

    .rodata : ALIGN(16) {
        _srodata = .;
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(16);
        _erodata = .;
    } > hp_dram

    .data : ALIGN(16) {
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(16);
        _edata = .;
        __global_pointer$ = _sdata + 0x800;
    } > hp_dram

    .bss (NOLOAD) : ALIGN(16) {
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(16);
        _ebss = .;
    } > hp_dram

    .heap (NOLOAD) : ALIGN(16) {
        _sheap = .;
        . = ALIGN(16);
        _eheap = .;
    } > hp_dram

    .lp_sram (NOLOAD) : ALIGN(16) {
        _slp_sram = .;
        _lp_sram_start = .;
        *(.lp_sram .lp_sram.*)
        . = ALIGN(16);
        _elp_sram = .;
    } > lp_sram

    .flash_xip : ALIGN(16) {
        _sflash_xip = .;
        _flash_text_start = .;
        *(.flash.text .flash.text.*)
        _flash_rodata_start = .;
        *(.flash.rodata .flash.rodata.*)
        . = ALIGN(16);
        _eflash_xip = .;
    } > flash_xip

    _stack_top = ORIGIN(hp_dram) + LENGTH(hp_dram);
}
