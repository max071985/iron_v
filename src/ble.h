/*
 * src/ble.h
 *
 * ESP32-C6 Bluetooth 5 (LE) Controller Driver, HCI Transport & GAP State Machine
 * Bluetooth Core Specification v5.3 & TRM Chapter 8 / Chapter 10
 *
 * Implements bare-metal HCI command assembly, response parsing, Link Layer
 * lifecycle orchestration, GAP advertising management, and hardware telemetry.
 */

#ifndef IRON_V_BLE_H
#define IRON_V_BLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"
#include "ble_gatt.h"

/* ========================================================================= */
/* HCI Packet Indicators (Bluetooth Core Spec v5.3 Vol 4, Part A)            */
/* ========================================================================= */
#define HCI_PKT_TYPE_CMD                0x01U
#define HCI_PKT_TYPE_ACL                0x02U
#define HCI_PKT_TYPE_SCO                0x03U
#define HCI_PKT_TYPE_EVT                0x04U
#define HCI_PKT_TYPE_ISO                0x05U

/* ========================================================================= */
/* HCI Opcode Group Fields (OGF) & Opcode Command Fields (OCF)               */
/* ========================================================================= */
#define HCI_OGF_LINK_CONTROL            0x01U
#define HCI_OGF_LINK_POLICY             0x02U
#define HCI_OGF_CTRL_BB                 0x03U
#define HCI_OGF_INFO_PARAM              0x04U
#define HCI_OGF_STATUS_PARAM            0x05U
#define HCI_OGF_LE_CTRL                 0x08U
#define HCI_OGF_VENDOR_SPECIFIC         0x3FU

#define HCI_OPCODE(ogf, ocf)            ((uint16_t)(((uint16_t)(ogf) << 10U) | ((uint16_t)(ocf) & 0x03FFU)))
#define HCI_OGF(opcode)                 ((uint8_t)(((uint16_t)(opcode) >> 10U) & 0x3FU))
#define HCI_OCF(opcode)                 ((uint16_t)((uint16_t)(opcode) & 0x03FFU))

/* Controller & Baseband Commands (OGF 0x03) */
#define HCI_OCF_RESET                   0x0003U
#define HCI_OPCODE_RESET                HCI_OPCODE(HCI_OGF_CTRL_BB, HCI_OCF_RESET) /* 0x0C03 */

/* Informational Parameter Commands (OGF 0x04) */
#define HCI_OCF_READ_LOCAL_VERSION      0x0001U
#define HCI_OPCODE_READ_LOCAL_VERSION   HCI_OPCODE(HCI_OGF_INFO_PARAM, HCI_OCF_READ_LOCAL_VERSION) /* 0x1001 */
#define HCI_OCF_READ_BD_ADDR            0x0002U
#define HCI_OPCODE_READ_BD_ADDR         HCI_OPCODE(HCI_OGF_INFO_PARAM, HCI_OCF_READ_BD_ADDR) /* 0x1002 */

/* LE Controller Commands (OGF 0x08) */
#define HCI_OCF_LE_SET_ADV_PARAMS       0x0006U
#define HCI_OPCODE_LE_SET_ADV_PARAMS    HCI_OPCODE(HCI_OGF_LE_CTRL, HCI_OCF_LE_SET_ADV_PARAMS) /* 0x2006 */
#define HCI_OCF_LE_SET_ADV_DATA         0x0008U
#define HCI_OPCODE_LE_SET_ADV_DATA      HCI_OPCODE(HCI_OGF_LE_CTRL, HCI_OCF_LE_SET_ADV_DATA) /* 0x2008 */
#define HCI_OCF_LE_SET_ADV_ENABLE       0x000AU
#define HCI_OPCODE_LE_SET_ADV_ENABLE    HCI_OPCODE(HCI_OGF_LE_CTRL, HCI_OCF_LE_SET_ADV_ENABLE) /* 0x200A */

/* ========================================================================= */
/* HCI Event Codes (Bluetooth Core Spec v5.3 Vol 4, Part E)                  */
/* ========================================================================= */
#define HCI_EVT_DISCONNECTION_COMPLETE  0x05U
#define HCI_EVT_COMMAND_COMPLETE        0x0EU
#define HCI_EVT_COMMAND_STATUS          0x0FU
#define HCI_EVT_NUM_COMPLETED_PACKETS   0x13U
#define HCI_EVT_LE_META                 0x3EU

/* HCI Error / Status Codes */
#define HCI_STATUS_SUCCESS              0x00U
#define HCI_STATUS_UNKNOWN_HCI_CMD      0x01U
#define HCI_STATUS_UNKNOWN_CONN_ID      0x02U
#define HCI_STATUS_HARDWARE_FAILURE     0x03U
#define HCI_STATUS_MEMORY_EXCEEDED      0x07U
#define HCI_STATUS_CONN_TIMEOUT         0x08U
#define HCI_STATUS_COMMAND_DISALLOWED   0x0CU
#define HCI_STATUS_INVALID_PARAMS       0x12U

