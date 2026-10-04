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
#include "section.h"
#include "modem.h"
#include "regs/wifi_mac.h"

/* ========================================================================= */
/* Software RX Queue Sizing                                                   */
/* ========================================================================= */
#define PACKET_BUFFER_SIZE               1536U
#define PACKET_RING_COUNT                32U

#ifndef WIFI_MAC_ADDR_LEN
#define WIFI_MAC_ADDR_LEN                6U
#endif
#define WIFI_FRAME_MIN_LEN               14U
#define WIFI_FRAME_MAX_LEN               1536U
#define WIFI_MAX_SSID_LEN                32U
#define WIFI_MAX_PASSPHRASE_LEN          64U
#define WIFI_SCAN_DEFAULT_DURATION_MS    20000U
#define WIFI_SCAN_CHANNEL_DWELL_MS       1500U
#define WIFI_SNIFFER_DEFAULT_DURATION_SEC 10U
#define WIFI_SNIFFER_MAX_BEACONS_LOGGED  32U
#define WIFI_STA_START_TIMEOUT_US        5000000ULL
#define WIFI_SCAN_ALL_2G_CHANNELS_MASK   0x7FFEU
#define WIFI_SCAN_BYPASS_5G_MASK         0x0001U
#define WIFI_DEFAULT_SCAN_RSSI_THRESHOLD (-127)

/* SoftAP Geometry & Timing Constants (Task 5.7.2) */
#define WIFI_DEFAULT_AP_SSID            "IronV-C6"
#define WIFI_DEFAULT_AP_CHANNEL         1U
#define WIFI_DEFAULT_AP_MAX_CONN        4U
#define WIFI_DEFAULT_AP_BEACON_INTERVAL_TU 100U
#define WIFI_DEFAULT_AP_DTIM_PERIOD      1U
#define WIFI_DEFAULT_AP_CSA_COUNT        3U
#define WIFI_DEFAULT_COUNTRY_MAX_TX_PWR  20
#define WIFI_DEFAULT_MAX_TX_POWER_INDEX  80
#define WIFI_TX_POWER_DBM_SCALE          4
#define WIFI_AP_MIN_PASSWORD_LEN        8U
#define WIFI_MIN_CHANNEL                1U
#define WIFI_MAX_CHANNEL                14U

/* NVS Table Byte Offsets for IEEE 802.11b Low Rate Configuration */
#define WIFI_NVS_OFFSET_STA_LOW_RATE    1185U
#define WIFI_NVS_OFFSET_AP_LOW_RATE     1341U
#define WIFI_NVS_STUB_DEFAULT_HANDLE    1U
#define WIFI_NVS_LOW_RATE_ENABLED       1U

/* Fallback Event Identifiers for Host Simulation */
#define WIFI_VENDOR_EVENT_AP_START       12
#define WIFI_VENDOR_EVENT_AP_STOP        13
#define WIFI_VENDOR_EVENT_AP_STACONNECTED 14
#define WIFI_VENDOR_EVENT_AP_STADISCONNECTED 15


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
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_AP_ACTIVE
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
    WIFI_ERR_TIMEOUT = -8,
    WIFI_ERR_IF_DOWN = -9,      /* TX interface not started / not connected */
    WIFI_ERR_TX_FAILED = -10    /* Blob rejected the frame (esp_wifi_internal_tx) */
} wifi_status_t;

/* TX interface; values match the blob's wifi_interface_t (WIFI_IF_STA = 0, WIFI_IF_AP = 1) */
typedef enum {
    WIFI_TX_IF_STA = 0,
    WIFI_TX_IF_AP = 1
} wifi_tx_if_t;

