/*
 * src/regs/adc.h
 *
 * ESP32-C6 APB SAR ADC & Wi-Fi RX DC Offset Calibration Registers
 * Base Address: 0x6000E000
 *
 * Reverse-engineered from ESP32-C6 factory RF test firmware
 * (ESP32-C6_RFTest_V106_4cc3bb5_20250909.bin)
 */

#ifndef IRON_V_ADC_H
#define IRON_V_ADC_H

#include <stdint.h>

/* ========================================================================= */
/* Peripheral Base Address                                                   */
/* ========================================================================= */
#define APB_SARADC_BASE_ADDR                    0x6000E000U

/* ========================================================================= */
/* Parameterized MMIO Register Accessor Macro (AGENTS.md rule)               */
/* ========================================================================= */
#define APB_SARADC_REG(offset)                  ((volatile uint32_t *)(uintptr_t)(APB_SARADC_BASE_ADDR + (offset)))

/* ========================================================================= */
/* Register Offsets & Register Definitions                                   */
/* ========================================================================= */
#define APB_SARADC_CTRL_OFFSET                  0x0000U
#define APB_SARADC_CTRL_REG                     APB_SARADC_REG(APB_SARADC_CTRL_OFFSET)

#define APB_SARADC_FILTER_DATA_OFFSET           0x006CU
#define APB_SARADC_FILTER_DATA_REG              APB_SARADC_REG(APB_SARADC_FILTER_DATA_OFFSET)

#define APB_SARADC_STATUS_OFFSET                0x00BCU
#define APB_SARADC_STATUS_REG                   APB_SARADC_REG(APB_SARADC_STATUS_OFFSET)

/* DC Offset Calibration Registers (0x6000E0D0 - 0x6000E0FC) */
#define APB_SARADC_CAL_START_OFFSET             0x00D0U
#define APB_SARADC_CAL_END_OFFSET               0x00FCU
#define APB_SARADC_CAL_REG_COUNT                12U

#define APB_SARADC_CAL_REG(idx)                 APB_SARADC_REG(APB_SARADC_CAL_START_OFFSET + ((idx) * 4U))

#endif /* IRON_V_ADC_H */
