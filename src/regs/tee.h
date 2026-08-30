/*
 * TEE.h
 * TEE Peripheral
 * Base Address: 0x60098000
 */

#ifndef TEE_H
#define TEE_H

#include <stdint.h>

#define TEE_BASE 0x60098000

// Tee mode control register
#define TEE_M0_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x0))
#define TEE_M0_MODE_CTRL_M0_MODE_M (0x00000003U)
#define TEE_M0_MODE_CTRL_M0_MODE_S (0)
#define TEE_M0_MODE_CTRL_M0_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M1_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x4))
#define TEE_M1_MODE_CTRL_M1_MODE_M (0x00000003U)
#define TEE_M1_MODE_CTRL_M1_MODE_S (0)
#define TEE_M1_MODE_CTRL_M1_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M2_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x8))
#define TEE_M2_MODE_CTRL_M2_MODE_M (0x00000003U)
#define TEE_M2_MODE_CTRL_M2_MODE_S (0)
#define TEE_M2_MODE_CTRL_M2_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M3_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0xC))
#define TEE_M3_MODE_CTRL_M3_MODE_M (0x00000003U)
#define TEE_M3_MODE_CTRL_M3_MODE_S (0)
#define TEE_M3_MODE_CTRL_M3_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M4_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x10))
#define TEE_M4_MODE_CTRL_M4_MODE_M (0x00000003U)
#define TEE_M4_MODE_CTRL_M4_MODE_S (0)
#define TEE_M4_MODE_CTRL_M4_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M5_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x14))
#define TEE_M5_MODE_CTRL_M5_MODE_M (0x00000003U)
#define TEE_M5_MODE_CTRL_M5_MODE_S (0)
#define TEE_M5_MODE_CTRL_M5_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M6_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x18))
#define TEE_M6_MODE_CTRL_M6_MODE_M (0x00000003U)
#define TEE_M6_MODE_CTRL_M6_MODE_S (0)
#define TEE_M6_MODE_CTRL_M6_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M7_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x1C))
#define TEE_M7_MODE_CTRL_M7_MODE_M (0x00000003U)
#define TEE_M7_MODE_CTRL_M7_MODE_S (0)
#define TEE_M7_MODE_CTRL_M7_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M8_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x20))
#define TEE_M8_MODE_CTRL_M8_MODE_M (0x00000003U)
#define TEE_M8_MODE_CTRL_M8_MODE_S (0)
#define TEE_M8_MODE_CTRL_M8_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M9_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x24))
#define TEE_M9_MODE_CTRL_M9_MODE_M (0x00000003U)
#define TEE_M9_MODE_CTRL_M9_MODE_S (0)
#define TEE_M9_MODE_CTRL_M9_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M10_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x28))
#define TEE_M10_MODE_CTRL_M10_MODE_M (0x00000003U)
#define TEE_M10_MODE_CTRL_M10_MODE_S (0)
#define TEE_M10_MODE_CTRL_M10_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M11_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x2C))
#define TEE_M11_MODE_CTRL_M11_MODE_M (0x00000003U)
#define TEE_M11_MODE_CTRL_M11_MODE_S (0)
#define TEE_M11_MODE_CTRL_M11_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M12_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x30))
#define TEE_M12_MODE_CTRL_M12_MODE_M (0x00000003U)
#define TEE_M12_MODE_CTRL_M12_MODE_S (0)
#define TEE_M12_MODE_CTRL_M12_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M13_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x34))
#define TEE_M13_MODE_CTRL_M13_MODE_M (0x00000003U)
#define TEE_M13_MODE_CTRL_M13_MODE_S (0)
#define TEE_M13_MODE_CTRL_M13_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M14_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x38))
#define TEE_M14_MODE_CTRL_M14_MODE_M (0x00000003U)
#define TEE_M14_MODE_CTRL_M14_MODE_S (0)
#define TEE_M14_MODE_CTRL_M14_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M15_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x3C))
#define TEE_M15_MODE_CTRL_M15_MODE_M (0x00000003U)
#define TEE_M15_MODE_CTRL_M15_MODE_S (0)
#define TEE_M15_MODE_CTRL_M15_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M16_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x40))
#define TEE_M16_MODE_CTRL_M16_MODE_M (0x00000003U)
#define TEE_M16_MODE_CTRL_M16_MODE_S (0)
#define TEE_M16_MODE_CTRL_M16_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M17_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x44))
#define TEE_M17_MODE_CTRL_M17_MODE_M (0x00000003U)
#define TEE_M17_MODE_CTRL_M17_MODE_S (0)
#define TEE_M17_MODE_CTRL_M17_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M18_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x48))
#define TEE_M18_MODE_CTRL_M18_MODE_M (0x00000003U)
#define TEE_M18_MODE_CTRL_M18_MODE_S (0)
#define TEE_M18_MODE_CTRL_M18_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M19_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x4C))
#define TEE_M19_MODE_CTRL_M19_MODE_M (0x00000003U)
#define TEE_M19_MODE_CTRL_M19_MODE_S (0)
#define TEE_M19_MODE_CTRL_M19_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M20_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x50))
#define TEE_M20_MODE_CTRL_M20_MODE_M (0x00000003U)
#define TEE_M20_MODE_CTRL_M20_MODE_S (0)
#define TEE_M20_MODE_CTRL_M20_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M21_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x54))
#define TEE_M21_MODE_CTRL_M21_MODE_M (0x00000003U)
#define TEE_M21_MODE_CTRL_M21_MODE_S (0)
#define TEE_M21_MODE_CTRL_M21_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M22_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x58))
#define TEE_M22_MODE_CTRL_M22_MODE_M (0x00000003U)
#define TEE_M22_MODE_CTRL_M22_MODE_S (0)
#define TEE_M22_MODE_CTRL_M22_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M23_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x5C))
#define TEE_M23_MODE_CTRL_M23_MODE_M (0x00000003U)
#define TEE_M23_MODE_CTRL_M23_MODE_S (0)
#define TEE_M23_MODE_CTRL_M23_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M24_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x60))
#define TEE_M24_MODE_CTRL_M24_MODE_M (0x00000003U)
#define TEE_M24_MODE_CTRL_M24_MODE_S (0)
#define TEE_M24_MODE_CTRL_M24_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M25_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x64))
#define TEE_M25_MODE_CTRL_M25_MODE_M (0x00000003U)
#define TEE_M25_MODE_CTRL_M25_MODE_S (0)
#define TEE_M25_MODE_CTRL_M25_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M26_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x68))
#define TEE_M26_MODE_CTRL_M26_MODE_M (0x00000003U)
#define TEE_M26_MODE_CTRL_M26_MODE_S (0)
#define TEE_M26_MODE_CTRL_M26_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M27_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x6C))
#define TEE_M27_MODE_CTRL_M27_MODE_M (0x00000003U)
#define TEE_M27_MODE_CTRL_M27_MODE_S (0)
#define TEE_M27_MODE_CTRL_M27_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M28_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x70))
#define TEE_M28_MODE_CTRL_M28_MODE_M (0x00000003U)
#define TEE_M28_MODE_CTRL_M28_MODE_S (0)
#define TEE_M28_MODE_CTRL_M28_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M29_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x74))
#define TEE_M29_MODE_CTRL_M29_MODE_M (0x00000003U)
#define TEE_M29_MODE_CTRL_M29_MODE_S (0)
#define TEE_M29_MODE_CTRL_M29_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M30_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x78))
#define TEE_M30_MODE_CTRL_M30_MODE_M (0x00000003U)
#define TEE_M30_MODE_CTRL_M30_MODE_S (0)
#define TEE_M30_MODE_CTRL_M30_MODE_V(v) (((v) << 0) & 0x00000003U)

// Tee mode control register
#define TEE_M31_MODE_CTRL_REG ((volatile uint32_t *)(TEE_BASE + 0x7C))
#define TEE_M31_MODE_CTRL_M31_MODE_M (0x00000003U)
#define TEE_M31_MODE_CTRL_M31_MODE_S (0)
#define TEE_M31_MODE_CTRL_M31_MODE_V(v) (((v) << 0) & 0x00000003U)

// Clock gating register
#define TEE_CLOCK_GATE_REG ((volatile uint32_t *)(TEE_BASE + 0x80))
#define TEE_CLOCK_GATE_CLK_EN_M (0x00000001U)
#define TEE_CLOCK_GATE_CLK_EN_S (0)
#define TEE_CLOCK_GATE_CLK_EN_V(v) (((v) << 0) & 0x00000001U)

// Version register
#define TEE_DATE_REG ((volatile uint32_t *)(TEE_BASE + 0xFFC))
#define TEE_DATE_DATE_M (0x0FFFFFFFU)
#define TEE_DATE_DATE_S (0)
#define TEE_DATE_DATE_V(v) (((v) << 0) & 0x0FFFFFFFU)

#endif // TEE_H
