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
/* Register Offsets                                                          */
/* ========================================================================= */
#define I2C_ANA_MST_I2C0_CTRL_OFFSET            0x0000U
#define I2C_ANA_MST_I2C1_CTRL_OFFSET            0x0004U
#define I2C_ANA_MST_I2C0_CONF_OFFSET            0x0008U
#define I2C_ANA_MST_I2C1_CONF_OFFSET            0x000CU
#define I2C_ANA_MST_BURST_CONF_OFFSET           0x0010U
#define I2C_ANA_MST_BURST_STATUS_OFFSET         0x0014U
#define I2C_ANA_MST_ANA_CONF0_OFFSET            0x0018U
#define I2C_ANA_MST_ANA_CONF1_OFFSET            0x001CU
#define I2C_ANA_MST_ANA_CONF2_OFFSET            0x0020U
#define I2C_ANA_MST_I2C0_CTRL1_OFFSET           0x0024U
#define I2C_ANA_MST_I2C1_CTRL1_OFFSET           0x0028U
#define I2C_ANA_MST_DATE_OFFSET                 0x0034U

/* ========================================================================= */
/* Direct Register Pointers                                                  */
/* ========================================================================= */
#define I2C_ANA_MST_I2C0_CTRL_REG               I2C_ANA_MST_REG(I2C_ANA_MST_I2C0_CTRL_OFFSET)
#define I2C_ANA_MST_I2C1_CTRL_REG               I2C_ANA_MST_REG(I2C_ANA_MST_I2C1_CTRL_OFFSET)
#define I2C_ANA_MST_I2C0_CONF_REG               I2C_ANA_MST_REG(I2C_ANA_MST_I2C0_CONF_OFFSET)
#define I2C_ANA_MST_I2C1_CONF_REG               I2C_ANA_MST_REG(I2C_ANA_MST_I2C1_CONF_OFFSET)
#define I2C_ANA_MST_BURST_CONF_REG              I2C_ANA_MST_REG(I2C_ANA_MST_BURST_CONF_OFFSET)
#define I2C_ANA_MST_BURST_STATUS_REG            I2C_ANA_MST_REG(I2C_ANA_MST_BURST_STATUS_OFFSET)
#define I2C_ANA_MST_ANA_CONF0_REG               I2C_ANA_MST_REG(I2C_ANA_MST_ANA_CONF0_OFFSET)
#define I2C_ANA_MST_ANA_CONF1_REG               I2C_ANA_MST_REG(I2C_ANA_MST_ANA_CONF1_OFFSET)
#define I2C_ANA_MST_ANA_CONF2_REG               I2C_ANA_MST_REG(I2C_ANA_MST_ANA_CONF2_OFFSET)
#define I2C_ANA_MST_I2C0_CTRL1_REG              I2C_ANA_MST_REG(I2C_ANA_MST_I2C0_CTRL1_OFFSET)
#define I2C_ANA_MST_I2C1_CTRL1_REG              I2C_ANA_MST_REG(I2C_ANA_MST_I2C1_CTRL1_OFFSET)
#define I2C_ANA_MST_DATE_REG                    I2C_ANA_MST_REG(I2C_ANA_MST_DATE_OFFSET)

/* Parameterized Channel Accessor Macros */
#define I2C_ANA_MST_I2C_CTRL_REG(ch)            I2C_ANA_MST_REG(((ch) == 0U) ? I2C_ANA_MST_I2C0_CTRL_OFFSET : I2C_ANA_MST_I2C1_CTRL_OFFSET)
#define I2C_ANA_MST_I2C_CONF_REG(ch)            I2C_ANA_MST_REG(((ch) == 0U) ? I2C_ANA_MST_I2C0_CONF_OFFSET : I2C_ANA_MST_I2C1_CONF_OFFSET)
#define I2C_ANA_MST_I2C_CTRL1_REG(ch)           I2C_ANA_MST_REG(((ch) == 0U) ? I2C_ANA_MST_I2C0_CTRL1_OFFSET : I2C_ANA_MST_I2C1_CTRL1_OFFSET)

/* Backward-compatibility alias for previous offset 0x0014 name */
#define I2C_ANA_MST_DEVICE_EN_OFFSET            I2C_ANA_MST_BURST_STATUS_OFFSET
#define I2C_ANA_MST_DEVICE_EN_REG               I2C_ANA_MST_BURST_STATUS_REG

