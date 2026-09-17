/*
 * src/regs/wifi_mac.h
 *
 * ESP32-C6 802.11ax Wi-Fi MAC / PHY Configuration & Baseband Timing Registers
 * Base Addresses:
 *   - WiFi MAC / PHY Config: 0x600A4000
 *   - Modem SYSCON / Data DMA: 0x600AD000
 *
 * Reverse-engineered from ESP32-C6 factory RF test firmware
 * (ESP32-C6_RFTest_V106_4cc3bb5_20250909.bin)
 */

#ifndef IRON_V_WIFI_MAC_H
#define IRON_V_WIFI_MAC_H

#include <stdint.h>

/* ========================================================================= */
/* Peripheral Base Addresses                                                 */
/* ========================================================================= */
#define WIFI_MAC_BASE_ADDR                      0x600A4000U
#define MODEM_DATA_BASE_ADDR                    0x600AD000U

/* ========================================================================= */
/* Parameterized MMIO Register Accessor Macros (AGENTS.md rule)              */
/* ========================================================================= */
#define WIFI_MAC_REG(offset)                    ((volatile uint32_t *)(WIFI_MAC_BASE_ADDR + (offset)))
#define MODEM_DATA_REG(offset)                  ((volatile uint32_t *)(MODEM_DATA_BASE_ADDR + (offset)))

/* ========================================================================= */
/* Modem Data & DMA Buffer Linkage Register Offsets (0x600AD000)             */
/* ========================================================================= */
#define MODEM_DATA_RF_DMA_DESC_ADDR_OFFSET      0x0000U
#define MODEM_DATA_RF_DMA_DESC_ADDR_REG         MODEM_DATA_REG(MODEM_DATA_RF_DMA_DESC_ADDR_OFFSET)

#define MODEM_DATA_TX_DMA_DESC_ADDR_OFFSET      0x0004U
#define MODEM_DATA_TX_DMA_DESC_ADDR_REG         MODEM_DATA_REG(MODEM_DATA_TX_DMA_DESC_ADDR_OFFSET)

#define MODEM_DATA_BLE_DMA_DESC_ADDR_OFFSET     0x0008U
#define MODEM_DATA_BLE_DMA_DESC_ADDR_REG        MODEM_DATA_REG(MODEM_DATA_BLE_DMA_DESC_ADDR_OFFSET)

/* Alias for compatibility with implementation plan */
#define MODEM_SYSCON_RF_DMA_ADDR_REG            MODEM_DATA_RF_DMA_DESC_ADDR_REG

/* ========================================================================= */
/* Wi-Fi MAC / PHY Timing Register Offsets (0x600A4000)                      */
/* ========================================================================= */
#define WIFI_MAC_BB_TX_ON_DELAY_OFFSET          0x0010U
#define WIFI_MAC_BB_TX_ON_DELAY_REG             WIFI_MAC_REG(WIFI_MAC_BB_TX_ON_DELAY_OFFSET)

#define WIFI_MAC_TX_RAMP_DELAY_OFFSET           0x0014U
#define WIFI_MAC_TX_RAMP_DELAY_REG              WIFI_MAC_REG(WIFI_MAC_TX_RAMP_DELAY_OFFSET)

#define WIFI_MAC_TX_CCA_START_TS_OFFSET         0x0018U
#define WIFI_MAC_TX_CCA_START_TS_REG            WIFI_MAC_REG(WIFI_MAC_TX_CCA_START_TS_OFFSET)

#define WIFI_MAC_TX_CCA_END_TS_OFFSET           0x001CU
#define WIFI_MAC_TX_CCA_END_TS_REG              WIFI_MAC_REG(WIFI_MAC_TX_CCA_END_TS_OFFSET)

/* ========================================================================= */
/* Timing Constants (Safe standard 802.11 defaults in microseconds)          */
/* Zero magic numbers execution standard (AGENTS.md rule)                    */
/* ========================================================================= */
#define WIFI_MAC_DEFAULT_BB_TX_ON_DELAY_US      50U
#define WIFI_MAC_DEFAULT_TX_RAMP_DELAY_US       60U
#define WIFI_MAC_DEFAULT_TX_CCA_START_TS_US     80U
#define WIFI_MAC_DEFAULT_TX_CCA_END_TS_US       100U

#endif /* IRON_V_WIFI_MAC_H */
