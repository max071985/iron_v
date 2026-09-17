/*
 * src/regs/i2c_ana.h
 *
 * ESP32-C6 Internal I2C Analog Master Register Definitions
 * Base Address: 0x600AF800
 *
 * Reverse-engineered from ESP32-C6 factory RF test firmware
 * (ESP32-C6_RFTest_V106_4cc3bb5_20250909.bin)
 */

#ifndef IRON_V_I2C_ANA_H
#define IRON_V_I2C_ANA_H

#include <stdint.h>

/* ========================================================================= */
/* Peripheral Base Address                                                   */
/* ========================================================================= */
#define I2C_ANA_MST_BASE_ADDR                   0x600AF800U

/* ========================================================================= */
/* Parameterized MMIO Register Accessor Macro (AGENTS.md rule)               */
/* ========================================================================= */
#define I2C_ANA_MST_REG(offset)                 ((volatile uint32_t *)(uintptr_t)(I2C_ANA_MST_BASE_ADDR + (offset)))

/* ========================================================================= */
/* Register Offsets & Register Definitions                                   */
/* ========================================================================= */
#define I2C_ANA_MST_CTRL_OFFSET                 0x0000U
#define I2C_ANA_MST_CTRL_REG                    I2C_ANA_MST_REG(I2C_ANA_MST_CTRL_OFFSET)

#define I2C_ANA_MST_CONF_OFFSET                 0x0004U
#define I2C_ANA_MST_CONF_REG                    I2C_ANA_MST_REG(I2C_ANA_MST_CONF_OFFSET)

#define I2C_ANA_MST_DATA_OFFSET                 0x0008U
#define I2C_ANA_MST_DATA_REG                    I2C_ANA_MST_REG(I2C_ANA_MST_DATA_OFFSET)

#define I2C_ANA_MST_ANA_CONF1_OFFSET            0x000CU
#define I2C_ANA_MST_ANA_CONF1_REG               I2C_ANA_MST_REG(I2C_ANA_MST_ANA_CONF1_OFFSET)

#define I2C_ANA_MST_DEVICE_EN_OFFSET            0x0014U
#define I2C_ANA_MST_DEVICE_EN_REG               I2C_ANA_MST_REG(I2C_ANA_MST_DEVICE_EN_OFFSET)

#define I2C_ANA_MST_DATE_OFFSET                 0x03FCU
#define I2C_ANA_MST_DATE_REG                    I2C_ANA_MST_REG(I2C_ANA_MST_DATE_OFFSET)

/* ========================================================================= */
/* Device Enable Bitfields & Constants                                       */
/* ========================================================================= */
#define I2C_ANA_MST_DEVICE_EN_BBPLL_BIT         (1U << 0)
#define I2C_ANA_MST_DEVICE_EN_TXRF_BIT          (1U << 1)
#define I2C_ANA_MST_DEVICE_EN_ALL               0x00000FFFU

/* ========================================================================= */
/* LP_CLKRST Bus Pointer Link Registers for I2C Analog Master                */
/* ========================================================================= */
#define LP_CLKRST_I2C_ANA_MST_LINK0_REG         ((volatile uint32_t *)(uintptr_t)0x600B0418U)
#define LP_CLKRST_I2C_ANA_MST_LINK1_REG         ((volatile uint32_t *)(uintptr_t)0x600B041CU)
#define LP_CLKRST_I2C_ANA_MST_LINK_VAL          0x600AF000U

#endif /* IRON_V_I2C_ANA_H */
