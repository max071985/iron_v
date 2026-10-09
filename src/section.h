/*
 * src/section.h
 *
 * Code and data placement (see ld/link.ld for the full policy).
 *
 * All code runs from flash (XIP through the cache) except a short keep-list
 * of core objects in IRAM. IRAM_ATTR moves one function into IRAM; use it only
 * for code that runs while flash is unavailable:
 *   - flash erase/write/read through the ROM routines (interrupts off, flash busy)
 *   - anything that runs before mmu_init() has mapped flash
 * Everything such a function calls must also be in IRAM or ROM; tests/test_runner.py
 * checks this on the linked ELF.
 *
 * FLASH_TEXT_ATTR keeps cold code (init, diagnostics) of an IRAM-resident object
 * in flash. Constants live in flash, except in the objects ld/link.ld keeps in DRAM
 * (IRAM keep-list, flash_rom, hw_rng, looptime); FLASH_RODATA_ATTR moves one constant
 * of such an object to flash. Data read by a DMA engine or by IRAM_ATTR code must be
 * in RAM: an object of the DRAM keep-list, or not const.
 */
#ifndef IRON_V_SECTION_H
#define IRON_V_SECTION_H

#if defined(__riscv)
/* noinline: an inlined copy would run from its caller's section (usually flash) */
#define IRAM_ATTR          __attribute__((section(".iram1"), noinline))
/* Cold code (init, diagnostics) inside an object that ld/link.ld keeps in IRAM */
#define FLASH_TEXT_ATTR    __attribute__((section(".flash.text")))
#define FLASH_RODATA_ATTR  __attribute__((section(".flash.rodata")))
#else
#define IRAM_ATTR
#define FLASH_TEXT_ATTR
#define FLASH_RODATA_ATTR
#endif

#endif /* IRON_V_SECTION_H */
