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
/* Wi-Fi Hardware TSF Microsecond Timer Registers (0x600AD000)               */
/* ========================================================================= */
#define WIFI_MAC_TSF_TIMER_LOW_OFFSET           0x0000U
#define WIFI_MAC_TSF_TIMER_LOW_REG              MODEM_DATA_REG(WIFI_MAC_TSF_TIMER_LOW_OFFSET)

#define WIFI_MAC_TSF_TIMER_HIGH_OFFSET          0x0004U
#define WIFI_MAC_TSF_TIMER_HIGH_REG             MODEM_DATA_REG(WIFI_MAC_TSF_TIMER_HIGH_OFFSET)

#define WIFI_MAC_TSF_TBTT0_CONF_OFFSET          0x0050U
#define WIFI_MAC_TSF_TBTT0_CONF_REG             MODEM_DATA_REG(WIFI_MAC_TSF_TBTT0_CONF_OFFSET)

#define WIFI_MAC_TSF_TBTT1_CONF_OFFSET          0x0058U
#define WIFI_MAC_TSF_TBTT1_CONF_REG             MODEM_DATA_REG(WIFI_MAC_TSF_TBTT1_CONF_OFFSET)

#define WIFI_MAC_TSF_TBTT_ENABLE_BIT            (1U << 26)

#define WIFI_MAC_TSF_TBTT_TRIG0_OFFSET          0x00A8U
#define WIFI_MAC_TSF_TBTT_TRIG0_REG             MODEM_DATA_REG(WIFI_MAC_TSF_TBTT_TRIG0_OFFSET)

#define WIFI_MAC_TSF_TBTT_TRIG1_OFFSET          0x00B4U
#define WIFI_MAC_TSF_TBTT_TRIG1_REG             MODEM_DATA_REG(WIFI_MAC_TSF_TBTT_TRIG1_OFFSET)

#define WIFI_MAC_TSF_TBTT_TRIG_ENABLE_BIT       (1U << 3)

/* Backward compatibility aliases for test assertions */
#define MODEM_DATA_RF_DMA_DESC_ADDR_OFFSET      WIFI_MAC_TSF_TIMER_LOW_OFFSET
#define MODEM_DATA_RF_DMA_DESC_ADDR_REG         WIFI_MAC_TSF_TIMER_LOW_REG

#define MODEM_DATA_TX_DMA_DESC_ADDR_OFFSET      WIFI_MAC_TSF_TIMER_HIGH_OFFSET
#define MODEM_DATA_TX_DMA_DESC_ADDR_REG         WIFI_MAC_TSF_TIMER_HIGH_REG

#define MODEM_DATA_BLE_DMA_DESC_ADDR_OFFSET     0x0008U
#define MODEM_DATA_BLE_DMA_DESC_ADDR_REG        MODEM_DATA_REG(MODEM_DATA_BLE_DMA_DESC_ADDR_OFFSET)

#define MODEM_SYSCON_RF_DMA_ADDR_REG            MODEM_DATA_RF_DMA_DESC_ADDR_REG

/* ========================================================================= */
/* Wi-Fi MAC Hardware BSSID Filter Registers (0x600A4000)                    */
/* ========================================================================= */
#define WIFI_MAC_BSSID0_LOW_OFFSET              0x0000U
#define WIFI_MAC_BSSID0_LOW_REG                 WIFI_MAC_REG(WIFI_MAC_BSSID0_LOW_OFFSET)

#define WIFI_MAC_BSSID0_HIGH_OFFSET             0x0004U
#define WIFI_MAC_BSSID0_HIGH_REG                WIFI_MAC_REG(WIFI_MAC_BSSID0_HIGH_OFFSET)

#define WIFI_MAC_BSSID1_LOW_OFFSET              0x0010U
#define WIFI_MAC_BSSID1_LOW_REG                 WIFI_MAC_REG(WIFI_MAC_BSSID1_LOW_OFFSET)

#define WIFI_MAC_BSSID1_HIGH_OFFSET             0x0014U
#define WIFI_MAC_BSSID1_HIGH_REG                WIFI_MAC_REG(WIFI_MAC_BSSID1_HIGH_OFFSET)

#define WIFI_MAC_BSSID2_LOW_OFFSET              0x0018U
#define WIFI_MAC_BSSID2_LOW_REG                 WIFI_MAC_REG(WIFI_MAC_BSSID2_LOW_OFFSET)

#define WIFI_MAC_BSSID2_HIGH_OFFSET             0x001CU
#define WIFI_MAC_BSSID2_HIGH_REG                WIFI_MAC_REG(WIFI_MAC_BSSID2_HIGH_OFFSET)

/* Backward compatibility aliases for delay test registers */
#define WIFI_MAC_BB_TX_ON_DELAY_OFFSET          WIFI_MAC_BSSID1_LOW_OFFSET
#define WIFI_MAC_BB_TX_ON_DELAY_REG             WIFI_MAC_BSSID1_LOW_REG

