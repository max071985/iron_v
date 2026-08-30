/*
 * INTPRI.h
 * INTPRI Peripheral
 * Base Address: 0x600C5000
 */

#ifndef INTPRI_H
#define INTPRI_H

#include <stdint.h>

#define INTPRI_BASE 0x600C5000

// register description
#define INTPRI_CPU_INT_ENABLE_REG ((volatile uint32_t *)(INTPRI_BASE + 0x0))
#define INTPRI_CPU_INT_ENABLE_CPU_INT_ENABLE_M (0xFFFFFFFFU)
#define INTPRI_CPU_INT_ENABLE_CPU_INT_ENABLE_S (0)
#define INTPRI_CPU_INT_ENABLE_CPU_INT_ENABLE_V(v) (((v) << 0) & 0xFFFFFFFFU)

// register description
#define INTPRI_CPU_INT_TYPE_REG ((volatile uint32_t *)(INTPRI_BASE + 0x4))
#define INTPRI_CPU_INT_TYPE_CPU_INT_TYPE_M (0xFFFFFFFFU)
#define INTPRI_CPU_INT_TYPE_CPU_INT_TYPE_S (0)
#define INTPRI_CPU_INT_TYPE_CPU_INT_TYPE_V(v) (((v) << 0) & 0xFFFFFFFFU)

// register description
#define INTPRI_CPU_INT_EIP_STATUS_REG ((volatile uint32_t *)(INTPRI_BASE + 0x8))
#define INTPRI_CPU_INT_EIP_STATUS_CPU_INT_EIP_STATUS_M (0xFFFFFFFFU)
#define INTPRI_CPU_INT_EIP_STATUS_CPU_INT_EIP_STATUS_S (0)
#define INTPRI_CPU_INT_EIP_STATUS_CPU_INT_EIP_STATUS_V(v) (((v) << 0) & 0xFFFFFFFFU)

