/*
 * Iron-V linker script, ESP32-C6 (image loaded directly by the ROM loader).
 *
 * HP SRAM is one 512 KB block reachable at the same address for code and data
 * (no I/D split register on the C6; ESP-IDF uses a single sram_seg as well).
 * It is laid out as one region (REV-34): IRAM code, DRAM constants, .data,
 * .bss and the main stack follow each other, so every byte IRAM code does not
 * use belongs to DRAM. Program headers keep code R+X and data R+W.
 *
 * Code placement policy:
 *   - All code runs from flash (XIP through the cache, .flash_xip) by default.
 *   - IRAM (.text) holds only code that must not depend on flash:
 *       boot and clock/MMU bring-up (runs before mmu_init() maps flash),
 *       trap/interrupt entry, panic, console and watchdog paths, the scheduler,
 *       the Wi-Fi blob OS adapter, libgcc and the blob's own IRAM sections,
 *       plus IRAM_ATTR functions (src/section.h), e.g. the flash erase/write
 *       routines that run with interrupts off while flash is busy.
 *     The packet path (wifi.o, net.o, tcp.o) also stays in IRAM as a latency
 *     choice: it runs inside the Wi-Fi task's poll, and from a cold cache the
 *     net self-test took 956 us in flash against 118 us in IRAM.
 *   - Read-only data of Iron-V's own objects lives in flash (REV-34), except
 *     for the EXCLUDE_FILE list in .flash_xip: the IRAM keep-list (their code
 *     runs without flash, or is latency-critical) and the objects that exist
 *     for IRAM_ATTR code (flash_rom, hw_rng, looptime). nvs.o and ota.o mix
 *     main-loop code with IRAM_ATTR flash routines; their constants go to flash
 *     because those routines read none. Vendor libraries keep their constants
 *     in DRAM (REV-35). DMA engines must never read from flash either: keep
 *     DMA sources in RAM. tests/test_runner.py checks that no IRAM-only code
 *     path calls into flash or loads an address in flash constants.
 * Interrupts are disabled around every flash operation, so flash-resident
 * interrupt handlers are safe; keeping them in IRAM is only a latency choice.
 *
 * mmu_init() maps the single .flash_xip output section contiguously from flash
 * offset 0x10000, so nothing else may be placed in the flash region.
 */
ENTRY(_start)

MEMORY
{
    hp_sram (rwx) : ORIGIN = 0x40800000, LENGTH = 512K
    lp_sram (rw)  : ORIGIN = 0x50000000, LENGTH = 16K
    flash_xip (rx): ORIGIN = 0x42000000, LENGTH = 8M
}

/* ESP32-C6 ROM use of the top of HP SRAM (ESP-IDF 66ab063a9a7f,
 * components/bootloader/subproject/main/ld/esp32c6/bootloader.memory.ld.in):
 *   0x4087c610 - 0x4087e610  ROM boot stack, free once the firmware runs
 *   0x4087e610 - 0x40880000  ROM .bss/.data, used by ROM functions at run time */
ROM_DATA_START          = 0x4087E610;
MAIN_STACK_ROM_GAP      = 0x3200;   /* main stack starts this far below the end of SRAM */
/* MAIN_STACK_MIN_SIZE: the build profile's MAIN_STACK_MIN (Makefile: --defsym, REV-33) */
/* Room left for IRAM code growth: the main stack must exceed its budget by this much.
 * (With one SRAM region there is no separate IRAM to run out of; IRAM code takes from DRAM.) */
IRAM_MIN_FREE           = 16K;

PHDRS
{
    iram  PT_LOAD FLAGS(5);     /* R X: IRAM code */
    xip   PT_LOAD FLAGS(5);     /* R X: flash code + constants */
    drom  PT_LOAD FLAGS(4);     /* R:   DRAM constants */
    dram  PT_LOAD FLAGS(6);     /* R W: .data, .bss, heap */
    lp    PT_LOAD FLAGS(6);     /* R W: LP SRAM (NOLOAD) */
}

