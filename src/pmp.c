/*
 * src/pmp.c
 *
 * ESP32-C6 RISC-V Physical Memory Protection (PMP) & Access Permission Management (APM)
 * TRM Chapter 1 (§1.8 Physical Memory Protection) & Chapter 16 (Permission Control)
 *
 * Hardware memory isolation driver providing NAPOT, TOR, and NA4 address matching
 * via RISC-V privileged CSRs (pmpcfg0, pmpaddr0..3) and APM peripheral authority gating.
 */

#include "pmp.h"

/* Real Hardware vs Host Unit-Testing Emulation Abstraction */
#if defined(__riscv)

static inline uint32_t pmp_csr_read_pmpcfg0(void)
{
    uint32_t val;
    asm volatile("csrr %0, pmpcfg0" : "=r"(val));
    return val;
}

static inline void pmp_csr_write_pmpcfg0(uint32_t val)
{
    asm volatile("csrw pmpcfg0, %0" :: "r"(val) : "memory");
}

static inline uint32_t pmp_csr_read_pmpaddr(uint32_t idx)
{
    uint32_t val = 0U;
    switch (idx)
    {
    case 0U: asm volatile("csrr %0, pmpaddr0" : "=r"(val)); break;
    case 1U: asm volatile("csrr %0, pmpaddr1" : "=r"(val)); break;
    case 2U: asm volatile("csrr %0, pmpaddr2" : "=r"(val)); break;
    case 3U: asm volatile("csrr %0, pmpaddr3" : "=r"(val)); break;
    default: break;
    }
    return val;
}

static inline void pmp_csr_write_pmpaddr(uint32_t idx, uint32_t val)
{
    switch (idx)
    {
    case 0U: asm volatile("csrw pmpaddr0, %0" :: "r"(val) : "memory"); break;
    case 1U: asm volatile("csrw pmpaddr1, %0" :: "r"(val) : "memory"); break;
    case 2U: asm volatile("csrw pmpaddr2, %0" :: "r"(val) : "memory"); break;
    case 3U: asm volatile("csrw pmpaddr3, %0" :: "r"(val) : "memory"); break;
    default: break;
    }
}

#define APM_REG_RD(reg_ptr)         (*(reg_ptr))
#define APM_REG_WR(reg_ptr, val)    (*(reg_ptr) = (val))

#else /* !defined(__riscv) - Host Mock for Unit Testing */

static uint32_t s_mock_pmpcfg0 = 0U;
static uint32_t s_mock_pmpaddr[PMP_MAX_REGIONS] = {0U, 0U, 0U, 0U};
static uint32_t s_mock_hp_apm_mem[512];

static inline uint32_t pmp_csr_read_pmpcfg0(void)
{
    return s_mock_pmpcfg0;
}

static inline void pmp_csr_write_pmpcfg0(uint32_t val)
{
    s_mock_pmpcfg0 = val;
}

static inline uint32_t pmp_csr_read_pmpaddr(uint32_t idx)
{
    return (idx < PMP_MAX_REGIONS) ? s_mock_pmpaddr[idx] : 0U;
}

static inline void pmp_csr_write_pmpaddr(uint32_t idx, uint32_t val)
{
    if (idx < PMP_MAX_REGIONS)
    {
        s_mock_pmpaddr[idx] = val;
    }
}

static inline uint32_t *apm_get_mock_ptr(volatile uint32_t *reg_ptr)
{
    uintptr_t addr = (uintptr_t)reg_ptr;
    uint32_t offset = (uint32_t)(addr - HP_APM_BASE_ADDR);
    uint32_t word_idx = (offset >> 2U) % 512U;
    return &s_mock_hp_apm_mem[word_idx];
}

#define APM_REG_RD(reg_ptr)         (*apm_get_mock_ptr(reg_ptr))
#define APM_REG_WR(reg_ptr, val)    (*apm_get_mock_ptr(reg_ptr) = (val))

#endif /* defined(__riscv) */

/* ========================================================================= */
/* RISC-V PMP Hardware Driver Implementation                                 */
/* ========================================================================= */

int pmp_init(void)
{
    uint32_t cur_cfg = pmp_csr_read_pmpcfg0();

    /* Disable any unlocked regions on initialization */
    for (uint32_t i = 0U; i < PMP_MAX_REGIONS; i++)
    {
        uint32_t shift = i * PMP_CFG_ENTRY_WIDTH;
        uint8_t entry = (uint8_t)((cur_cfg >> shift) & PMP_CFG_ENTRY_MASK);
        if ((entry & PMP_CFG_L_BIT) == 0U)
        {
            cur_cfg &= ~(PMP_CFG_ENTRY_MASK << shift);
            pmp_csr_write_pmpaddr(i, 0U);
        }
    }
    pmp_csr_write_pmpcfg0(cur_cfg);

    return PMP_OK;
}

