/*
 * src/ieee802154.h
 *
 * ESP32-C6 IEEE 802.15.4 (Zigbee / Thread) Radio Transceiver Driver
 * TRM Chapter 30 (IEEE 802.15.4 Subsystem) & Chapter 8 (MODEM_SYSCON)
 *
 * Defines memory-mapped register accessors, 2.4 GHz channel frequencies,
 * hardware auto-ACK control flags, transceiver state machines, and public APIs.
 */

#ifndef IRON_V_IEEE802154_H
#define IRON_V_IEEE802154_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"

/* ========================================================================= */
/* Hardware Register Block Base & Offsets (TRM §30.4)                         */
/* ========================================================================= */
#define IEEE802154_BASE_ADDR                 0x600A3000U

#define IEEE802154_COMMAND_OFFSET            0x0000U
#define IEEE802154_CTRL_CFG_OFFSET           0x0004U
#define IEEE802154_INF0_SHORT_ADDR_OFFSET    0x0008U
#define IEEE802154_INF0_PAN_ID_OFFSET        0x000CU
#define IEEE802154_INF0_EXTEND_ADDR0_OFFSET  0x0010U
#define IEEE802154_INF0_EXTEND_ADDR1_OFFSET  0x0014U
#define IEEE802154_CHANNEL_OFFSET            0x0048U
#define IEEE802154_TX_POWER_OFFSET           0x004CU
#define IEEE802154_RX_STATUS_OFFSET          0x0080U
#define IEEE802154_TX_STATUS_OFFSET          0x0084U
#define IEEE802154_TXRX_STATUS_OFFSET        0x0088U
#define IEEE802154_CORE_GCK_CFG_OFFSET       0x0090U
#define IEEE802154_MAC_DATE_OFFSET           0x0184U

/* Parameterized MMIO Register Accessors (AGENTS.md rule) */
#define IEEE802154_REG(offset)               ((volatile uint32_t *)(IEEE802154_BASE_ADDR + (offset)))

#define IEEE802154_COMMAND_REG               IEEE802154_REG(IEEE802154_COMMAND_OFFSET)
#define IEEE802154_CTRL_CFG_REG              IEEE802154_REG(IEEE802154_CTRL_CFG_OFFSET)
#define IEEE802154_INF0_SHORT_ADDR_REG       IEEE802154_REG(IEEE802154_INF0_SHORT_ADDR_OFFSET)
#define IEEE802154_INF0_PAN_ID_REG           IEEE802154_REG(IEEE802154_INF0_PAN_ID_OFFSET)
#define IEEE802154_INF0_EXTEND_ADDR0_REG     IEEE802154_REG(IEEE802154_INF0_EXTEND_ADDR0_OFFSET)
#define IEEE802154_INF0_EXTEND_ADDR1_REG     IEEE802154_REG(IEEE802154_INF0_EXTEND_ADDR1_OFFSET)
#define IEEE802154_CHANNEL_REG               IEEE802154_REG(IEEE802154_CHANNEL_OFFSET)
#define IEEE802154_TX_POWER_REG              IEEE802154_REG(IEEE802154_TX_POWER_OFFSET)
#define IEEE802154_RX_STATUS_REG             IEEE802154_REG(IEEE802154_RX_STATUS_OFFSET)
#define IEEE802154_TX_STATUS_REG             IEEE802154_REG(IEEE802154_TX_STATUS_OFFSET)
#define IEEE802154_TXRX_STATUS_REG           IEEE802154_REG(IEEE802154_TXRX_STATUS_OFFSET)
#define IEEE802154_CORE_GCK_CFG_REG          IEEE802154_REG(IEEE802154_CORE_GCK_CFG_OFFSET)
#define IEEE802154_MAC_DATE_REG              IEEE802154_REG(IEEE802154_MAC_DATE_OFFSET)

/* ========================================================================= */
/* Register Bitfield Definitions & Masks                                     */
/* ========================================================================= */
#define IEEE802154_MAC_DATE_EXPECTED         0x00220622U

/* Control Configuration Bits (IEEE802154_CTRL_CFG_REG) */
#define IEEE802154_CTRL_AUTO_ACK_TX_BIT      (1U << 0)
#define IEEE802154_CTRL_ENH_ACK_TX_BIT       (1U << 1)
#define IEEE802154_CTRL_AUTO_ACK_RX_BIT      (1U << 3)
#define IEEE802154_CTRL_PROMISCUOUS_BIT      (1U << 7)
#define IEEE802154_CTRL_MAC_INF0_EN_BIT      (1U << 28)

/* 2.4 GHz Channel Parameters (Channels 11 - 26) */
#define IEEE802154_CHANNEL_MIN               11U
#define IEEE802154_CHANNEL_MAX               26U
#define IEEE802154_CHANNEL_COUNT             16U
#define IEEE802154_CHANNEL_DEFAULT           CONFIG_IEEE802154_DEFAULT_CHANNEL

