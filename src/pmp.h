/*
 * src/pmp.h
 *
 * ESP32-C6 RISC-V Physical Memory Protection (PMP) & Access Permission Management (APM)
 * TRM Chapter 1 (§1.8 Physical Memory Protection) & Chapter 16 (Permission Control)
 *
 * Provides hardware-enforced memory isolation, NAPOT/TOR/NA4 address matching,
 * and peripheral/SRAM access authority attributes.
 */

#ifndef IRON_V_PMP_H
#define IRON_V_PMP_H

#include <stdint.h>
#include <stddef.h>
#include "regs/hp_apm.h"

/* PMP Configuration Constants (TRM §1.8 & RISC-V Privileged Spec) */
#define PMP_MAX_REGIONS              4U
#define PMP_CFG_REG_COUNT            1U  /* pmpcfg0 covers entries 0..3 */

/* PMP Configuration Bitfields (per 8-bit entry in pmpcfg0) */
#define PMP_CFG_R_BIT                (1U << 0)  /* Read permission */
#define PMP_CFG_W_BIT                (1U << 1)  /* Write permission */
#define PMP_CFG_X_BIT                (1U << 2)  /* Execute permission */
#define PMP_CFG_A_SHIFT              3U
#define PMP_CFG_A_MASK               (0x03U << PMP_CFG_A_SHIFT)
#define PMP_CFG_A_OFF                (0x00U << PMP_CFG_A_SHIFT)  /* Disabled / Null region */
#define PMP_CFG_A_TOR                (0x01U << PMP_CFG_A_SHIFT)  /* Top of Range */
#define PMP_CFG_A_NA4                (0x02U << PMP_CFG_A_SHIFT)  /* Naturally aligned 4-byte */
#define PMP_CFG_A_NAPOT              (0x03U << PMP_CFG_A_SHIFT)  /* Naturally aligned power-of-two (>=8B) */
#define PMP_CFG_L_BIT                (1U << 7)  /* Lock bit (applies to M-mode, locked till reset) */

#define PMP_CFG_ENTRY_WIDTH          8U
#define PMP_CFG_ENTRY_MASK           0xFFU
#define PMP_ADDR_SHIFT               2U
#define PMP_NAPOT_MASK_SHIFT         3U
#define PMP_NAPOT_MIN_LEN            8U
#define PMP_NA4_LEN                  4U
#define PMP_WORD_ALIGN_MASK          0x03U
#define PMP_ALL_BITS_MASK            0xFFFFFFFFU
#define PMP_NAPOT_MAX_TRAILING_ONES  29U
#define PMP_NAPOT_MAX_BITS           32U

/* Address Matching Modes */
typedef enum {
    PMP_ADDR_MODE_AUTO  = 0,  /* Auto-detect NAPOT vs NA4 vs TOR based on length & alignment */
    PMP_ADDR_MODE_TOR   = 1,  /* Top of Range */
    PMP_ADDR_MODE_NA4   = 2,  /* Naturally aligned 4-byte */
    PMP_ADDR_MODE_NAPOT = 3,  /* Naturally aligned power-of-two (>= 8 bytes) */
    PMP_ADDR_MODE_OFF   = 4   /* Disabled entry */
} pmp_addr_mode_t;

/* Status & Error Codes */
#define PMP_OK                       0
#define PMP_ERR_INVALID_REGION       (-1)
#define PMP_ERR_INVALID_ALIGN        (-2)
#define PMP_ERR_LOCKED               (-3)
#define PMP_ERR_NULL_PTR             (-4)
#define PMP_ERR_INVALID_LEN          (-5)
#define PMP_ERR_INVALID_ADDR         (-6)

/* Concrete PMP Region Configuration (per Roadmap §3.4) */
typedef struct {
    uint32_t region_idx;
    uint32_t start_addr;
    uint32_t length;
    uint8_t  read_allow;
    uint8_t  write_allow;
    uint8_t  execute_allow;
    uint8_t  lock;
    uint8_t  addr_mode; /* Optional / auto-calculated if 0 */
} pmp_region_cfg_t;

/* HP_APM Constants (TRM Chapter 16 & src/regs/hp_apm.h) */
#define HP_APM_BASE_ADDR             0x60099000U
#define APM_MAX_REGIONS              16U
#define APM_MAX_MASTERS              4U

#define APM_PMS_X_BIT                (1U << 0)  /* Execute permission */
#define APM_PMS_W_BIT                (1U << 1)  /* Write permission */
#define APM_PMS_R_BIT                (1U << 2)  /* Read permission */

#define APM_PMS_MODE_SHIFT(m)        ((uint32_t)(m) * 4U)
#define APM_PMS_ATTR_MASK(m)         (0x07U << APM_PMS_MODE_SHIFT(m))