#define WIFI_MAC_TX_RAMP_DELAY_OFFSET           WIFI_MAC_BSSID1_HIGH_OFFSET
#define WIFI_MAC_TX_RAMP_DELAY_REG              WIFI_MAC_BSSID1_HIGH_REG

#define WIFI_MAC_TX_CCA_START_TS_OFFSET         WIFI_MAC_BSSID2_LOW_OFFSET
#define WIFI_MAC_TX_CCA_START_TS_REG            WIFI_MAC_BSSID2_LOW_REG

#define WIFI_MAC_TX_CCA_END_TS_OFFSET           WIFI_MAC_BSSID2_HIGH_OFFSET
#define WIFI_MAC_TX_CCA_END_TS_REG              WIFI_MAC_BSSID2_HIGH_REG

/* ========================================================================= */
/* Wi-Fi MAC CCA and Hardware Debug Override Registers                       */
/* ========================================================================= */
#define WIFI_MAC_PHY_CCA_CTRL_OFFSET            0x0C5CU
#define WIFI_MAC_PHY_CCA_CTRL_REG               WIFI_MAC_REG(WIFI_MAC_PHY_CCA_CTRL_OFFSET)
#define WIFI_MAC_PHY_CCA_BUSY_FORCE_BIT31       (1U << 31)
#define WIFI_MAC_PHY_CCA_BUSY_FORCE_BIT30       (1U << 30)
#define WIFI_MAC_PHY_CCA_BUSY_FORCE_MASK        (WIFI_MAC_PHY_CCA_BUSY_FORCE_BIT31 | WIFI_MAC_PHY_CCA_BUSY_FORCE_BIT30)

/* Legacy alias for test compatibility */
#define WIFI_MAC_PHY_CCA_DISABLE_BIT31          WIFI_MAC_PHY_CCA_BUSY_FORCE_BIT31
#define WIFI_MAC_PHY_CCA_DISABLE_BIT29          (1U << 29)
#define WIFI_MAC_PHY_CCA_DISABLE_MASK           WIFI_MAC_PHY_CCA_BUSY_FORCE_MASK

#define WIFI_MAC_DBG_CTRL_OFFSET                0x0C7CU
#define WIFI_MAC_DBG_CTRL_REG                   WIFI_MAC_REG(WIFI_MAC_DBG_CTRL_OFFSET)
#define WIFI_MAC_DBG_TB_IGNORE_CCA_ENABLE_BIT   (1U << 12)

#define WIFI_MAC_TX_Q0_PTI_OFFSET               0x0D68U
#define WIFI_MAC_TX_Q0_PTI_REG                  WIFI_MAC_REG(WIFI_MAC_TX_Q0_PTI_OFFSET)

#define WIFI_MAC_TX_Q0_DMA_OFFSET               0x0D6CU
#define WIFI_MAC_TX_Q0_DMA_REG                  WIFI_MAC_REG(WIFI_MAC_TX_Q0_DMA_OFFSET)

/* ========================================================================= */
/* Wi-Fi MAC Hardware Rate Control Registers (0x600A4440 - 0x600A4450)        */
/* ========================================================================= */
#define WIFI_MAC_RATE_CTRL0_OFFSET              0x0440U
#define WIFI_MAC_RATE_CTRL0_REG                 WIFI_MAC_REG(WIFI_MAC_RATE_CTRL0_OFFSET)

#define WIFI_MAC_RATE_CTRL1_OFFSET              0x0444U
#define WIFI_MAC_RATE_CTRL1_REG                 WIFI_MAC_REG(WIFI_MAC_RATE_CTRL1_OFFSET)

#define WIFI_MAC_RATE_CTRL2_OFFSET              0x044CU
#define WIFI_MAC_RATE_CTRL2_REG                 WIFI_MAC_REG(WIFI_MAC_RATE_CTRL2_OFFSET)

#define WIFI_MAC_RATE_CTRL3_OFFSET              0x0450U
#define WIFI_MAC_RATE_CTRL3_REG                 WIFI_MAC_REG(WIFI_MAC_RATE_CTRL3_OFFSET)

#define WIFI_MAC_RATE_LOW_RATE_ENABLE_VAL       0x0B0B0B0BU

/* ========================================================================= */
/* Timing Constants (Safe standard 802.11 defaults in microseconds)          */
/* Zero magic numbers execution standard (AGENTS.md rule)                    */
/* ========================================================================= */
#define WIFI_MAC_DEFAULT_BB_TX_ON_DELAY_US      50U
#define WIFI_MAC_DEFAULT_TX_RAMP_DELAY_US       60U
#define WIFI_MAC_DEFAULT_TX_CCA_START_TS_US     80U
#define WIFI_MAC_DEFAULT_TX_CCA_END_TS_US       100U

#endif /* IRON_V_WIFI_MAC_H */
