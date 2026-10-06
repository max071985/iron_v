/*
 * Iron V - Flash access through the ROM SPI flash routines (REV-16)
 */
#include "flash_rom.h"

#if defined(__riscv)
#include <stdbool.h>
#include "section.h"
#include "string.h"
#include "interrupt.h"
#include "systimer.h"
#include "looptime.h"

/* ROM SPI flash API (esp32c6.rom.api.ld) */
typedef int esp_rom_spiflash_result_t;
extern esp_rom_spiflash_result_t esp_rom_spiflash_read(uint32_t target, uint32_t *dest, int32_t len);
extern esp_rom_spiflash_result_t esp_rom_spiflash_write(uint32_t target, const uint32_t *src, int32_t len);
extern esp_rom_spiflash_result_t esp_rom_spiflash_erase_sector(size_t sector_num);
extern esp_rom_spiflash_result_t esp_rom_spiflash_unlock(void);

#define FLASH_ROM_CHUNK_WORDS   (FLASH_ROM_CHUNK_BYTES / sizeof(uint32_t))
#define FLASH_ROM_WORD_ROUND    (sizeof(uint32_t) - 1U)

static IRAM_ATTR void flash_rom_window_end(loop_irqoff_t kind, uint64_t start_us, uint32_t mstatus)
{
    interrupt_global_restore(mstatus);
    looptime_irqoff_record(kind, (uint32_t)(systimer_get_us() - start_us));
}

static IRAM_ATTR int flash_rom_unlock(void)
{
    uint64_t t0 = systimer_get_us();
    uint32_t mstatus = interrupt_global_save_and_disable();
    int res = esp_rom_spiflash_unlock();
    flash_rom_window_end(LOOP_IRQOFF_FLASH_UNLOCK, t0, mstatus);
    return res;
}

IRAM_ATTR int flash_rom_read(uint32_t offset, void *dest, size_t len)
{
    uint32_t buf[FLASH_ROM_CHUNK_WORDS];
    uint8_t *dst = (uint8_t *)dest;

    while (len > 0U)
    {
        size_t chunk = (len > FLASH_ROM_CHUNK_BYTES) ? FLASH_ROM_CHUNK_BYTES : len;
        size_t bytes = (chunk + FLASH_ROM_WORD_ROUND) & ~FLASH_ROM_WORD_ROUND;

        uint64_t t0 = systimer_get_us();
        uint32_t mstatus = interrupt_global_save_and_disable();
        int res = esp_rom_spiflash_read(offset, buf, (int32_t)bytes);
        flash_rom_window_end(LOOP_IRQOFF_FLASH_READ, t0, mstatus);
        if (res != 0)
        {
            return FLASH_ROM_ERR_IO;
        }

        memcpy(dst, buf, chunk);
        dst += chunk;
        offset += (uint32_t)chunk;
        len -= chunk;
    }
    return FLASH_ROM_OK;
}

IRAM_ATTR int flash_rom_write(uint32_t offset, const void *src, size_t len)
{
    uint32_t buf[FLASH_ROM_CHUNK_WORDS];
    const uint8_t *in = (const uint8_t *)src;

    if (flash_rom_unlock() != 0)
    {
        return FLASH_ROM_ERR_IO;
    }
    while (len > 0U)
    {
        size_t chunk = (len > FLASH_ROM_CHUNK_BYTES) ? FLASH_ROM_CHUNK_BYTES : len;
        size_t bytes = (chunk + FLASH_ROM_WORD_ROUND) & ~FLASH_ROM_WORD_ROUND;
        memset(buf, FLASH_ROM_ERASED_BYTE, sizeof(buf));
        memcpy(buf, in, chunk);

        uint64_t t0 = systimer_get_us();
        uint32_t mstatus = interrupt_global_save_and_disable();
        int res = esp_rom_spiflash_write(offset, buf, (int32_t)bytes);
        flash_rom_window_end(LOOP_IRQOFF_FLASH_WRITE, t0, mstatus);
        if (res != 0)
        {
            return FLASH_ROM_ERR_IO;
        }

        in += chunk;
        offset += (uint32_t)chunk;
        len -= chunk;
    }
    return FLASH_ROM_OK;
}

IRAM_ATTR int flash_rom_erase_sector(uint32_t offset)
{
    if (flash_rom_unlock() != 0)
    {
        return FLASH_ROM_ERR_IO;
    }
    uint64_t t0 = systimer_get_us();
    uint32_t mstatus = interrupt_global_save_and_disable();
    int res = esp_rom_spiflash_erase_sector((size_t)(offset / FLASH_ROM_SECTOR_SIZE));
    flash_rom_window_end(LOOP_IRQOFF_FLASH_ERASE, t0, mstatus);
    return (res == 0) ? FLASH_ROM_OK : FLASH_ROM_ERR_IO;
}
#endif