#define APM_CLOCK_GATE_CLK_EN_BIT    (1U << 0)
#define APM_REGION0_FILTER_EN_BIT    (1U << 0)
#define APM_M_STATUS_CLR_BIT         (1U << 0)

#define APM_OK                       0
#define APM_ERR_INVALID_REGION       (-1)
#define APM_ERR_INVALID_MASTER       (-2)
#define APM_ERR_NULL_PTR             (-3)
#define APM_ERR_INVALID_ADDR         (-4)
#define APM_ERR_RESERVED_REGION      (-5)

/* Parameterized Register Accessor Macros for HP_APM (AGENTS.md Compliance) */
#define HP_APM_REGION_FILTER_ENABLE_REG   ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0x00U))
#define HP_APM_REGION_START_REG(r)        ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0x04U + ((uint32_t)(r) * 0x0CU)))
#define HP_APM_REGION_END_REG(r)          ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0x08U + ((uint32_t)(r) * 0x0CU)))
#define HP_APM_REGION_PMS_ATTR_REG(r)     ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0x0CU + ((uint32_t)(r) * 0x0CU)))
#define HP_APM_FUNC_CONTROL_REG           ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0xC4U))
#define HP_APM_M_STATUS_REG(m)            ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0xC8U + ((uint32_t)(m) * 0x10U)))
#define HP_APM_M_STATUS_CLR_REG(m)        ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0xCCU + ((uint32_t)(m) * 0x10U)))
#define HP_APM_M_EXCEPTION_INFO0_REG(m)   ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0xD0U + ((uint32_t)(m) * 0x10U)))
#define HP_APM_M_EXCEPTION_INFO1_REG(m)   ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0xD4U + ((uint32_t)(m) * 0x10U)))
#define HP_APM_CLK_GATE_REG               ((volatile uint32_t *)(uintptr_t)(HP_APM_BASE_ADDR + 0x10CU))

/* High-Performance Trusted Execution Environment (HP_TEE) Register Accessors */
#define HP_TEE_BASE_ADDR                  0x60097000U
#define HP_TEE_M_MODE_CTRL_REG(m)         ((volatile uint32_t *)(uintptr_t)(HP_TEE_BASE_ADDR + ((uint32_t)(m) * 4U)))
#define HP_TEE_CLOCK_GATE_REG             ((volatile uint32_t *)(uintptr_t)(HP_TEE_BASE_ADDR + 0x80U))
#define HP_TEE_MODE_TEE                   0U
#define HP_TEE_MAX_MASTERS                32U
#define APM_REGION_ATTR_ALL_PERM          0xFFFFFFFFU

/* Concrete HP_APM Region Configuration */
typedef struct {
    uint32_t region_idx;
    uint32_t start_addr;
    uint32_t end_addr;
    uint8_t  read_allow;
    uint8_t  write_allow;
    uint8_t  execute_allow;
    uint8_t  filter_enable;
} apm_region_cfg_t;

/* Concrete HP_APM Master Exception Info */
typedef struct {
    uint32_t exception_status;
    uint32_t exception_region;
    uint32_t exception_mode;
    uint32_t exception_id;
    uint32_t exception_addr;
} apm_exception_info_t;

/* Unified Telemetry Structure */
typedef struct {
    uint32_t pmp_active_count;
    uint32_t apm_active_count;
    uint32_t pmpcfg0_val;
    uint32_t pmpaddr_vals[PMP_MAX_REGIONS];
    uint32_t apm_filter_en_val;
    uint32_t apm_func_ctrl_val;
} pmp_telemetry_t;

/* PMP Subsystem API */
int pmp_init(void);
int pmp_set_region(const pmp_region_cfg_t *cfg);
int pmp_get_region(uint32_t region_idx, pmp_region_cfg_t *cfg);
int pmp_disable_region(uint32_t region_idx);
uint32_t pmp_read_cfg(uint32_t cfg_idx);
void pmp_write_cfg(uint32_t cfg_idx, uint32_t val);
uint32_t pmp_read_addr(uint32_t region_idx);
void pmp_write_addr(uint32_t region_idx, uint32_t val);
int pmp_calc_napot(uint32_t base, uint32_t len, uint32_t *pmpaddr);
int pmp_decode_napot(uint32_t pmpaddr, uint32_t *base, uint32_t *len);

/* HP_APM Subsystem API */
int apm_init(void);
int apm_set_region(const apm_region_cfg_t *cfg);
int apm_get_region(uint32_t region_idx, apm_region_cfg_t *cfg);
int apm_disable_region(uint32_t region_idx);
int apm_enable_master(uint32_t master_idx, int enable);
int apm_get_exception_info(uint32_t master_idx, apm_exception_info_t *info);
int apm_clear_exception(uint32_t master_idx);

/* Unified Telemetry */
void pmp_get_telemetry(pmp_telemetry_t *tel);

#endif /* IRON_V_PMP_H */
