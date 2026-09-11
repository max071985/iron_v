/*
 * src/wifi.h
 *
 * ESP32-C6 802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Circular Packet Ring
 * TRM Chapter 4 (GDMA Controller) & Chapter 8 (Reset and Clock / MODEM_SYSCON)
 *
 * Defines static net_packet_t circular buffer rings, zero-copy packet descriptors,
 * GDMA Channel 1 binding, hardware MAC extraction, and public Wi-Fi driver APIs.
 */

#ifndef IRON_V_WIFI_H
#define IRON_V_WIFI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "gdma.h"
#include "modem.h"

/* ========================================================================= */
/* Packet Ring Sizing & Geometry Constants (docs/development-roadmap.md:678) */
/* ========================================================================= */
#define PACKET_BUFFER_SIZE               1536U
#define PACKET_RING_COUNT                32U
#define WIFI_TX_RING_COUNT               8U

#define WIFI_MAC_ADDR_LEN                6U
#define WIFI_FRAME_MIN_LEN               14U
#define WIFI_FRAME_MAX_LEN               1536U

/* Dedicated GDMA Channel for Wi-Fi MAC Data Transfer */
#define WIFI_GDMA_CHANNEL                GDMA_CHANNEL_1

/* Memory Cartography Boundaries for HP SRAM DRAM Descriptor Validation   */
#define WIFI_DRAM_START_ADDR             0x40820000U
#define WIFI_DRAM_END_ADDR               0x40880000U

/* Bounded Polling Timeout */
#define WIFI_DEFAULT_TIMEOUT_US          1000U

/* ========================================================================= */
/* eFuse Memory-Mapped Registers for MAC Address Extraction                  */
/* ========================================================================= */
#ifndef EFUSE_CONTROLLER_BASE
#define EFUSE_CONTROLLER_BASE            0x600B0800U
#define EFUSE_MAC_SYS_0_OFFSET           0x0044U
#define EFUSE_MAC_SYS_1_OFFSET           0x0048U

#define EFUSE_MAC_SYS_0_REG              ((volatile uint32_t *)(EFUSE_CONTROLLER_BASE + EFUSE_MAC_SYS_0_OFFSET))
#define EFUSE_MAC_SYS_1_REG              ((volatile uint32_t *)(EFUSE_CONTROLLER_BASE + EFUSE_MAC_SYS_1_OFFSET))
#endif

/* ========================================================================= */
/* Driver State & Status Enumerations                                        */
/* ========================================================================= */
typedef enum {
    WIFI_STATE_OFF = 0,
    WIFI_STATE_INIT,
    WIFI_STATE_IDLE,
    WIFI_STATE_ACTIVE,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED
} wifi_state_t;

typedef enum {
    WIFI_OK = 0,
    WIFI_ERR_INVALID_ARG = -1,
    WIFI_ERR_UNALIGNED = -2,
    WIFI_ERR_OUT_OF_BOUNDS = -3,
    WIFI_ERR_RING_FULL = -4,
    WIFI_ERR_RING_EMPTY = -5,
    WIFI_ERR_NOT_INITIALIZED = -6,
    WIFI_ERR_DMA_FAULT = -7,
    WIFI_ERR_TIMEOUT = -8
} wifi_status_t;

/* ========================================================================= */
/* Concrete Data Structures (docs/development-roadmap.md:681-685)            */
/* ========================================================================= */
/**
 * @brief Zero-Copy Network Packet Structure
 *
 * Embeds a hardware 12-byte GDMA linked list descriptor directly adjacent
 * to a fixed 1536-byte payload buffer in HP SRAM DRAM, ensuring zero-copy
 * transfer from receiver hardware straight to upper-layer protocol parsers.
 */
typedef struct {
    dma_descriptor_t dma_desc;
    uint8_t payload[PACKET_BUFFER_SIZE];
} __attribute__((aligned(4))) net_packet_t;

/**
 * @brief Wi-Fi MAC Subsystem Telemetry Structure
 */
typedef struct {
    wifi_state_t state;
    uint8_t mac_addr[WIFI_MAC_ADDR_LEN];
    uint32_t rx_ring_capacity;
    uint32_t tx_ring_capacity;
    uint32_t rx_ring_head;
    uint32_t rx_ring_tail;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t ring_full_drops;
    uint32_t dma_err_count;
} wifi_telemetry_t;

/* ========================================================================= */
/* Public Driver APIs                                                        */
/* ========================================================================= */

/* Core lifecycle initialization */
wifi_status_t wifi_init(void);

/* Packet Ring Initialization & Verification */
wifi_status_t wifi_rx_ring_init(void);
wifi_status_t wifi_tx_ring_init(void);
wifi_status_t wifi_verify_rx_ring(uint32_t *out_visited_count);

/* Zero-Copy Packet Transmission & Reception */
wifi_status_t wifi_rx_poll(net_packet_t **out_packet, uint16_t *out_len);
wifi_status_t wifi_rx_release(net_packet_t *packet);
wifi_status_t wifi_tx_packet(const uint8_t *payload, uint16_t len);

/* Identity & Telemetry Queries */
wifi_state_t wifi_get_state(void);
wifi_status_t wifi_get_mac_addr(uint8_t *out_mac);
wifi_status_t wifi_get_telemetry(wifi_telemetry_t *out_telemetry);
const net_packet_t *wifi_get_rx_packet(uint32_t index);

#endif /* IRON_V_WIFI_H */