int pmp_calc_napot(uint32_t base, uint32_t len, uint32_t *pmpaddr)
{
    if (pmpaddr == NULL)
    {
        return PMP_ERR_NULL_PTR;
    }
    if (len < PMP_NAPOT_MIN_LEN)
    {
        return PMP_ERR_INVALID_LEN;
    }
    /* Length must be a power of 2 */
    if ((len & (len - 1U)) != 0U)
    {
        return PMP_ERR_INVALID_LEN;
    }
    /* Base address must be naturally aligned to length */
    if ((base & (len - 1U)) != 0U)
    {
        return PMP_ERR_INVALID_ALIGN;
    }

    *pmpaddr = (base >> PMP_ADDR_SHIFT) | ((len - 1U) >> PMP_NAPOT_MASK_SHIFT);
    return PMP_OK;
}

int pmp_decode_napot(uint32_t pmpaddr, uint32_t *base, uint32_t *len)
{
    if (base == NULL || len == NULL)
    {
        return PMP_ERR_NULL_PTR;
    }

    uint32_t trailing_ones;
    if (pmpaddr == 0xFFFFFFFFU)
    {
        trailing_ones = 32U;
    }
    else
    {
        trailing_ones = (uint32_t)__builtin_ctz(~pmpaddr);
    }

    if (trailing_ones >= 29U)
    {
        *len = 0xFFFFFFFFU;
    }
    else
    {
        *len = 1U << (trailing_ones + PMP_NAPOT_MASK_SHIFT);
    }

    uint32_t mask = (trailing_ones >= 32U) ? 0xFFFFFFFFU : ((1U << trailing_ones) - 1U);
    *base = (pmpaddr & ~mask) << PMP_ADDR_SHIFT;
    return PMP_OK;
}

int pmp_set_region(const pmp_region_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return PMP_ERR_NULL_PTR;
    }
    if (cfg->region_idx >= PMP_MAX_REGIONS)
    {
        return PMP_ERR_INVALID_REGION;
    }

    /* Check if target region is currently locked */
    uint32_t pmpcfg0 = pmp_csr_read_pmpcfg0();
    uint32_t shift = cfg->region_idx * PMP_CFG_ENTRY_WIDTH;
    uint8_t cur_entry = (uint8_t)((pmpcfg0 >> shift) & PMP_CFG_ENTRY_MASK);
    if ((cur_entry & PMP_CFG_L_BIT) != 0U)
    {
        return PMP_ERR_LOCKED;
    }

    /* Resolve address matching mode */
    pmp_addr_mode_t mode = (pmp_addr_mode_t)cfg->addr_mode;
    if (mode == PMP_ADDR_MODE_AUTO)
    {
        if (cfg->length == 0U)
        {
            mode = PMP_ADDR_MODE_OFF;
        }
        else if (cfg->length == PMP_NA4_LEN && ((cfg->start_addr & 3U) == 0U))
        {
            mode = PMP_ADDR_MODE_NA4;
        }
        else if (cfg->length >= PMP_NAPOT_MIN_LEN &&
                 ((cfg->length & (cfg->length - 1U)) == 0U) &&
                 ((cfg->start_addr & (cfg->length - 1U)) == 0U))
        {
            mode = PMP_ADDR_MODE_NAPOT;
        }
        else
        {
            mode = PMP_ADDR_MODE_TOR;
        }
    }

    uint32_t pmpaddr = 0U;
    uint8_t a_field = 0U;

    switch (mode)
    {
    case PMP_ADDR_MODE_OFF:
        pmpaddr = 0U;
        a_field = PMP_CFG_A_OFF;
        break;

    case PMP_ADDR_MODE_TOR:
        if ((cfg->start_addr & 3U) != 0U)
        {
            return PMP_ERR_INVALID_ALIGN;
        }
        pmpaddr = (cfg->start_addr + cfg->length) >> PMP_ADDR_SHIFT;
        a_field = PMP_CFG_A_TOR;
        break;

    case PMP_ADDR_MODE_NA4:
        if ((cfg->start_addr & 3U) != 0U)
        {
            return PMP_ERR_INVALID_ALIGN;
        }
        pmpaddr = cfg->start_addr >> PMP_ADDR_SHIFT;
        a_field = PMP_CFG_A_NA4;
        break;

    case PMP_ADDR_MODE_NAPOT:
    {
        int ret = pmp_calc_napot(cfg->start_addr, cfg->length, &pmpaddr);
        if (ret != PMP_OK)
        {
            return ret;
        }
        a_field = PMP_CFG_A_NAPOT;
        break;
    }

    default:
        return PMP_ERR_INVALID_REGION;
    }

    /* Build entry configuration byte */
    uint8_t entry_cfg = a_field;
    if (cfg->read_allow)    entry_cfg |= PMP_CFG_R_BIT;
    if (cfg->write_allow)   entry_cfg |= PMP_CFG_W_BIT;
    if (cfg->execute_allow) entry_cfg |= PMP_CFG_X_BIT;
    if (cfg->lock)          entry_cfg |= PMP_CFG_L_BIT;

    /* Safe sequential update:
     * 1. Disable entry by writing A = OFF to pmpcfg0
     * 2. Write address to pmpaddr
     * 3. Enable entry with requested mode and permissions
     */
    pmpcfg0 &= ~(PMP_CFG_ENTRY_MASK << shift);
    pmp_csr_write_pmpcfg0(pmpcfg0);

    pmp_csr_write_pmpaddr(cfg->region_idx, pmpaddr);

    pmpcfg0 |= ((uint32_t)entry_cfg << shift);
    pmp_csr_write_pmpcfg0(pmpcfg0);

    return PMP_OK;
}