SECTIONS
{
    /* IRAM keep-list. It must come before .flash_xip: the first rule that
     * matches an input section wins, and .flash_xip ends with *(.text .text.*) */
    .text : ALIGN(16) {
        _stext = .;
        KEEP(*(.text.entry))
        *crt0.o(.text .text.*)
        *trap_entry.o(.text .text.*)
        *trap.o(.text .text.*)
        *panic.o(.text .text.*)
        *task_switch.o(.text .text.*)
        *task.o(.text .text.*)
        *interrupt.o(.text .text.*)
        *dpc.o(.text .text.*)
        *systimer.o(.text .text.*)
        *timer.o(.text .text.*)
        *wdt.o(.text .text.*)
        *uart.o(.text .text.*)
        *usb_serial.o(.text .text.*)
        *console.o(.text .text.*)
        *utils.o(.text .text.*)
        *string.o(.text .text.*)
        *arena.o(.text .text.*)
        *clock.o(.text .text.*)
        *mmu.o(.text .text.*)
        *power.o(.text .text.*)
        *main.o(.text .text.*)
        *wifi_os_adapter.o(.text .text.*)
        *wifi.o(.text .text.*)        /* packet path: latency, see header */
        *net.o(.text .text.*)
        *tcp.o(.text .text.*)
        *libgcc.a:(.text .text.*)
        *(.init .init.*)
        *(.iram1 .iram1.*)
        *(.wifi*iram .wifi*iram.*)
        *(.wifiextrairam .wifiextrairam.*)
        *(.wifirxiram .wifirxiram.*)
        *(.wifislpiram .wifislpiram.*)
        *(.wifislprxiram .wifislprxiram.*)
        . = ALIGN(16);
        _etext = .;
    } > hp_sram :iram

    .flash_xip : ALIGN(16) {
        _sflash_xip = .;
        _flash_text_start = .;
        *(.text .text.*)
        *(.flash_text .flash_text.*)
        *(.flash.text .flash.text.*)
        _flash_rodata_start = .;
        *(.flash.rodata .flash.rodata.*)
        /* Own objects' constants (REV-34): everything under build/<profile>/obj/ except the
         * DRAM keep-list. Vendor archive members (libs/) never match this file pattern. */
        EXCLUDE_FILE(
            */obj/crt0.o */obj/trap_entry.o */obj/trap.o */obj/panic.o
            */obj/task_switch.o */obj/task.o */obj/interrupt.o */obj/dpc.o
            */obj/systimer.o */obj/timer.o */obj/wdt.o */obj/uart.o
            */obj/usb_serial.o */obj/console.o */obj/utils.o */obj/string.o
            */obj/arena.o */obj/clock.o */obj/mmu.o */obj/power.o */obj/main.o
            */obj/wifi_os_adapter.o */obj/wifi.o */obj/net.o */obj/tcp.o
            */obj/flash_rom.o */obj/hw_rng.o */obj/looptime.o
        ) */obj/*(.rodata .rodata.* .srodata .srodata.*)
        . = ALIGN(4);
        __start_iron_modules = .;
        KEEP(*(iron_modules))
        __stop_iron_modules = .;
        . = ALIGN(4);
        __start_iron_shell_cmds = .;
        KEEP(*(iron_shell_cmds))
        __stop_iron_shell_cmds = .;
        . = ALIGN(16);
        _eflash_xip = .;
    } > flash_xip :xip

    .rodata : ALIGN(16) {
        _srodata = .;
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        *(.rodata_wlog* .rodata_wlog*.*)
        . = ALIGN(16);
        _erodata = .;
    } > hp_sram :drom

    .data : ALIGN(16) {
        _sdata = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.dram1 .dram1.*)
        . = ALIGN(16);
        _edata = .;
        __global_pointer$ = _sdata + 0x800;
    } > hp_sram :dram

    .bss (NOLOAD) : ALIGN(16) {
        _sbss = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(16);
        _ebss = .;
    } > hp_sram :dram

    .heap (NOLOAD) : ALIGN(16) {
        _sheap = .;
        . = ALIGN(16);
        _eheap = .;
    } > hp_sram :dram

    .lp_sram (NOLOAD) : ALIGN(16) {
        _slp_sram = .;
        _lp_sram_start = .;
        *(.lp_sram .lp_sram.*)
        . = ALIGN(16);
        _elp_sram = .;
    } > lp_sram :lp

    /* Region bounds for the RAM budget (`mem`, REV-32) */
    _sram_region_start = ORIGIN(hp_sram);
    _sram_region_end   = ORIGIN(hp_sram) + LENGTH(hp_sram);

    /* End of HP SRAM; the main stack starts MAIN_STACK_ROM_GAP below it */
    _stack_top = ORIGIN(hp_sram) + LENGTH(hp_sram);
    _main_stack_top = _stack_top - MAIN_STACK_ROM_GAP;

    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
    }
}

/* Budgets: fail the link with a clear message instead of a silent squeeze */
ASSERT(_main_stack_top <= ROM_DATA_START,
       "main stack would overlap ROM .bss/.data (0x4087e610-0x40880000)")
ASSERT(_main_stack_top % 16 == 0, "main stack top must be 16-byte aligned")
ASSERT(_main_stack_top - _ebss >= MAIN_STACK_MIN_SIZE,
       "DRAM: less than MAIN_STACK_MIN_SIZE left for the main stack above .bss")
ASSERT(_main_stack_top - _ebss >= MAIN_STACK_MIN_SIZE + IRAM_MIN_FREE,
       "SRAM: less than IRAM_MIN_FREE above the main-stack budget; keep new code in flash or use IRAM_ATTR sparingly")
ASSERT(_etext <= _srodata, "IRAM code overlaps DRAM constants")
ASSERT(_sflash_xip == ORIGIN(flash_xip),
       ".flash_xip must start at 0x42000000: mmu_init() maps it from flash offset 0x10000")