/* ========================================================================= */
/* Concrete Data Structures (docs/development-roadmap.md:681-685)            */
/* ========================================================================= */
/**
 * @brief RX queue entry
 *
 * The blob's RX callback copies each frame into one of these and frees its own
 * buffer immediately. dma_desc is only used as a software owner/length record
 * (DMA_OWNER_DMA = free slot, DMA_OWNER_CPU = frame waiting); no GDMA channel
 * touches these buffers.
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
    uint32_t rx_ring_head;
    uint32_t rx_ring_tail;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t ring_full_drops;   /* RX frames dropped because the queue was full */
    uint32_t tx_errors;         /* TX attempts refused (interface down or blob error) */
} wifi_telemetry_t;

/* ========================================================================= */
/* Public Driver APIs                                                        */
/* ========================================================================= */

/* Core lifecycle initialization */
wifi_status_t wifi_init(void);

/* Packet Ring Initialization & Verification */
wifi_status_t wifi_rx_ring_init(void);
wifi_status_t wifi_verify_rx_ring(uint32_t *out_visited_count);

/* Packet Reception (software queue) & Transmission (blob) */
wifi_status_t wifi_rx_poll(net_packet_t **out_packet, uint16_t *out_len);
wifi_status_t wifi_rx_release(net_packet_t *packet);
wifi_status_t wifi_tx_packet(wifi_tx_if_t ifx, const uint8_t *payload, uint16_t len);
/* Interface that owns the IP configuration: STA once associated, otherwise the SoftAP */
wifi_tx_if_t wifi_get_ip_tx_if(void);
#if !defined(__riscv)
/* Host builds: last frame handed to wifi_tx_packet, for tests */
const uint8_t *wifi_host_last_tx(uint16_t *out_len, wifi_tx_if_t *out_ifx);
#endif
void wifi_poll_rx_traffic(void);

/* Identity & Telemetry Queries */
wifi_state_t wifi_get_state(void);
wifi_status_t wifi_get_mac_addr(uint8_t *out_mac);
wifi_status_t wifi_get_ap_mac_addr(uint8_t *out_mac);
wifi_status_t wifi_get_telemetry(wifi_telemetry_t *out_telemetry);
const net_packet_t *wifi_get_rx_packet(uint32_t index);

/* Event Handling from Vendor Wi-Fi Stack */
void wifi_handle_vendor_event(int32_t event_id, void *event_data);

/* Active/Passive Scanning & Sniffing */
wifi_status_t wifi_scan(const char *ssid, uint8_t channel, bool passive, uint32_t duration_ms) FLASH_TEXT_ATTR;
wifi_status_t wifi_sniffer(uint8_t channel, uint32_t duration_sec) FLASH_TEXT_ATTR;

/* Baseband DMA Linkage & Timings (Task 3) */
uint32_t wifi_get_bb_tx_on_delay(void);
uint32_t wifi_get_tx_ramp_delay(void);
uint32_t wifi_get_tx_cca_start_ts(void);

/* SoftAP Broadcasting Subsystem (Task 5.7.2) */
wifi_status_t wifi_start_ap(const char *ssid, const char *password, uint8_t channel);
wifi_status_t wifi_stop_ap(void);
bool wifi_is_ap_active(void);
/* Seconds of station silence before the blob deauths it (0 if unavailable) */
uint16_t wifi_get_inactive_time_s(wifi_tx_if_t ifx);
const char *wifi_get_ap_ssid(void);
uint8_t wifi_get_ap_channel(void);

/* PHY CCA Control APIs (Task 5.7.3) */
wifi_status_t wifi_set_cca_enabled(bool enabled);
bool wifi_is_cca_enabled(void);

/* Wi-Fi Station (STA) Subsystem (Task 8.2) */
wifi_status_t wifi_start_sta(const char *ssid, const char *password) FLASH_TEXT_ATTR;
wifi_status_t wifi_start_sta_chan(const char *ssid, const char *password, uint8_t channel) FLASH_TEXT_ATTR;
wifi_status_t wifi_stop_sta(void) FLASH_TEXT_ATTR;
bool wifi_is_sta_connected(void) FLASH_TEXT_ATTR;
wifi_status_t wifi_sta_get_bssid(uint8_t *out_bssid) FLASH_TEXT_ATTR;

#endif /* IRON_V_WIFI_H */