/* ========================================================================= */
/* I2C%s_CTRL Bitfields & Parameterized Command Builder                      */
/* ========================================================================= */
#define I2C_ANA_MST_SLAVE_ADDR_SHIFT            0U
#define I2C_ANA_MST_SLAVE_ADDR_MASK             0x000000FFU
#define I2C_ANA_MST_SLAVE_REG_ADDR_SHIFT        8U
#define I2C_ANA_MST_SLAVE_REG_ADDR_MASK         0x0000FF00U
#define I2C_ANA_MST_DATA_SHIFT                  16U
#define I2C_ANA_MST_DATA_MASK                   0x00FF0000U
#define I2C_ANA_MST_READ_WRITE_BIT              (1U << 24) /* 0 = read, 1 = write */
#define I2C_ANA_MST_BUSY_BIT                    (1U << 25) /* 1 = in progress */

/* Parameterized Transaction Word Constructors */
#define I2C_ANA_MST_CMD_WRITE(slave, reg, data) ( \
    I2C_ANA_MST_READ_WRITE_BIT | \
    ((((uint32_t)(data))  << I2C_ANA_MST_DATA_SHIFT)           & I2C_ANA_MST_DATA_MASK) | \
    ((((uint32_t)(reg))   << I2C_ANA_MST_SLAVE_REG_ADDR_SHIFT) & I2C_ANA_MST_SLAVE_REG_ADDR_MASK) | \
    ((((uint32_t)(slave)) << I2C_ANA_MST_SLAVE_ADDR_SHIFT)     & I2C_ANA_MST_SLAVE_ADDR_MASK))

#define I2C_ANA_MST_CMD_READ(slave, reg) ( \
    ((((uint32_t)(reg))   << I2C_ANA_MST_SLAVE_REG_ADDR_SHIFT) & I2C_ANA_MST_SLAVE_REG_ADDR_MASK) | \
    ((((uint32_t)(slave)) << I2C_ANA_MST_SLAVE_ADDR_SHIFT)     & I2C_ANA_MST_SLAVE_ADDR_MASK))

/* ========================================================================= */
/* ANA_CONF0 Bitfields (Calibration Control & Status)                        */
/* ========================================================================= */
#define I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT   (1U << 2)
#define I2C_ANA_MST_BBPLL_STOP_FORCE_LOW_BIT    (1U << 3)
#define I2C_ANA_MST_CAL_DONE_BIT                (1U << 24)

/* ========================================================================= */
/* ANA_CONF1 Bitfields (Analog Subsystem Power-Down & Slave Enables)         */
/* ========================================================================= */
#define I2C_ANA_MST_ANA_CONF1_BIAS_RD_BIT       (1U << 6)
#define I2C_ANA_MST_ANA_CONF1_BBPLL_RD_BIT      (1U << 7)  /* Active-low: 0 to select BBPLL */
#define I2C_ANA_MST_ANA_CONF1_ULP_CAL_RD_BIT    (1U << 8)
#define I2C_ANA_MST_ANA_CONF1_SAR_I2C_RD_BIT    (1U << 9)
#define I2C_ANA_MST_ANA_CONF1_DIG_REG_RD_BIT    (1U << 10)
#define I2C_ANA_MST_ANA_CONF1_SAR_FORCE_PU_BIT  (1U << 16)
#define I2C_ANA_MST_ANA_CONF1_BBPLL_PD_BIT      (1U << 17) /* Active-low: 0 to power BBPLL */
#define I2C_ANA_MST_ANA_CONF1_SAR_FORCE_PD_BIT  (1U << 18)
#define I2C_ANA_MST_ANA_CONF1_DEFAULT_MASK      0x00FFFFFFU

/* ========================================================================= */
/* ANA_CONF2 Bitfields (I2C Master Multiplexing)                             */
/* ========================================================================= */
#define I2C_ANA_MST_ANA_CONF2_BIAS_MST_SEL_BIT  (1U << 8)
#define I2C_ANA_MST_ANA_CONF2_BBPLL_MST_SEL_BIT (1U << 9)  /* 1 = I2C0 Master, 0 = I2C1 Master */

/* ========================================================================= */
/* Internal BBPLL Module Register Definitions (Slave Block 0x66)             */
/* ========================================================================= */
#define I2C_BBPLL_SLAVE_ADDR                    0x66U

