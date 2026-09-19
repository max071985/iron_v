/*
 * src/regs/modem_rf_analog.h
 *
 * ESP32-C6 Modem RF Front-End Analog Switch & Antenna Routing Registers
 * Base Address: 0x600AA000
 *
 * Reverse-engineered from ESP32-C6 factory RF test firmware
 * (ESP32-C6_RFTest_V106_4cc3bb5_20250909.bin)
 */

#ifndef IRON_V_MODEM_RF_ANALOG_H
#define IRON_V_MODEM_RF_ANALOG_H

#include <stdint.h>
#include "regs/i2c_ana.h"

/* ========================================================================= */
/* Peripheral Base Address                                                   */
/* ========================================================================= */
#define MODEM_RF_ANALOG_BASE_ADDR               0x600AA000U

/* ========================================================================= */
/* Parameterized MMIO Register Accessor Macro (AGENTS.md rule)               */
/* ========================================================================= */
#define MODEM_RF_ANALOG_REG(offset)             ((volatile uint32_t *)(uintptr_t)(MODEM_RF_ANALOG_BASE_ADDR + (offset)))

/* ========================================================================= */
/* Parameterized RF Analog Switch Macro (AGENTS.md rule)                     */
/* ========================================================================= */
#define MODEM_RF_ANALOG_SWITCH_COUNT            17U
#define MODEM_RF_ANALOG_SWITCH_OFFSET(idx)      ((idx) == 0U ? 0x0008U : (0x002CU + (((idx) - 1U) * 4U)))
#define MODEM_RF_ANALOG_SWITCH_REG(idx)         MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH_OFFSET(idx))

/* ========================================================================= */
/* Register Offsets & Register Definitions                                   */
/* ========================================================================= */
#define MODEM_RF_ANALOG_SWITCH0_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(0U)
#define MODEM_RF_ANALOG_SWITCH0_REG             MODEM_RF_ANALOG_SWITCH_REG(0U)

#define MODEM_RF_ANALOG_SWITCH1_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(1U)
#define MODEM_RF_ANALOG_SWITCH1_REG             MODEM_RF_ANALOG_SWITCH_REG(1U)

#define MODEM_RF_ANALOG_SWITCH2_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(2U)
#define MODEM_RF_ANALOG_SWITCH2_REG             MODEM_RF_ANALOG_SWITCH_REG(2U)

#define MODEM_RF_ANALOG_SWITCH3_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(3U)
#define MODEM_RF_ANALOG_SWITCH3_REG             MODEM_RF_ANALOG_SWITCH_REG(3U)

#define MODEM_RF_ANALOG_SWITCH4_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(4U)
#define MODEM_RF_ANALOG_SWITCH4_REG             MODEM_RF_ANALOG_SWITCH_REG(4U)

#define MODEM_RF_ANALOG_SWITCH5_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(5U)
#define MODEM_RF_ANALOG_SWITCH5_REG             MODEM_RF_ANALOG_SWITCH_REG(5U)

#define MODEM_RF_ANALOG_SWITCH6_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(6U)
#define MODEM_RF_ANALOG_SWITCH6_REG             MODEM_RF_ANALOG_SWITCH_REG(6U)

#define MODEM_RF_ANALOG_SWITCH7_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(7U)
#define MODEM_RF_ANALOG_SWITCH7_REG             MODEM_RF_ANALOG_SWITCH_REG(7U)

#define MODEM_RF_ANALOG_SWITCH8_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(8U)
#define MODEM_RF_ANALOG_SWITCH8_REG             MODEM_RF_ANALOG_SWITCH_REG(8U)

#define MODEM_RF_ANALOG_SWITCH9_OFFSET          MODEM_RF_ANALOG_SWITCH_OFFSET(9U)
#define MODEM_RF_ANALOG_SWITCH9_REG             MODEM_RF_ANALOG_SWITCH_REG(9U)

#define MODEM_RF_ANALOG_SWITCH10_OFFSET         MODEM_RF_ANALOG_SWITCH_OFFSET(10U)
#define MODEM_RF_ANALOG_SWITCH10_REG            MODEM_RF_ANALOG_SWITCH_REG(10U)

#define MODEM_RF_ANALOG_SWITCH11_OFFSET         MODEM_RF_ANALOG_SWITCH_OFFSET(11U)
#define MODEM_RF_ANALOG_SWITCH11_REG            MODEM_RF_ANALOG_SWITCH_REG(11U)

#define MODEM_RF_ANALOG_SWITCH12_OFFSET         MODEM_RF_ANALOG_SWITCH_OFFSET(12U)
#define MODEM_RF_ANALOG_SWITCH12_REG            MODEM_RF_ANALOG_SWITCH_REG(12U)

#define MODEM_RF_ANALOG_SWITCH13_OFFSET         MODEM_RF_ANALOG_SWITCH_OFFSET(13U)
#define MODEM_RF_ANALOG_SWITCH13_REG            MODEM_RF_ANALOG_SWITCH_REG(13U)

#define MODEM_RF_ANALOG_SWITCH14_OFFSET         MODEM_RF_ANALOG_SWITCH_OFFSET(14U)
#define MODEM_RF_ANALOG_SWITCH14_REG            MODEM_RF_ANALOG_SWITCH_REG(14U)

#define MODEM_RF_ANALOG_SWITCH15_OFFSET         MODEM_RF_ANALOG_SWITCH_OFFSET(15U)
#define MODEM_RF_ANALOG_SWITCH15_REG            MODEM_RF_ANALOG_SWITCH_REG(15U)

#define MODEM_RF_ANALOG_SWITCH16_OFFSET         MODEM_RF_ANALOG_SWITCH_OFFSET(16U)
#define MODEM_RF_ANALOG_SWITCH16_REG            MODEM_RF_ANALOG_SWITCH_REG(16U)

/* ========================================================================= */
/* Symbolic Constants & Bitmasks                                             */
/* ========================================================================= */
#define MODEM_RF_ANALOG_SWITCH_TX_ROUTING_BIT   (1U << 0)
#define MODEM_RF_ANALOG_SWITCH_RX_ROUTING_BIT   (1U << 1)
#define MODEM_RF_ANALOG_SWITCH_TX_ROUTING_VAL   0x00000001U
#define MODEM_RF_ANALOG_SWITCH_RX_ROUTING_VAL   0x00000002U
#define MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG   0x00000003U

#endif /* IRON_V_MODEM_RF_ANALOG_H */