int pmp_get_region(uint32_t region_idx, pmp_region_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return PMP_ERR_NULL_PTR;
    }
    if (region_idx >= PMP_MAX_REGIONS)
    {
        return PMP_ERR_INVALID_REGION;
    }

    uint32_t pmpcfg0 = pmp_csr_read_pmpcfg0();
    uint32_t shift = region_idx * PMP_CFG_ENTRY_WIDTH;
    uint8_t entry_cfg = (uint8_t)((pmpcfg0 >> shift) & PMP_CFG_ENTRY_MASK);
    uint32_t pmpaddr = pmp_csr_read_pmpaddr(region_idx);

    cfg->region_idx = region_idx;
    cfg->read_allow = (entry_cfg & PMP_CFG_R_BIT) ? 1U : 0U;
    cfg->write_allow = (entry_cfg & PMP_CFG_W_BIT) ? 1U : 0U;
    cfg->execute_allow = (entry_cfg & PMP_CFG_X_BIT) ? 1U : 0U;
    cfg->lock = (entry_cfg & PMP_CFG_L_BIT) ? 1U : 0U;

    uint8_t a = entry_cfg & PMP_CFG_A_MASK;
    if (a == PMP_CFG_A_OFF)
    {
        cfg->start_addr = 0U;
        cfg->length = 0U;
        cfg->addr_mode = (uint8_t)PMP_ADDR_MODE_OFF;
    }
    else if (a == PMP_CFG_A_TOR)
    {
        uint32_t top = pmpaddr << PMP_ADDR_SHIFT;
        uint32_t bottom = (region_idx == 0U) ? 0U : (pmp_csr_read_pmpaddr(region_idx - 1U) << PMP_ADDR_SHIFT);
        cfg->start_addr = bottom;
        cfg->length = (top >= bottom) ? (top - bottom) : 0U;
        cfg->addr_mode = (uint8_t)PMP_ADDR_MODE_TOR;
    }
    else if (a == PMP_CFG_A_NA4)
    {
        cfg->start_addr = pmpaddr << PMP_ADDR_SHIFT;
        cfg->length = PMP_NA4_LEN;
        cfg->addr_mode = (uint8_t)PMP_ADDR_MODE_NA4;
    }
    else if (a == PMP_CFG_A_NAPOT)
    {
        uint32_t base = 0U;
        uint32_t len = 0U;
        pmp_decode_napot(pmpaddr, &base, &len);
        cfg->start_addr = base;
        cfg->length = len;
        cfg->addr_mode = (uint8_t)PMP_ADDR_MODE_NAPOT;
    }

    return PMP_OK;
}

int pmp_disable_region(uint32_t region_idx)
{
    if (region_idx >= PMP_MAX_REGIONS)
    {
        return PMP_ERR_INVALID_REGION;
    }

    uint32_t pmpcfg0 = pmp_csr_read_pmpcfg0();
    uint32_t shift = region_idx * PMP_CFG_ENTRY_WIDTH;
    uint8_t cur_entry = (uint8_t)((pmpcfg0 >> shift) & PMP_CFG_ENTRY_MASK);
    if ((cur_entry & PMP_CFG_L_BIT) != 0U)
    {
        return PMP_ERR_LOCKED;
    }

    pmpcfg0 &= ~(PMP_CFG_ENTRY_MASK << shift);
    pmp_csr_write_pmpcfg0(pmpcfg0);
    pmp_csr_write_pmpaddr(region_idx, 0U);

    return PMP_OK;
}