/* Register 2: Reference Clock Divider & Charge Pump */
#define I2C_BBPLL_OC_REF_ADDR                   2U
#define I2C_BBPLL_OC_REF_DIV_SHIFT              0U
#define I2C_BBPLL_OC_REF_DIV_MASK               0x0FU
#define I2C_BBPLL_OC_REF_DIV_40M                0U
#define I2C_BBPLL_OC_DCHGP_SHIFT                4U
#define I2C_BBPLL_OC_DCHGP_MASK                 0x70U
#define I2C_BBPLL_OC_DCHGP_DEFAULT              5U
#define I2C_BBPLL_OC_REF_VAL                    ((I2C_BBPLL_OC_DCHGP_DEFAULT << I2C_BBPLL_OC_DCHGP_SHIFT) | \
                                                 (I2C_BBPLL_OC_REF_DIV_40M   << I2C_BBPLL_OC_REF_DIV_SHIFT))

/* Register 3: Multiplier Factor (480 MHz from 40 MHz XTAL: (8 + 4) * 40 = 480) */
#define I2C_BBPLL_OC_DIV_REG_ADDR               3U
#define I2C_BBPLL_OC_DIV_REG_VAL                8U

/* Register 5: Intermediate Dividers */
#define I2C_BBPLL_OC_DR_ADDR                    5U
#define I2C_BBPLL_OC_DR1_MASK                   0x07U
#define I2C_BBPLL_OC_DR3_MASK                   0x70U

/* Register 6: Analog Bias and Reference Voltage */
#define I2C_BBPLL_REG6_ADDR                     6U
#define I2C_BBPLL_OC_DCUR_SHIFT                 0U
#define I2C_BBPLL_OC_DCUR_MASK                  0x07U
#define I2C_BBPLL_OC_DCUR_DEFAULT               3U
#define I2C_BBPLL_OC_DHREF_SEL_SHIFT            4U
#define I2C_BBPLL_OC_DHREF_SEL_MASK             0x30U
#define I2C_BBPLL_OC_DHREF_SEL_DEFAULT          3U
#define I2C_BBPLL_OC_DLREF_SEL_SHIFT            6U
#define I2C_BBPLL_OC_DLREF_SEL_MASK             0xC0U
#define I2C_BBPLL_OC_DLREF_SEL_DEFAULT          1U
#define I2C_BBPLL_REG6_VAL                      ((I2C_BBPLL_OC_DLREF_SEL_DEFAULT << I2C_BBPLL_OC_DLREF_SEL_SHIFT) | \
                                                 (I2C_BBPLL_OC_DHREF_SEL_DEFAULT << I2C_BBPLL_OC_DHREF_SEL_SHIFT) | \
                                                 (I2C_BBPLL_OC_DCUR_DEFAULT      << I2C_BBPLL_OC_DCUR_SHIFT))

/* Register 9: VCO Dynamic Bias */
#define I2C_BBPLL_REG9_ADDR                     9U
#define I2C_BBPLL_OC_VCO_DBIAS_SHIFT            0U
#define I2C_BBPLL_OC_VCO_DBIAS_MASK             0x03U
#define I2C_BBPLL_OC_VCO_DBIAS_DEFAULT          2U

/* Hardware Operational Constraints & Timeouts */
#define BBPLL_CALIBRATION_TIMEOUT_CYCLES        50000U
#define BBPLL_BUSY_POLL_TIMEOUT_CYCLES          10000U
#define BBPLL_SETTLE_DELAY_US                   10U

/* ========================================================================= */
/* PMU BBPLL Power Mask (0x600B00CC)                                         */
/* ========================================================================= */
#define PMU_HP_CK_GLOBAL_BBPLL_ICG_BIT          (1U << 25)
#define PMU_HP_CK_XPD_BB_I2C_BIT                (1U << 28)
#define PMU_HP_CK_XPD_BBPLL_I2C_BIT             (1U << 29)
#define PMU_HP_CK_XPD_BBPLL_BIT                 (1U << 30)
#define PMU_BBPLL_POWER_ENABLE_MASK             (PMU_HP_CK_GLOBAL_BBPLL_ICG_BIT | \
                                                 PMU_HP_CK_XPD_BB_I2C_BIT |       \
                                                 PMU_HP_CK_XPD_BBPLL_I2C_BIT |    \
                                                 PMU_HP_CK_XPD_BBPLL_BIT)

/* ========================================================================= */
/* LP_CLKRST Bus Pointer Link Registers for I2C Analog Master                */
/* ========================================================================= */
#define LP_CLKRST_I2C_ANA_MST_LINK0_REG         ((volatile uint32_t *)(uintptr_t)0x600B0418U)
#define LP_CLKRST_I2C_ANA_MST_LINK1_REG         ((volatile uint32_t *)(uintptr_t)0x600B041CU)
#define LP_CLKRST_I2C_ANA_MST_LINK_VAL          0x600AF000U

#endif /* IRON_V_I2C_ANA_H */
