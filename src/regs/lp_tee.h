/*
 * LP_TEE.h
 * Low-power Trusted Execution Environment
 * Base Address: 0x600B3400
 */

#ifndef LP_TEE_H
#define LP_TEE_H

#include <stdint.h>

#define LP_TEE_BASE 0x600B3400

// Tee mode control register
#define LP_TEE_M0_MODE_CTRL_REG ((volatile uint32_t *)(LP_TEE_BASE + 0x0))
#define LP_TEE_M0_MODE_CTRL_M0_MODE_M (0x00000003U)
#define LP_TEE_M0_MODE_CTRL_M0_MODE_S (0)
#define LP_TEE_M0_MODE_CTRL_M0_MODE_V(v) (((v) << 0) & 0x00000003U)

// Clock gating register
#define LP_TEE_CLOCK_GATE_REG ((volatile uint32_t *)(LP_TEE_BASE + 0x4))
#define LP_TEE_CLOCK_GATE_CLK_EN_M (0x00000001U)
#define LP_TEE_CLOCK_GATE_CLK_EN_S (0)
#define LP_TEE_CLOCK_GATE_CLK_EN_V(v) (((v) << 0) & 0x00000001U)

// need_des
#define LP_TEE_FORCE_ACC_HP_REG ((volatile uint32_t *)(LP_TEE_BASE + 0x90))
#define LP_TEE_FORCE_ACC_HP_LP_AON_FORCE_ACC_HPMEM_EN_M (0x00000001U)
#define LP_TEE_FORCE_ACC_HP_LP_AON_FORCE_ACC_HPMEM_EN_S (0)
#define LP_TEE_FORCE_ACC_HP_LP_AON_FORCE_ACC_HPMEM_EN_V(v) (((v) << 0) & 0x00000001U)

// Version register
#define LP_TEE_DATE_REG ((volatile uint32_t *)(LP_TEE_BASE + 0xFC))
#define LP_TEE_DATE_DATE_M (0x0FFFFFFFU)
#define LP_TEE_DATE_DATE_S (0)
#define LP_TEE_DATE_DATE_V(v) (((v) << 0) & 0x0FFFFFFFU)

#endif // LP_TEE_H
