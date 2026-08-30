/*
 * ECC.h
 * ECC (ECC Hardware Accelerator)
 * Base Address: 0x6008B000
 */

#ifndef ECC_H
#define ECC_H

#include <stdint.h>

#define ECC_BASE 0x6008B000

// ECC interrupt raw register, valid in level.
#define ECC_MULT_INT_RAW_REG ((volatile uint32_t *)(ECC_BASE + 0xC))
#define ECC_MULT_INT_RAW_CALC_DONE_INT_RAW_M (0x00000001U)
#define ECC_MULT_INT_RAW_CALC_DONE_INT_RAW_S (0)
#define ECC_MULT_INT_RAW_CALC_DONE_INT_RAW_V(v) (((v) << 0) & 0x00000001U)

// ECC interrupt status register.
#define ECC_MULT_INT_ST_REG ((volatile uint32_t *)(ECC_BASE + 0x10))
#define ECC_MULT_INT_ST_CALC_DONE_INT_ST_M (0x00000001U)
#define ECC_MULT_INT_ST_CALC_DONE_INT_ST_S (0)
#define ECC_MULT_INT_ST_CALC_DONE_INT_ST_V(v) (((v) << 0) & 0x00000001U)

// ECC interrupt enable register.
#define ECC_MULT_INT_ENA_REG ((volatile uint32_t *)(ECC_BASE + 0x14))
#define ECC_MULT_INT_ENA_CALC_DONE_INT_ENA_M (0x00000001U)
#define ECC_MULT_INT_ENA_CALC_DONE_INT_ENA_S (0)
#define ECC_MULT_INT_ENA_CALC_DONE_INT_ENA_V(v) (((v) << 0) & 0x00000001U)

// ECC interrupt clear register.
#define ECC_MULT_INT_CLR_REG ((volatile uint32_t *)(ECC_BASE + 0x18))
#define ECC_MULT_INT_CLR_CALC_DONE_INT_CLR_M (0x00000001U)
#define ECC_MULT_INT_CLR_CALC_DONE_INT_CLR_S (0)
#define ECC_MULT_INT_CLR_CALC_DONE_INT_CLR_V(v) (((v) << 0) & 0x00000001U)

// ECC configure register
#define ECC_MULT_CONF_REG ((volatile uint32_t *)(ECC_BASE + 0x1C))
#define ECC_MULT_CONF_START_M (0x00000001U)
#define ECC_MULT_CONF_START_S (0)
#define ECC_MULT_CONF_START_V(v) (((v) << 0) & 0x00000001U)
#define ECC_MULT_CONF_RESET_M (0x00000002U)
#define ECC_MULT_CONF_RESET_S (1)
#define ECC_MULT_CONF_RESET_V(v) (((v) << 1) & 0x00000002U)
#define ECC_MULT_CONF_KEY_LENGTH_M (0x00000004U)
#define ECC_MULT_CONF_KEY_LENGTH_S (2)
#define ECC_MULT_CONF_KEY_LENGTH_V(v) (((v) << 2) & 0x00000004U)
#define ECC_MULT_CONF_SECURITY_MODE_M (0x00000008U)
#define ECC_MULT_CONF_SECURITY_MODE_S (3)
#define ECC_MULT_CONF_SECURITY_MODE_V(v) (((v) << 3) & 0x00000008U)
#define ECC_MULT_CONF_CLK_EN_M (0x00000010U)
#define ECC_MULT_CONF_CLK_EN_S (4)
#define ECC_MULT_CONF_CLK_EN_V(v) (((v) << 4) & 0x00000010U)
#define ECC_MULT_CONF_WORK_MODE_M (0x000000E0U)
#define ECC_MULT_CONF_WORK_MODE_S (5)
#define ECC_MULT_CONF_WORK_MODE_V(v) (((v) << 5) & 0x000000E0U)
#define ECC_MULT_CONF_VERIFICATION_RESULT_M (0x00000100U)
#define ECC_MULT_CONF_VERIFICATION_RESULT_S (8)
#define ECC_MULT_CONF_VERIFICATION_RESULT_V(v) (((v) << 8) & 0x00000100U)
#define ECC_MULT_CONF_MEM_CLOCK_GATE_FORCE_ON_M (0x80000000U)
#define ECC_MULT_CONF_MEM_CLOCK_GATE_FORCE_ON_S (31)
#define ECC_MULT_CONF_MEM_CLOCK_GATE_FORCE_ON_V(v) (((v) << 31) & 0x80000000U)

// Version control register
#define ECC_MULT_DATE_REG ((volatile uint32_t *)(ECC_BASE + 0xFC))
#define ECC_MULT_DATE_DATE_M (0x0FFFFFFFU)
#define ECC_MULT_DATE_DATE_S (0)
#define ECC_MULT_DATE_DATE_V(v) (((v) << 0) & 0x0FFFFFFFU)

// The memory that stores k.
#define ECC_K_MEM_REG ((volatile uint32_t *)(ECC_BASE + 0x100))

// The memory that stores Px.
#define ECC_PX_MEM_REG ((volatile uint32_t *)(ECC_BASE + 0x120))

// The memory that stores Py.
#define ECC_PY_MEM_REG ((volatile uint32_t *)(ECC_BASE + 0x140))

#endif // ECC_H
