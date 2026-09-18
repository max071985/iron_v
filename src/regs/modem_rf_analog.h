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

/* ========================================================================= */
/* Peripheral Base Address                                                   */
/* ========================================================================= */
#define MODEM_RF_ANALOG_BASE_ADDR               0x600AA000U

/* ========================================================================= */
/* Parameterized MMIO Register Accessor Macro (AGENTS.md rule)               */
/* ========================================================================= */
#define MODEM_RF_ANALOG_REG(offset)             ((volatile uint32_t *)(MODEM_RF_ANALOG_BASE_ADDR + (offset)))

/* ========================================================================= */
/* Register Offsets & Register Definitions                                   */
/* ========================================================================= */
#define MODEM_RF_ANALOG_SWITCH0_OFFSET          0x0008U
#define MODEM_RF_ANALOG_SWITCH0_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH0_OFFSET)

#define MODEM_RF_ANALOG_SWITCH1_OFFSET          0x002CU
#define MODEM_RF_ANALOG_SWITCH1_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH1_OFFSET)

#define MODEM_RF_ANALOG_SWITCH2_OFFSET          0x0030U
#define MODEM_RF_ANALOG_SWITCH2_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH2_OFFSET)

#define MODEM_RF_ANALOG_SWITCH3_OFFSET          0x0034U
#define MODEM_RF_ANALOG_SWITCH3_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH3_OFFSET)

#define MODEM_RF_ANALOG_SWITCH4_OFFSET          0x0038U
#define MODEM_RF_ANALOG_SWITCH4_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH4_OFFSET)

#define MODEM_RF_ANALOG_SWITCH5_OFFSET          0x003CU
#define MODEM_RF_ANALOG_SWITCH5_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH5_OFFSET)

#define MODEM_RF_ANALOG_SWITCH6_OFFSET          0x0040U
#define MODEM_RF_ANALOG_SWITCH6_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH6_OFFSET)

#define MODEM_RF_ANALOG_SWITCH7_OFFSET          0x0044U
#define MODEM_RF_ANALOG_SWITCH7_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH7_OFFSET)

#define MODEM_RF_ANALOG_SWITCH8_OFFSET          0x0048U
#define MODEM_RF_ANALOG_SWITCH8_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH8_OFFSET)

#define MODEM_RF_ANALOG_SWITCH9_OFFSET          0x004CU
#define MODEM_RF_ANALOG_SWITCH9_REG             MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH9_OFFSET)

#define MODEM_RF_ANALOG_SWITCH10_OFFSET         0x0050U
#define MODEM_RF_ANALOG_SWITCH10_REG            MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH10_OFFSET)

#define MODEM_RF_ANALOG_SWITCH11_OFFSET         0x0054U
#define MODEM_RF_ANALOG_SWITCH11_REG            MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH11_OFFSET)

#define MODEM_RF_ANALOG_SWITCH12_OFFSET         0x0058U
#define MODEM_RF_ANALOG_SWITCH12_REG            MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH12_OFFSET)

#define MODEM_RF_ANALOG_SWITCH13_OFFSET         0x005CU
#define MODEM_RF_ANALOG_SWITCH13_REG            MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH13_OFFSET)

#define MODEM_RF_ANALOG_SWITCH14_OFFSET         0x0060U
#define MODEM_RF_ANALOG_SWITCH14_REG            MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH14_OFFSET)

#define MODEM_RF_ANALOG_SWITCH15_OFFSET         0x0064U
#define MODEM_RF_ANALOG_SWITCH15_REG            MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH15_OFFSET)

#define MODEM_RF_ANALOG_SWITCH16_OFFSET         0x0068U
#define MODEM_RF_ANALOG_SWITCH16_REG            MODEM_RF_ANALOG_REG(MODEM_RF_ANALOG_SWITCH16_OFFSET)

/* ========================================================================= */
/* Symbolic Constants & Bitmasks                                             */
/* ========================================================================= */
#define MODEM_RF_ANALOG_SWITCH_TX_ROUTING_VAL   0x00000001U
#define MODEM_RF_ANALOG_SWITCH_RX_ROUTING_VAL   0x00000002U
#define MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG   0x00000003U

#endif /* IRON_V_MODEM_RF_ANALOG_H */