// register description
#define INTPRI_CPU_INT_PRI_0_REG ((volatile uint32_t *)(INTPRI_BASE + 0xC))
#define INTPRI_CPU_INT_PRI_0_CPU_PRI_0_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_0_CPU_PRI_0_MAP_S (0)
#define INTPRI_CPU_INT_PRI_0_CPU_PRI_0_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_1_REG ((volatile uint32_t *)(INTPRI_BASE + 0x10))
#define INTPRI_CPU_INT_PRI_1_CPU_PRI_1_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_1_CPU_PRI_1_MAP_S (0)
#define INTPRI_CPU_INT_PRI_1_CPU_PRI_1_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_2_REG ((volatile uint32_t *)(INTPRI_BASE + 0x14))
#define INTPRI_CPU_INT_PRI_2_CPU_PRI_2_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_2_CPU_PRI_2_MAP_S (0)
#define INTPRI_CPU_INT_PRI_2_CPU_PRI_2_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_3_REG ((volatile uint32_t *)(INTPRI_BASE + 0x18))
#define INTPRI_CPU_INT_PRI_3_CPU_PRI_3_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_3_CPU_PRI_3_MAP_S (0)
#define INTPRI_CPU_INT_PRI_3_CPU_PRI_3_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_4_REG ((volatile uint32_t *)(INTPRI_BASE + 0x1C))
#define INTPRI_CPU_INT_PRI_4_CPU_PRI_4_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_4_CPU_PRI_4_MAP_S (0)
#define INTPRI_CPU_INT_PRI_4_CPU_PRI_4_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_5_REG ((volatile uint32_t *)(INTPRI_BASE + 0x20))
#define INTPRI_CPU_INT_PRI_5_CPU_PRI_5_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_5_CPU_PRI_5_MAP_S (0)
#define INTPRI_CPU_INT_PRI_5_CPU_PRI_5_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_6_REG ((volatile uint32_t *)(INTPRI_BASE + 0x24))
#define INTPRI_CPU_INT_PRI_6_CPU_PRI_6_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_6_CPU_PRI_6_MAP_S (0)
#define INTPRI_CPU_INT_PRI_6_CPU_PRI_6_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_7_REG ((volatile uint32_t *)(INTPRI_BASE + 0x28))
#define INTPRI_CPU_INT_PRI_7_CPU_PRI_7_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_7_CPU_PRI_7_MAP_S (0)
#define INTPRI_CPU_INT_PRI_7_CPU_PRI_7_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_8_REG ((volatile uint32_t *)(INTPRI_BASE + 0x2C))
#define INTPRI_CPU_INT_PRI_8_CPU_PRI_8_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_8_CPU_PRI_8_MAP_S (0)
#define INTPRI_CPU_INT_PRI_8_CPU_PRI_8_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_9_REG ((volatile uint32_t *)(INTPRI_BASE + 0x30))
#define INTPRI_CPU_INT_PRI_9_CPU_PRI_9_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_9_CPU_PRI_9_MAP_S (0)
#define INTPRI_CPU_INT_PRI_9_CPU_PRI_9_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_10_REG ((volatile uint32_t *)(INTPRI_BASE + 0x34))
#define INTPRI_CPU_INT_PRI_10_CPU_PRI_10_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_10_CPU_PRI_10_MAP_S (0)
#define INTPRI_CPU_INT_PRI_10_CPU_PRI_10_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_11_REG ((volatile uint32_t *)(INTPRI_BASE + 0x38))
#define INTPRI_CPU_INT_PRI_11_CPU_PRI_11_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_11_CPU_PRI_11_MAP_S (0)
#define INTPRI_CPU_INT_PRI_11_CPU_PRI_11_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_12_REG ((volatile uint32_t *)(INTPRI_BASE + 0x3C))
#define INTPRI_CPU_INT_PRI_12_CPU_PRI_12_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_12_CPU_PRI_12_MAP_S (0)
#define INTPRI_CPU_INT_PRI_12_CPU_PRI_12_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_13_REG ((volatile uint32_t *)(INTPRI_BASE + 0x40))
#define INTPRI_CPU_INT_PRI_13_CPU_PRI_13_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_13_CPU_PRI_13_MAP_S (0)
#define INTPRI_CPU_INT_PRI_13_CPU_PRI_13_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_14_REG ((volatile uint32_t *)(INTPRI_BASE + 0x44))
#define INTPRI_CPU_INT_PRI_14_CPU_PRI_14_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_14_CPU_PRI_14_MAP_S (0)
#define INTPRI_CPU_INT_PRI_14_CPU_PRI_14_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_15_REG ((volatile uint32_t *)(INTPRI_BASE + 0x48))
#define INTPRI_CPU_INT_PRI_15_CPU_PRI_15_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_15_CPU_PRI_15_MAP_S (0)
#define INTPRI_CPU_INT_PRI_15_CPU_PRI_15_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_16_REG ((volatile uint32_t *)(INTPRI_BASE + 0x4C))
#define INTPRI_CPU_INT_PRI_16_CPU_PRI_16_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_16_CPU_PRI_16_MAP_S (0)
#define INTPRI_CPU_INT_PRI_16_CPU_PRI_16_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_17_REG ((volatile uint32_t *)(INTPRI_BASE + 0x50))
#define INTPRI_CPU_INT_PRI_17_CPU_PRI_17_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_17_CPU_PRI_17_MAP_S (0)
#define INTPRI_CPU_INT_PRI_17_CPU_PRI_17_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_18_REG ((volatile uint32_t *)(INTPRI_BASE + 0x54))
#define INTPRI_CPU_INT_PRI_18_CPU_PRI_18_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_18_CPU_PRI_18_MAP_S (0)
#define INTPRI_CPU_INT_PRI_18_CPU_PRI_18_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_19_REG ((volatile uint32_t *)(INTPRI_BASE + 0x58))
#define INTPRI_CPU_INT_PRI_19_CPU_PRI_19_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_19_CPU_PRI_19_MAP_S (0)
#define INTPRI_CPU_INT_PRI_19_CPU_PRI_19_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_20_REG ((volatile uint32_t *)(INTPRI_BASE + 0x5C))
#define INTPRI_CPU_INT_PRI_20_CPU_PRI_20_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_20_CPU_PRI_20_MAP_S (0)
#define INTPRI_CPU_INT_PRI_20_CPU_PRI_20_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_21_REG ((volatile uint32_t *)(INTPRI_BASE + 0x60))
#define INTPRI_CPU_INT_PRI_21_CPU_PRI_21_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_21_CPU_PRI_21_MAP_S (0)
#define INTPRI_CPU_INT_PRI_21_CPU_PRI_21_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_22_REG ((volatile uint32_t *)(INTPRI_BASE + 0x64))
#define INTPRI_CPU_INT_PRI_22_CPU_PRI_22_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_22_CPU_PRI_22_MAP_S (0)
#define INTPRI_CPU_INT_PRI_22_CPU_PRI_22_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_23_REG ((volatile uint32_t *)(INTPRI_BASE + 0x68))
#define INTPRI_CPU_INT_PRI_23_CPU_PRI_23_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_23_CPU_PRI_23_MAP_S (0)
#define INTPRI_CPU_INT_PRI_23_CPU_PRI_23_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_24_REG ((volatile uint32_t *)(INTPRI_BASE + 0x6C))
#define INTPRI_CPU_INT_PRI_24_CPU_PRI_24_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_24_CPU_PRI_24_MAP_S (0)
#define INTPRI_CPU_INT_PRI_24_CPU_PRI_24_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_25_REG ((volatile uint32_t *)(INTPRI_BASE + 0x70))
#define INTPRI_CPU_INT_PRI_25_CPU_PRI_25_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_25_CPU_PRI_25_MAP_S (0)
#define INTPRI_CPU_INT_PRI_25_CPU_PRI_25_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_26_REG ((volatile uint32_t *)(INTPRI_BASE + 0x74))
#define INTPRI_CPU_INT_PRI_26_CPU_PRI_26_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_26_CPU_PRI_26_MAP_S (0)
#define INTPRI_CPU_INT_PRI_26_CPU_PRI_26_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_27_REG ((volatile uint32_t *)(INTPRI_BASE + 0x78))
#define INTPRI_CPU_INT_PRI_27_CPU_PRI_27_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_27_CPU_PRI_27_MAP_S (0)
#define INTPRI_CPU_INT_PRI_27_CPU_PRI_27_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_28_REG ((volatile uint32_t *)(INTPRI_BASE + 0x7C))
#define INTPRI_CPU_INT_PRI_28_CPU_PRI_28_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_28_CPU_PRI_28_MAP_S (0)
#define INTPRI_CPU_INT_PRI_28_CPU_PRI_28_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_29_REG ((volatile uint32_t *)(INTPRI_BASE + 0x80))
#define INTPRI_CPU_INT_PRI_29_CPU_PRI_29_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_29_CPU_PRI_29_MAP_S (0)
#define INTPRI_CPU_INT_PRI_29_CPU_PRI_29_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_30_REG ((volatile uint32_t *)(INTPRI_BASE + 0x84))
#define INTPRI_CPU_INT_PRI_30_CPU_PRI_30_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_30_CPU_PRI_30_MAP_S (0)
#define INTPRI_CPU_INT_PRI_30_CPU_PRI_30_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_PRI_31_REG ((volatile uint32_t *)(INTPRI_BASE + 0x88))
#define INTPRI_CPU_INT_PRI_31_CPU_PRI_31_MAP_M (0x0000000FU)
#define INTPRI_CPU_INT_PRI_31_CPU_PRI_31_MAP_S (0)
#define INTPRI_CPU_INT_PRI_31_CPU_PRI_31_MAP_V(v) (((v) << 0) & 0x0000000FU)

