/*
 * src/regs/ble_ll.h
 *
 * ESP32-C6 Bluetooth 5 (LE) Link Layer Hardware Register Definitions
 * Base Address: 0x600B0000
 *
 * Implements bare-metal Link Layer command registers, clock routing,
 * baseband modem linking, and hardware initialization sequences.
 */

#ifndef IRON_V_REGS_BLE_LL_H
#define IRON_V_REGS_BLE_LL_H

#include <stdint.h>

/* ========================================================================= */
/* Peripheral Base Address                                                   */
/* ========================================================================= */
#define BLE_LL_BASE_ADDR                        0x600B0000U

/* ========================================================================= */
/* Parameterized MMIO Register Accessor Macro (AGENTS.md rule)               */
/* ========================================================================= */
#define BLE_LL_REG(offset)                      ((volatile uint32_t *)(BLE_LL_BASE_ADDR + (offset)))

/* ========================================================================= */
/* Register Offsets & Register Definitions                                   */
/* ========================================================================= */
#define BLE_LL_CMD_OFFSET                       0x000CU
#define BLE_LL_CMD_REG                          BLE_LL_REG(BLE_LL_CMD_OFFSET)

#define BLE_LL_STATUS_OFFSET                    0x0058U
#define BLE_LL_STATUS_REG                       BLE_LL_REG(BLE_LL_STATUS_OFFSET)

#define BLE_LL_CLK_LINK_OFFSET                  0x00CCU
#define BLE_LL_CLK_LINK_REG                     BLE_LL_REG(BLE_LL_CLK_LINK_OFFSET)

#define BLE_LL_MODEM_LINK_OFFSET                0x0154U
#define BLE_LL_MODEM_LINK_REG                   BLE_LL_REG(BLE_LL_MODEM_LINK_OFFSET)

/* ========================================================================= */
/* Bitmasks and Symbolic Constants                                           */
/* ========================================================================= */
#define BLE_LL_CMD_TRIG_CLR_MASK                0x3FFFFFFFU
#define BLE_LL_CMD_START_ADV_BIT                0x80000000U
#define BLE_LL_STATUS_TIMEOUT_US                10000U
#define BLE_LL_MODEM_LINK_MASK_1                0x00010000U
#define BLE_LL_MODEM_LINK_MASK_2                0x000F0000U

/* Clock link masks (sequentially applied per RE report) */
#define BLE_LL_CLK_LINK_MASK_1                  0x00010000U
#define BLE_LL_CLK_LINK_MASK_2                  0x00080000U
#define BLE_LL_CLK_LINK_MASK_3                  0x00000001U
#define BLE_LL_CLK_LINK_MASK_4                  0x00000030U
#define BLE_LL_CLK_LINK_MASK_5                  0x00070000U
#define BLE_LL_CLK_LINK_MASK_6                  0x00002000U

#endif /* IRON_V_REGS_BLE_LL_H */
