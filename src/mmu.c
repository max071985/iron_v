/*
 * src/mmu.c
 *
 * ESP32-C6 Flash Cache & MSPI MMU Driver
 * Maps Flash XIP virtual address space (0x42000000) to physical NOR flash
 * and configures L1 instruction/data cache buses.
 */

#include "mmu.h"
#include "regs/pcr.h"
#include "regs/spi0.h"
#include "regs/extmem.h"

#if defined(__riscv)
extern void Cache_Invalidate_ICache_All(void);
#endif

#if !defined(__riscv)
static uint32_t s_mock_mmu_table[MMU_MAX_ENTRIES];
static uint32_t s_mock_mmu_page_size = 0U;
static int s_mock_cache_enabled = 0;
#endif

void mmu_cache_invalidate_all(void)
{
#if defined(__riscv)
    *EXTMEM_CACHE_SYNC_ADDR_REG = MMU_CACHE_SYNC_ALL_ADDR;
    *EXTMEM_CACHE_SYNC_SIZE_REG = MMU_CACHE_SYNC_ALL_SIZE;
    *EXTMEM_CACHE_SYNC_CTRL_REG |= EXTMEM_CACHE_SYNC_CTRL_CACHE_INVALIDATE_ENA_M;
    while (!(*EXTMEM_CACHE_SYNC_CTRL_REG & EXTMEM_CACHE_SYNC_CTRL_CACHE_SYNC_DONE_M))
    {
        /* Spin wait for cache invalidate completion */
    }
    Cache_Invalidate_ICache_All();
#endif
}

mmu_status_t mmu_map_pages(uint32_t vaddr, uint32_t paddr, uint32_t page_count)
{
    if ((vaddr < MMU_VADDR_START) || ((vaddr & (MMU_PAGE_SIZE_64KB - 1U)) != 0U))
    {
        return MMU_ERR_NOT_ALIGNED;
    }
    if ((paddr & (MMU_PAGE_SIZE_64KB - 1U)) != 0U)
    {
        return MMU_ERR_NOT_ALIGNED;
    }
    if (page_count == 0U || page_count > MMU_MAX_ENTRIES)
    {
        return MMU_ERR_INVALID_PARAM;
    }

    uint32_t start_idx = (vaddr - MMU_VADDR_START) >> MMU_PAGE_SIZE_SHIFT;
    if ((start_idx + page_count) > MMU_MAX_ENTRIES)
    {
        return MMU_ERR_OVERFLOW;
    }

    uint32_t start_phys_page = paddr >> MMU_PAGE_SIZE_SHIFT;

#if defined(__riscv)
    for (uint32_t i = 0U; i < page_count; i++)
    {
        *SPI0_SPI_MEM_MMU_ITEM_INDEX_REG = start_idx + i;
        *SPI0_SPI_MEM_MMU_ITEM_CONTENT_REG = (start_phys_page + i) | MMU_ITEM_VALID_BIT;
    }
    mmu_cache_invalidate_all();
#else
    for (uint32_t i = 0U; i < page_count; i++)
    {
        s_mock_mmu_table[start_idx + i] = (start_phys_page + i) | MMU_ITEM_VALID_BIT;
    }
#endif

    return MMU_OK;
}

mmu_status_t mmu_init(void)
{
#if defined(__riscv)
    /* 1. Enable Cache clock in PCR and cycle reset */
    *PCR_CACHE_CONF_REG |= PCR_CACHE_CONF_CACHE_CLK_EN_M;
    *PCR_CACHE_CONF_REG |= PCR_CACHE_CONF_CACHE_RST_EN_M;
    *PCR_CACHE_CONF_REG &= ~PCR_CACHE_CONF_CACHE_RST_EN_M;

    /* 2. Configure MSPI MMU page size to 64KB (mode 0) */
    *SPI0_SPI_MEM_MMU_POWER_CTRL_REG &= ~SPI0_SPI_MEM_MMU_POWER_CTRL_SPI_MMU_PAGE_SIZE_M;

    /* 3. Un-shut L1 cache buses in EXTMEM */
    *EXTMEM_L1_CACHE_CTRL_REG &= ~(EXTMEM_L1_CACHE_CTRL_L1_CACHE_SHUT_BUS0_M |
                                   EXTMEM_L1_CACHE_CTRL_L1_CACHE_SHUT_BUS1_M);
    *EXTMEM_L1_ICACHE_CTRL_REG &= ~(EXTMEM_L1_ICACHE_CTRL_L1_ICACHE_SHUT_IBUS0_M |
                                    EXTMEM_L1_ICACHE_CTRL_L1_ICACHE_SHUT_IBUS1_M |
                                    EXTMEM_L1_ICACHE_CTRL_L1_ICACHE_SHUT_IBUS2_M |
                                    EXTMEM_L1_ICACHE_CTRL_L1_ICACHE_SHUT_IBUS3_M);

    /* 4. Determine page count from linker script symbols */
    extern const uint8_t _sflash_xip[];
    extern const uint8_t _eflash_xip[];
    uint32_t flash_len = (uint32_t)(_eflash_xip - _sflash_xip);
    uint32_t page_count = (flash_len + (MMU_PAGE_SIZE_64KB - 1U)) >> MMU_PAGE_SIZE_SHIFT;
    if (page_count < MMU_DEFAULT_MAP_PAGES)
    {
        page_count = MMU_DEFAULT_MAP_PAGES;
    }
    if (page_count > MMU_MAX_ENTRIES)
    {
        page_count = MMU_MAX_ENTRIES;
    }

    /* 5. Map virtual 0x42000000 to physical flash offset 0x10000 (page 1) */
    for (uint32_t i = 0U; i < page_count; i++)
    {
        *SPI0_SPI_MEM_MMU_ITEM_INDEX_REG = i;
        *SPI0_SPI_MEM_MMU_ITEM_CONTENT_REG = (MMU_PHYS_PAGE_START + i) | MMU_ITEM_VALID_BIT;
    }

    /* 6. Invalidate I-Cache to flush any stale tag memory */
    mmu_cache_invalidate_all();
#else
    s_mock_cache_enabled = 1;
    s_mock_mmu_page_size = 0U;
    for (uint32_t i = 0U; i < MMU_DEFAULT_MAP_PAGES; i++)
    {
        s_mock_mmu_table[i] = (MMU_PHYS_PAGE_START + i) | MMU_ITEM_VALID_BIT;
    }
#endif

    return MMU_OK;
}