// register description
#define INTPRI_CPU_INT_THRESH_REG ((volatile uint32_t *)(INTPRI_BASE + 0x8C))
#define INTPRI_CPU_INT_THRESH_CPU_INT_THRESH_M (0x000000FFU)
#define INTPRI_CPU_INT_THRESH_CPU_INT_THRESH_S (0)
#define INTPRI_CPU_INT_THRESH_CPU_INT_THRESH_V(v) (((v) << 0) & 0x000000FFU)

// register description
#define INTPRI_CPU_INTR_FROM_CPU_0_REG ((volatile uint32_t *)(INTPRI_BASE + 0x90))
#define INTPRI_CPU_INTR_FROM_CPU_0_CPU_INTR_FROM_CPU_0_M (0x00000001U)
#define INTPRI_CPU_INTR_FROM_CPU_0_CPU_INTR_FROM_CPU_0_S (0)
#define INTPRI_CPU_INTR_FROM_CPU_0_CPU_INTR_FROM_CPU_0_V(v) (((v) << 0) & 0x00000001U)

// register description
#define INTPRI_CPU_INTR_FROM_CPU_1_REG ((volatile uint32_t *)(INTPRI_BASE + 0x94))
#define INTPRI_CPU_INTR_FROM_CPU_1_CPU_INTR_FROM_CPU_1_M (0x00000001U)
#define INTPRI_CPU_INTR_FROM_CPU_1_CPU_INTR_FROM_CPU_1_S (0)
#define INTPRI_CPU_INTR_FROM_CPU_1_CPU_INTR_FROM_CPU_1_V(v) (((v) << 0) & 0x00000001U)

