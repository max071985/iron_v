/*
 * src/mmu.h
 *
 * ESP32-C6 Flash Cache & MSPI MMU Controller Interface
 * Maps external NOR flash into the CPU 32-bit virtual address space (0x42000000)
 * and configures L1 instruction/data cache buses.
 */

#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include <stddef.h>

#define MMU_PAGE_SIZE_64KB          (0x00010000U)
#define MMU_PAGE_SIZE_SHIFT         (16U)
#define MMU_VADDR_START             (0x42000000U)
#define MMU_PHYS_PAGE_START         (1U) /* Flash offset 0x10000 / 64KB = page 1 */
#define MMU_ITEM_VALID_BIT          (0x00000200U)
#define MMU_CACHE_SYNC_ALL_ADDR     (0x00000000U)
#define MMU_CACHE_SYNC_ALL_SIZE     (0x00FFFFFFU)
#define MMU_DEFAULT_MAP_PAGES       (16U) /* Minimum 1MB mapped (16 * 64KB) */
#define MMU_MAX_ENTRIES             (128U) /* Up to 8MB Flash mapped */

/* Status return codes */
typedef enum {
    MMU_OK = 0,
    MMU_ERR_INVALID_PARAM = -1,
    MMU_ERR_NOT_ALIGNED = -2,
    MMU_ERR_OVERFLOW = -3
} mmu_status_t;

/* Public API */
mmu_status_t mmu_init(void);
mmu_status_t mmu_map_pages(uint32_t vaddr, uint32_t paddr, uint32_t page_count);
void mmu_cache_invalidate_all(void);

#endif /* MMU_H */
