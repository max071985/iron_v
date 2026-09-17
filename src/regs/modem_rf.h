/*
 * src/regs/modem_rf.h
 *
 * ESP32-C6 Modem RF & Synthesizer Register Definitions
 * Base Address: 0x600A7000
 *
 * Reverse-engineered from ESP32-C6 factory RF test firmware
 * (ESP32-C6_RFTest_V106_4cc3bb5_20250909.bin)
 */

#ifndef IRON_V_MODEM_RF_H
#define IRON_V_MODEM_RF_H

#include <stdint.h>

/* ========================================================================= */
/* Peripheral Base Address                                                   */
/* ========================================================================= */
#define MODEM_RF_BASE_ADDR                      0x600A7000U

/* ========================================================================= */
/* Parameterized MMIO Register Accessor Macro (AGENTS.md rule)               */
/* ========================================================================= */
#define MODEM_RF_REG(offset)                    ((volatile uint32_t *)(MODEM_RF_BASE_ADDR + (offset)))

/* ========================================================================= */
/* Register Offsets & Register Definitions                                   */
/* ========================================================================= */
#define MODEM_RF_CTRL_OFFSET                    0x0000U
#define MODEM_RF_CTRL_REG                       MODEM_RF_REG(MODEM_RF_CTRL_OFFSET)

#define MODEM_RF_PLL_FEEDBACK_OFFSET            0x002CU
#define MODEM_RF_PLL_FEEDBACK_REG               MODEM_RF_REG(MODEM_RF_PLL_FEEDBACK_OFFSET)

#define MODEM_RF_SYNTH_TUNING_OFFSET            0x0094U
#define MODEM_RF_SYNTH_TUNING_REG               MODEM_RF_REG(MODEM_RF_SYNTH_TUNING_OFFSET)

#define MODEM_RF_BLE_LINK_OFFSET                0x00A0U
#define MODEM_RF_BLE_LINK_REG                   MODEM_RF_REG(MODEM_RF_BLE_LINK_OFFSET)

#define MODEM_RF_CAL_DATA_PTR_OFFSET            0x00B8U
#define MODEM_RF_CAL_DATA_PTR_REG               MODEM_RF_REG(MODEM_RF_CAL_DATA_PTR_OFFSET)

#define MODEM_RF_BASEBAND_LINK_OFFSET           0x00DCU
#define MODEM_RF_BASEBAND_LINK_REG              MODEM_RF_REG(MODEM_RF_BASEBAND_LINK_OFFSET)

#define MODEM_RF_ENABLE_OFFSET                  0x0104U
#define MODEM_RF_ENABLE_REG                     MODEM_RF_REG(MODEM_RF_ENABLE_OFFSET)
#define MODEM_RF_ENABLE_MASTER_BIT              (1U << 31)

#define MODEM_RF_PLL_LOCK_OFFSET                0x0124U
#define MODEM_RF_PLL_LOCK_REG                   MODEM_RF_REG(MODEM_RF_PLL_LOCK_OFFSET)
#define MODEM_RF_PLL_LOCK_BIT                   (1U << 31)

#define MODEM_RF_PLL_FREQ_WORD_OFFSET           0x0128U
#define MODEM_RF_PLL_FREQ_WORD_REG              MODEM_RF_REG(MODEM_RF_PLL_FREQ_WORD_OFFSET)

#define MODEM_RF_CAL_CONF_OFFSET                0x013CU
#define MODEM_RF_CAL_CONF_REG                   MODEM_RF_REG(MODEM_RF_CAL_CONF_OFFSET)

#define MODEM_RF_DMA_BUF_OFFSET                 0x01D4U
#define MODEM_RF_DMA_BUF_REG                    MODEM_RF_REG(MODEM_RF_DMA_BUF_OFFSET)

#define MODEM_RF_AGC_CTRL_OFFSET                0x0400U
#define MODEM_RF_AGC_CTRL_REG                   MODEM_RF_REG(MODEM_RF_AGC_CTRL_OFFSET)
#define MODEM_RF_AGC_EN_BIT                     0x0080U
#define MODEM_RF_AGC_OPT_EN_MASK                0x6000U

#define MODEM_RF_AGC_CFG1_OFFSET                0x0414U
#define MODEM_RF_AGC_CFG1_REG                   MODEM_RF_REG(MODEM_RF_AGC_CFG1_OFFSET)

#define MODEM_RF_AGC_CFG2_OFFSET                0x0418U
#define MODEM_RF_AGC_CFG2_REG                   MODEM_RF_REG(MODEM_RF_AGC_CFG2_OFFSET)

#define MODEM_RF_AGC_CFG3_OFFSET                0x0424U
#define MODEM_RF_AGC_CFG3_REG                   MODEM_RF_REG(MODEM_RF_AGC_CFG3_OFFSET)

#define MODEM_RF_AGC_CFG4_OFFSET                0x0428U
#define MODEM_RF_AGC_CFG4_REG                   MODEM_RF_REG(MODEM_RF_AGC_CFG4_OFFSET)

#define MODEM_RF_AGC_CFG5_OFFSET                0x0438U
#define MODEM_RF_AGC_CFG5_REG                   MODEM_RF_REG(MODEM_RF_AGC_CFG5_OFFSET)

#define MODEM_RF_TX_PWR_IDX_OFFSET              0x0808U
#define MODEM_RF_TX_PWR_IDX_REG                 MODEM_RF_REG(MODEM_RF_TX_PWR_IDX_OFFSET)

#define MODEM_RF_CAL_OFFSET_0_OFFSET            0x0C00U
#define MODEM_RF_CAL_OFFSET_0_REG               MODEM_RF_REG(MODEM_RF_CAL_OFFSET_0_OFFSET)

#define MODEM_RF_CAL_OFFSET_1_OFFSET            0x0C14U
#define MODEM_RF_CAL_OFFSET_1_REG               MODEM_RF_REG(MODEM_RF_CAL_OFFSET_1_OFFSET)

#define MODEM_RF_CAL_OFFSET_2_OFFSET            0x0C30U
#define MODEM_RF_CAL_OFFSET_2_REG               MODEM_RF_REG(MODEM_RF_CAL_OFFSET_2_OFFSET)

#define MODEM_RF_CAL_OFFSET_3_OFFSET            0x0C6CU
#define MODEM_RF_CAL_OFFSET_3_REG               MODEM_RF_REG(MODEM_RF_CAL_OFFSET_3_OFFSET)

#define MODEM_RF_CAL_OFFSET_4_OFFSET            0x0CA8U
#define MODEM_RF_CAL_OFFSET_4_REG               MODEM_RF_REG(MODEM_RF_CAL_OFFSET_4_OFFSET)

#define MODEM_RF_CAL_OFFSET_5_OFFSET            0x0CD0U
#define MODEM_RF_CAL_OFFSET_5_REG               MODEM_RF_REG(MODEM_RF_CAL_OFFSET_5_OFFSET)

#endif /* IRON_V_MODEM_RF_H */