uint32_t pmp_read_cfg(uint32_t cfg_idx)
{
    if (cfg_idx == 0U)
    {
        return pmp_csr_read_pmpcfg0();
    }
    return 0U;
}

void pmp_write_cfg(uint32_t cfg_idx, uint32_t val)
{
    if (cfg_idx == 0U)
    {
        pmp_csr_write_pmpcfg0(val);
    }
}

uint32_t pmp_read_addr(uint32_t region_idx)
{
    return pmp_csr_read_pmpaddr(region_idx);
}

void pmp_write_addr(uint32_t region_idx, uint32_t val)
{
    pmp_csr_write_pmpaddr(region_idx, val);
}

/* ========================================================================= */
/* HP_APM Peripheral Driver Implementation                                   */
/* ========================================================================= */

int apm_init(void)
{
    /* 1. Enable HP_APM clock gating */
    uint32_t clk_gate = APM_REG_RD(HP_APM_CLK_GATE_REG);
    clk_gate |= HP_APM_CLOCK_GATE_CLK_EN_M;
    APM_REG_WR(HP_APM_CLK_GATE_REG, clk_gate);

    /* 2. Preserve Region 0 default boot mapping (0x0 - 0xFFFFFFFF) and clear dynamic filters 1..15 */
    uint32_t filter_en = APM_REG_RD(HP_APM_REGION_FILTER_ENABLE_REG);
    APM_REG_WR(HP_APM_REGION_FILTER_ENABLE_REG, filter_en & 0x00000001U);

    return APM_OK;
}

int apm_set_region(const apm_region_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return APM_ERR_NULL_PTR;
    }
    if (cfg->region_idx >= APM_MAX_REGIONS)
    {
        return APM_ERR_INVALID_REGION;
    }
    if (cfg->start_addr > cfg->end_addr)
    {
        return APM_ERR_INVALID_ADDR;
    }

    /* Disable filter bit for this region during update */
    uint32_t filter_en = APM_REG_RD(HP_APM_REGION_FILTER_ENABLE_REG);
    filter_en &= ~(1U << cfg->region_idx);
    APM_REG_WR(HP_APM_REGION_FILTER_ENABLE_REG, filter_en);

    /* Set start and end address boundaries */
    APM_REG_WR(HP_APM_REGION_START_REG(cfg->region_idx), cfg->start_addr);
    APM_REG_WR(HP_APM_REGION_END_REG(cfg->region_idx), cfg->end_addr);

    /* Build PMS authority attributes for Mode 0 (Root/Secure) */
    uint32_t pms = 0U;
    if (cfg->execute_allow) pms |= APM_PMS_X_BIT;
    if (cfg->write_allow)   pms |= APM_PMS_W_BIT;
    if (cfg->read_allow)    pms |= APM_PMS_R_BIT;
    APM_REG_WR(HP_APM_REGION_PMS_ATTR_REG(cfg->region_idx), pms);

    /* Enable region filter if requested */
    if (cfg->filter_enable)
    {
        filter_en |= (1U << cfg->region_idx);
        APM_REG_WR(HP_APM_REGION_FILTER_ENABLE_REG, filter_en);
    }

    return APM_OK;
}

int apm_get_region(uint32_t region_idx, apm_region_cfg_t *cfg)
{
    if (cfg == NULL)
    {
        return APM_ERR_NULL_PTR;
    }
    if (region_idx >= APM_MAX_REGIONS)
    {
        return APM_ERR_INVALID_REGION;
    }

    cfg->region_idx = region_idx;
    cfg->start_addr = APM_REG_RD(HP_APM_REGION_START_REG(region_idx));
    cfg->end_addr = APM_REG_RD(HP_APM_REGION_END_REG(region_idx));

    uint32_t pms = APM_REG_RD(HP_APM_REGION_PMS_ATTR_REG(region_idx));
    cfg->execute_allow = (pms & APM_PMS_X_BIT) ? 1U : 0U;
    cfg->write_allow = (pms & APM_PMS_W_BIT) ? 1U : 0U;
    cfg->read_allow = (pms & APM_PMS_R_BIT) ? 1U : 0U;

    uint32_t filter_en = APM_REG_RD(HP_APM_REGION_FILTER_ENABLE_REG);
    cfg->filter_enable = (filter_en & (1U << region_idx)) ? 1U : 0U;

    return APM_OK;
}