#define IEEE802154_FREQ_BASE_MHZ             2405U
#define IEEE802154_FREQ_STEP_MHZ             5U

/* Addressing & Geometry Constants */
#define IEEE802154_DEFAULT_SHORT_ADDR        CONFIG_IEEE802154_DEFAULT_SHORT_ADDR
#define IEEE802154_DEFAULT_PAN_ID            CONFIG_IEEE802154_DEFAULT_PAN_ID
#define IEEE802154_BROADCAST_ADDR            0xFFFFU
#define IEEE802154_EXT_ADDR_LEN              8U

/* Transmit Power */
#define IEEE802154_TX_POWER_DEFAULT          CONFIG_IEEE802154_DEFAULT_TX_POWER
#define IEEE802154_TX_POWER_MAX              0x1FU

/* eFuse Memory Base for Extended Address Generation */
#ifndef EFUSE_CONTROLLER_BASE
#define EFUSE_CONTROLLER_BASE                0x600B0800U
#define EFUSE_MAC_SYS_0_OFFSET               0x0044U
#define EFUSE_MAC_SYS_1_OFFSET               0x0048U
#define EFUSE_MAC_SYS_0_REG                  ((volatile uint32_t *)(EFUSE_CONTROLLER_BASE + EFUSE_MAC_SYS_0_OFFSET))
#define EFUSE_MAC_SYS_1_REG                  ((volatile uint32_t *)(EFUSE_CONTROLLER_BASE + EFUSE_MAC_SYS_1_OFFSET))
#endif

/* ========================================================================= */
/* Command, State & Status Enumerations                                      */
/* ========================================================================= */
typedef enum {
    IEEE802154_CMD_TX_START = 0x01,
    IEEE802154_CMD_RX_START = 0x02,
    IEEE802154_CMD_CCA_START = 0x03,
    IEEE802154_CMD_FORCE_TRX_OFF = 0x04
} ieee802154_cmd_t;

typedef enum {
    IEEE802154_STATE_DISABLE = 0,
    IEEE802154_STATE_IDLE,
    IEEE802154_STATE_TRX_OFF,
    IEEE802154_STATE_RX,
    IEEE802154_STATE_TX,
    IEEE802154_STATE_CCA
} ieee802154_state_t;

typedef enum {
    IEEE802154_OK = 0,
    IEEE802154_ERR_INVALID_ARG = -1,
    IEEE802154_ERR_NOT_INITIALIZED = -2,
    IEEE802154_ERR_BUSY = -3,
    IEEE802154_ERR_TIMEOUT = -4,
    IEEE802154_ERR_STATE = -5
} ieee802154_status_t;

/* ========================================================================= */
/* Telemetry Data Structure                                                  */
/* ========================================================================= */
typedef struct {
    ieee802154_state_t state;
    uint8_t channel;
    uint16_t freq_mhz;
    uint16_t short_addr;
    uint16_t pan_id;
    uint8_t ext_addr[IEEE802154_EXT_ADDR_LEN];
    uint8_t tx_power;
    bool auto_ack_tx;
    bool auto_ack_rx;
    bool promiscuous;
    uint32_t date_version;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t cca_fail_count;
} ieee802154_telemetry_t;

/* ========================================================================= */
/* Public Driver APIs                                                        */
/* ========================================================================= */

/* Core lifecycle initialization */
ieee802154_status_t ieee802154_init(void);

/* Transceiver command dispatcher */
ieee802154_status_t ieee802154_cmd(ieee802154_cmd_t cmd);

/* RF Channel & Frequency Configuration */
ieee802154_status_t ieee802154_set_channel(uint8_t channel);
uint8_t ieee802154_get_channel(void);
uint16_t ieee802154_get_freq_mhz(uint8_t channel);

/* MAC Addressing (Short, PAN ID, Extended EUI-64) */
ieee802154_status_t ieee802154_set_short_address(uint16_t addr);
uint16_t ieee802154_get_short_address(void);
ieee802154_status_t ieee802154_set_pan_id(uint16_t pan_id);
uint16_t ieee802154_get_pan_id(void);
ieee802154_status_t ieee802154_set_extended_address(const uint8_t *ext_addr);
ieee802154_status_t ieee802154_get_extended_address(uint8_t *out_addr);

/* Hardware Frame Filtering & ACK Configuration */
ieee802154_status_t ieee802154_set_auto_ack(bool tx_ack, bool rx_ack);
ieee802154_status_t ieee802154_set_promiscuous(bool enable);

/* RF Output Power */
ieee802154_status_t ieee802154_set_tx_power(uint8_t power);
uint8_t ieee802154_get_tx_power(void);

/* State & Telemetry Accessors */
ieee802154_state_t ieee802154_get_state(void);
ieee802154_status_t ieee802154_get_telemetry(ieee802154_telemetry_t *out_telem);
uint32_t ieee802154_get_date_version(void);

#endif /* IRON_V_IEEE802154_H */