/* ========================================================================= */
/* Buffer Sizing & Timing Constants                                          */
/* ========================================================================= */
#define HCI_MAX_PACKET_LEN              256U
#define HCI_CMD_HEADER_LEN              3U   /* Opcode (2) + Param Len (1) */
#define HCI_EVT_HEADER_LEN              2U   /* Event Code (1) + Param Len (1) */
#define BLE_BD_ADDR_LEN                 6U
#define BLE_DEFAULT_TIMEOUT_MS          100U /* Max latency constraint: <= 100 ms */

/* ========================================================================= */
/* eFuse Memory-Mapped Registers for BD_ADDR Extraction (TRM Chapter 10)     */
/* ========================================================================= */
#define EFUSE_CONTROLLER_BASE            0x600B0800U
#define EFUSE_MAC_SYS_0_OFFSET           0x0044U
#define EFUSE_MAC_SYS_1_OFFSET           0x0048U

#define EFUSE_MAC_SYS_0_REG              ((volatile uint32_t *)(EFUSE_CONTROLLER_BASE + EFUSE_MAC_SYS_0_OFFSET))
#define EFUSE_MAC_SYS_1_REG              ((volatile uint32_t *)(EFUSE_CONTROLLER_BASE + EFUSE_MAC_SYS_1_OFFSET))

/* Hardware Interrupt Channel for BLE Timer */
#define BLE_TIMER_INTR_CHANNEL           9U
#define BLE_TIMER_INTR_PRIORITY          8U

/* Maximum Queue Capacity for HCI Virtual Transport (Zero Dynamic Heap)     */
#define BLE_HCI_QUEUE_CAPACITY           8U

/* Advertising Constants */
#define BLE_ADV_FLAGS_LEN                0x02U
#define BLE_ADV_FLAGS_TYPE               0x01U
#define BLE_ADV_FLAGS_VAL                0x06U /* General Discoverable + BR/EDR Not Supported */

#define BLE_ADV_NAME_TYPE                0x09U /* Complete Local Name */
#define BLE_ADV_UUID16_TYPE              0x03U /* Complete 16-bit Service UUID */
#define BLE_CUSTOM_SERVICE_UUID          0xFFE0U

#define BLE_ADV_TYPE_IND                0x00U /* Connectable undirected advertising */
#define BLE_ADV_FILTER_ALLOW_ALL        0x00U
#define BLE_ADV_INT_MIN_DEFAULT         CONFIG_BLE_ADV_INTERVAL_MIN
#define BLE_ADV_INT_MAX_DEFAULT         CONFIG_BLE_ADV_INTERVAL_MAX
#define BLE_ADV_CHANNEL_MAP_DEFAULT     CONFIG_BLE_ADV_CHANNEL_MAP

/* ========================================================================= */
/* Concrete Data Structures (docs/development-roadmap.md:642-664)            */
/* ========================================================================= */
typedef enum {
    BLE_STATE_STANDBY = 0,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCONNECTING
} ble_gap_state_t;

typedef struct {
    uint8_t flags[3];              /* 0x02, 0x01, 0x06 (General Discoverable) */
    uint8_t complete_name_hdr[2];  /* Len, 0x09 (Complete Local Name) */
    char name[12];                 /* "IRON-V-C6\0" */
    uint8_t service_uuid_hdr[2];   /* Len, 0x03 (Complete 16-bit Service UUID) */
    uint16_t service_uuid;         /* 0xFFE0 */
} __attribute__((packed)) ble_adv_packet_t;

typedef struct {
    ble_gap_state_t state;
    uint8_t bd_addr[BLE_BD_ADDR_LEN];
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t cmd_complete_count;
    uint32_t adv_start_count;
    uint32_t adv_stop_count;
} ble_telemetry_t;

/* ========================================================================= */
/* Driver Return Codes                                                       */
/* ========================================================================= */
typedef enum {
    BLE_OK = 0,
    BLE_ERR_INVALID_ARG = -1,
    BLE_ERR_TIMEOUT = -2,
    BLE_ERR_QUEUE_FULL = -3,
    BLE_ERR_QUEUE_EMPTY = -4,
    BLE_ERR_NOT_INITIALIZED = -5,
    BLE_ERR_COMMAND_FAILED = -6
} ble_status_t;

/* ========================================================================= */
/* Public BLE Driver APIs                                                    */
/* ========================================================================= */

/* Core lifecycle initialization */
ble_status_t ble_init(void);

/* Controller shim & VHCI packet transport */
ble_status_t ble_hci_send_cmd(const uint8_t *packet, uint16_t len);
ble_status_t ble_hci_recv_event(uint8_t *out_packet, uint16_t max_len, uint32_t timeout_ms);
bool ble_hci_has_event(void);

/* Helper to execute a synchronous command and wait for Command Complete event */
ble_status_t ble_hci_execute_cmd(const uint8_t *cmd_pkt, uint16_t cmd_len,
                                 uint8_t *out_evt, uint16_t max_evt_len,
                                 uint32_t timeout_ms);

/* GAP Advertising Controls */
ble_status_t ble_gap_start_advertising(void);
ble_status_t ble_gap_stop_advertising(void);
ble_gap_state_t ble_gap_get_state(void);

/* Device Identity & Telemetry */
ble_status_t ble_get_bd_addr(uint8_t *out_addr);
ble_status_t ble_get_telemetry(ble_telemetry_t *out_telemetry);

#endif /* IRON_V_BLE_H */