int apm_disable_region(uint32_t region_idx)
{
    if (region_idx >= APM_MAX_REGIONS)
    {
        return APM_ERR_INVALID_REGION;
    }

    uint32_t filter_en = APM_REG_RD(HP_APM_REGION_FILTER_ENABLE_REG);
    filter_en &= ~(1U << region_idx);
    APM_REG_WR(HP_APM_REGION_FILTER_ENABLE_REG, filter_en);

    APM_REG_WR(HP_APM_REGION_START_REG(region_idx), 0U);
    APM_REG_WR(HP_APM_REGION_END_REG(region_idx), 0U);
    APM_REG_WR(HP_APM_REGION_PMS_ATTR_REG(region_idx), 0U);

    return APM_OK;
}

int apm_enable_master(uint32_t master_idx, int enable)
{
    if (master_idx >= APM_MAX_MASTERS)
    {
        return APM_ERR_INVALID_MASTER;
    }

    uint32_t ctrl = APM_REG_RD(HP_APM_FUNC_CONTROL_REG);
    if (enable)
    {
        ctrl |= (1U << master_idx);
    }
    else
    {
        ctrl &= ~(1U << master_idx);
    }
    APM_REG_WR(HP_APM_FUNC_CONTROL_REG, ctrl);

    return APM_OK;
}

int apm_get_exception_info(uint32_t master_idx, apm_exception_info_t *info)
{
    if (info == NULL)
    {
        return APM_ERR_NULL_PTR;
    }
    if (master_idx >= APM_MAX_MASTERS)
    {
        return APM_ERR_INVALID_MASTER;
    }

    info->exception_status = APM_REG_RD(HP_APM_M_STATUS_REG(master_idx));
    uint32_t i0 = APM_REG_RD(HP_APM_M_EXCEPTION_INFO0_REG(master_idx));
    info->exception_region = i0 & HP_APM_M0_EXCEPTION_INFO0_M0_EXCEPTION_REGION_M;
    info->exception_mode = (i0 & HP_APM_M0_EXCEPTION_INFO0_M0_EXCEPTION_MODE_M) >> HP_APM_M0_EXCEPTION_INFO0_M0_EXCEPTION_MODE_S;
    info->exception_id = (i0 & HP_APM_M0_EXCEPTION_INFO0_M0_EXCEPTION_ID_M) >> HP_APM_M0_EXCEPTION_INFO0_M0_EXCEPTION_ID_S;
    info->exception_addr = APM_REG_RD(HP_APM_M_EXCEPTION_INFO1_REG(master_idx));

    return APM_OK;
}

int apm_clear_exception(uint32_t master_idx)
{
    if (master_idx >= APM_MAX_MASTERS)
    {
        return APM_ERR_INVALID_MASTER;
    }
    APM_REG_WR(HP_APM_M_STATUS_CLR_REG(master_idx), 1U);
    return APM_OK;
}

/* ========================================================================= */
/* Unified PMP & APM Telemetry                                               */
/* ========================================================================= */

void pmp_get_telemetry(pmp_telemetry_t *tel)
{
    if (tel == NULL)
    {
        return;
    }

    tel->pmpcfg0_val = pmp_csr_read_pmpcfg0();
    uint32_t pmp_count = 0U;
    for (uint32_t i = 0U; i < PMP_MAX_REGIONS; i++)
    {
        tel->pmpaddr_vals[i] = pmp_csr_read_pmpaddr(i);
        uint8_t cfg = (uint8_t)((tel->pmpcfg0_val >> (i * PMP_CFG_ENTRY_WIDTH)) & PMP_CFG_ENTRY_MASK);
        if ((cfg & PMP_CFG_A_MASK) != PMP_CFG_A_OFF)
        {
            pmp_count++;
        }
    }
    tel->pmp_active_count = pmp_count;

    tel->apm_filter_en_val = APM_REG_RD(HP_APM_REGION_FILTER_ENABLE_REG);
    tel->apm_func_ctrl_val = APM_REG_RD(HP_APM_FUNC_CONTROL_REG);

    uint32_t apm_count = 0U;
    for (uint32_t i = 0U; i < APM_MAX_REGIONS; i++)
    {
        if ((tel->apm_filter_en_val & (1U << i)) != 0U)
        {
            apm_count++;
        }
    }
    tel->apm_active_count = apm_count;
}
