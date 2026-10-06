/*
 * Iron V - Flash access through the ROM SPI flash routines (REV-16)
 *
 * Shared by NVS and OTA. Interrupts are off while the flash is busy (ld/link.ld: that is what keeps
 * flash-resident code and ISRs off the cache while the chip cannot answer). Each ROM call is its own
 * short interrupt-off window: reads and writes go in FLASH_ROM_CHUNK_BYTES pieces with interrupts
 * back on in between; a sector erase is one window by nature. Every window is recorded in the loop
 * telemetry (looptime.h).
 */
#ifndef IRON_V_FLASH_ROM_H
#define IRON_V_FLASH_ROM_H

#include <stddef.h>
#include <stdint.h>

#define FLASH_ROM_SECTOR_SIZE   (0x1000U)
#define FLASH_ROM_CHUNK_BYTES   (64U)      /* one ROM read/program call per interrupt-off window */
#define FLASH_ROM_ERASED_BYTE   (0xFFU)

#define FLASH_ROM_OK            0
#define FLASH_ROM_ERR_IO        (-1)

#if defined(__riscv)
int flash_rom_read(uint32_t offset, void *dest, size_t len);
int flash_rom_write(uint32_t offset, const void *src, size_t len);
int flash_rom_erase_sector(uint32_t offset);
#endif

#endif /* IRON_V_FLASH_ROM_H */