// register description
#define INTPRI_CPU_INTR_FROM_CPU_2_REG ((volatile uint32_t *)(INTPRI_BASE + 0x98))
#define INTPRI_CPU_INTR_FROM_CPU_2_CPU_INTR_FROM_CPU_2_M (0x00000001U)
#define INTPRI_CPU_INTR_FROM_CPU_2_CPU_INTR_FROM_CPU_2_S (0)
#define INTPRI_CPU_INTR_FROM_CPU_2_CPU_INTR_FROM_CPU_2_V(v) (((v) << 0) & 0x00000001U)

// register description
#define INTPRI_CPU_INTR_FROM_CPU_3_REG ((volatile uint32_t *)(INTPRI_BASE + 0x9C))
#define INTPRI_CPU_INTR_FROM_CPU_3_CPU_INTR_FROM_CPU_3_M (0x00000001U)
#define INTPRI_CPU_INTR_FROM_CPU_3_CPU_INTR_FROM_CPU_3_S (0)
#define INTPRI_CPU_INTR_FROM_CPU_3_CPU_INTR_FROM_CPU_3_V(v) (((v) << 0) & 0x00000001U)

// register description
#define INTPRI_DATE_REG ((volatile uint32_t *)(INTPRI_BASE + 0xA0))
#define INTPRI_DATE_DATE_M (0x0FFFFFFFU)
#define INTPRI_DATE_DATE_S (0)
#define INTPRI_DATE_DATE_V(v) (((v) << 0) & 0x0FFFFFFFU)

// register description
#define INTPRI_CLOCK_GATE_REG ((volatile uint32_t *)(INTPRI_BASE + 0xA4))
#define INTPRI_CLOCK_GATE_CLK_EN_M (0x00000001U)
#define INTPRI_CLOCK_GATE_CLK_EN_S (0)
#define INTPRI_CLOCK_GATE_CLK_EN_V(v) (((v) << 0) & 0x00000001U)

// register description
#define INTPRI_CPU_INT_CLEAR_REG ((volatile uint32_t *)(INTPRI_BASE + 0xA8))
#define INTPRI_CPU_INT_CLEAR_CPU_INT_CLEAR_M (0xFFFFFFFFU)
#define INTPRI_CPU_INT_CLEAR_CPU_INT_CLEAR_S (0)
#define INTPRI_CPU_INT_CLEAR_CPU_INT_CLEAR_V(v) (((v) << 0) & 0xFFFFFFFFU)

// redcy eco register.
#define INTPRI_RND_ECO_REG ((volatile uint32_t *)(INTPRI_BASE + 0xAC))
#define INTPRI_RND_ECO_REDCY_ENA_M (0x00000001U)
#define INTPRI_RND_ECO_REDCY_ENA_S (0)
#define INTPRI_RND_ECO_REDCY_ENA_V(v) (((v) << 0) & 0x00000001U)
#define INTPRI_RND_ECO_REDCY_RESULT_M (0x00000002U)
#define INTPRI_RND_ECO_REDCY_RESULT_S (1)
#define INTPRI_RND_ECO_REDCY_RESULT_V(v) (((v) << 1) & 0x00000002U)

// redcy eco low register.
#define INTPRI_RND_ECO_LOW_REG ((volatile uint32_t *)(INTPRI_BASE + 0xB0))
#define INTPRI_RND_ECO_LOW_REDCY_LOW_M (0xFFFFFFFFU)
#define INTPRI_RND_ECO_LOW_REDCY_LOW_S (0)
#define INTPRI_RND_ECO_LOW_REDCY_LOW_V(v) (((v) << 0) & 0xFFFFFFFFU)

// redcy eco high register.
#define INTPRI_RND_ECO_HIGH_REG ((volatile uint32_t *)(INTPRI_BASE + 0x3FC))
#define INTPRI_RND_ECO_HIGH_REDCY_HIGH_M (0xFFFFFFFFU)
#define INTPRI_RND_ECO_HIGH_REDCY_HIGH_S (0)
#define INTPRI_RND_ECO_HIGH_REDCY_HIGH_V(v) (((v) << 0) & 0xFFFFFFFFU)

#endif // INTPRI_H
