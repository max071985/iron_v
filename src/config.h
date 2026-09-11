/*
 * src/config.h
 *
 * Iron V System & Subsystem Configuration Manifest
 *
 * Centralizes all user-configurable parameters for networking, wireless PHY,
 * device identification, system telemetry intervals, and console defaults.
 *
 * Execution Standards (AGENTS.md):
 * - Safe abstraction: Hardware invariants (clock trees, PLL dividers, memory cartography,
 *   PMP boundaries) are strictly protected in their architectural driver blocks and
 *   MUST NOT be exposed here to prevent operational disruption.
 * - Non-zero symbolic definitions: All items use strongly-typed uppercase macros
 *   with #ifndef guards, permitting compile-time overrides via CFLAGS.
 */

#ifndef IRON_V_CONFIG_H
#define IRON_V_CONFIG_H

#include <stdint.h>

/* ========================================================================= */
/* 1. Device Identification & System Metadata                                */
/* ========================================================================= */

#ifndef CONFIG_DEVICE_HOSTNAME
#define CONFIG_DEVICE_HOSTNAME              "iron-v"
#endif

#ifndef CONFIG_DEVICE_MANUFACTURER
#define CONFIG_DEVICE_MANUFACTURER          "Iron-V RISC-V Team"
#endif

#ifndef CONFIG_DEVICE_MODEL_NUMBER
#define CONFIG_DEVICE_MODEL_NUMBER          "ESP32-C6-BareMetal"
#endif

#ifndef CONFIG_FIRMWARE_REVISION
#define CONFIG_FIRMWARE_REVISION            "1.0.0"
#endif

#ifndef CONFIG_HARDWARE_REVISION
#define CONFIG_HARDWARE_REVISION            "ESP32-C6-WROOM-1"
#endif

/* ========================================================================= */
/* 2. IPv4 Network Subsystem Defaults (RFC 791 / RFC 826)                    */
/* ========================================================================= */

#ifndef CONFIG_NET_IP_OCTET_1
#define CONFIG_NET_IP_OCTET_1               10U
#endif
#ifndef CONFIG_NET_IP_OCTET_2
#define CONFIG_NET_IP_OCTET_2               0U
#endif
#ifndef CONFIG_NET_IP_OCTET_3
#define CONFIG_NET_IP_OCTET_3               0U
#endif
#ifndef CONFIG_NET_IP_OCTET_4
#define CONFIG_NET_IP_OCTET_4               100U
#endif

#ifndef CONFIG_NET_NETMASK_OCTET_1
#define CONFIG_NET_NETMASK_OCTET_1          255U
#endif
#ifndef CONFIG_NET_NETMASK_OCTET_2
#define CONFIG_NET_NETMASK_OCTET_2          255U
#endif
#ifndef CONFIG_NET_NETMASK_OCTET_3
#define CONFIG_NET_NETMASK_OCTET_3          255U
#endif
#ifndef CONFIG_NET_NETMASK_OCTET_4
#define CONFIG_NET_NETMASK_OCTET_4          0U
#endif

#ifndef CONFIG_NET_GATEWAY_OCTET_1
#define CONFIG_NET_GATEWAY_OCTET_1          10U
#endif
#ifndef CONFIG_NET_GATEWAY_OCTET_2
#define CONFIG_NET_GATEWAY_OCTET_2          0U
#endif
#ifndef CONFIG_NET_GATEWAY_OCTET_3
#define CONFIG_NET_GATEWAY_OCTET_3          0U
#endif
#ifndef CONFIG_NET_GATEWAY_OCTET_4
#define CONFIG_NET_GATEWAY_OCTET_4          138U
#endif

#ifndef CONFIG_NET_DNS_OCTET_1
#define CONFIG_NET_DNS_OCTET_1              10U
#endif
#ifndef CONFIG_NET_DNS_OCTET_2
#define CONFIG_NET_DNS_OCTET_2              0U
#endif
#ifndef CONFIG_NET_DNS_OCTET_3
#define CONFIG_NET_DNS_OCTET_3              0U
#endif
#ifndef CONFIG_NET_DNS_OCTET_4
#define CONFIG_NET_DNS_OCTET_4              138U
#endif

/* Convenience IPv4 Address Packing Macro */
#define CONFIG_IP4_ADDR(a, b, c, d) \
    ((uint32_t)((((uint32_t)(a) & 0xFFU) << 24U) | \
                (((uint32_t)(b) & 0xFFU) << 16U) | \
                (((uint32_t)(c) & 0xFFU) << 8U)  | \
                ((uint32_t)(d) & 0xFFU)))

#ifndef CONFIG_NET_DEFAULT_IP
#define CONFIG_NET_DEFAULT_IP \
    CONFIG_IP4_ADDR(CONFIG_NET_IP_OCTET_1, CONFIG_NET_IP_OCTET_2, CONFIG_NET_IP_OCTET_3, CONFIG_NET_IP_OCTET_4)
#endif

#ifndef CONFIG_NET_DEFAULT_NETMASK
#define CONFIG_NET_DEFAULT_NETMASK \
    CONFIG_IP4_ADDR(CONFIG_NET_NETMASK_OCTET_1, CONFIG_NET_NETMASK_OCTET_2, CONFIG_NET_NETMASK_OCTET_3, CONFIG_NET_NETMASK_OCTET_4)
#endif

#ifndef CONFIG_NET_DEFAULT_GATEWAY
#define CONFIG_NET_DEFAULT_GATEWAY \
    CONFIG_IP4_ADDR(CONFIG_NET_GATEWAY_OCTET_1, CONFIG_NET_GATEWAY_OCTET_2, CONFIG_NET_GATEWAY_OCTET_3, CONFIG_NET_GATEWAY_OCTET_4)
#endif

#ifndef CONFIG_NET_DEFAULT_DNS
#define CONFIG_NET_DEFAULT_DNS \
    CONFIG_IP4_ADDR(CONFIG_NET_DNS_OCTET_1, CONFIG_NET_DNS_OCTET_2, CONFIG_NET_DNS_OCTET_3, CONFIG_NET_DNS_OCTET_4)
#endif

#ifndef CONFIG_NET_DEFAULT_TTL
#define CONFIG_NET_DEFAULT_TTL              64U
#endif

/* ========================================================================= */
/* 3. TCP Protocol Engine Defaults (RFC 793)                                 */
/* ========================================================================= */

#ifndef CONFIG_TCP_DEFAULT_WINDOW
#define CONFIG_TCP_DEFAULT_WINDOW           1024U
#endif

#ifndef CONFIG_TCP_DEFAULT_MSS
#define CONFIG_TCP_DEFAULT_MSS              1460U
#endif

#ifndef CONFIG_TCP_RETRANSMIT_TIMEOUT_MS
#define CONFIG_TCP_RETRANSMIT_TIMEOUT_MS    1000U
#endif

#ifndef CONFIG_TCP_MAX_RETRIES
#define CONFIG_TCP_MAX_RETRIES              3U
#endif

#ifndef CONFIG_TCP_DEFAULT_HTTP_PORT
#define CONFIG_TCP_DEFAULT_HTTP_PORT        80U
#endif

/* ========================================================================= */
/* 4. Wi-Fi 6 (802.11ax) MAC & Station Defaults                              */
/* ========================================================================= */

#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID                    "IronV-AP"
#endif

#ifndef CONFIG_WIFI_PASSPHRASE
#define CONFIG_WIFI_PASSPHRASE              "ironv-c6-pass"
#endif

#ifndef CONFIG_WIFI_CHANNEL
#define CONFIG_WIFI_CHANNEL                 1U
#endif

#ifndef CONFIG_WIFI_COUNTRY_CODE
#define CONFIG_WIFI_COUNTRY_CODE            "US"
#endif

#ifndef CONFIG_WIFI_MAX_TX_POWER_DBM
#define CONFIG_WIFI_MAX_TX_POWER_DBM        20U
#endif

#ifndef CONFIG_WIFI_USE_STATIC_IP
#define CONFIG_WIFI_USE_STATIC_IP           1U
#endif

/* ========================================================================= */
/* 5. Bluetooth 5 (LE) Controller & GATT Server Defaults                     */
/* ========================================================================= */

#ifndef CONFIG_BLE_DEVICE_NAME
#define CONFIG_BLE_DEVICE_NAME              "IRON-V-C6"
#endif

/* Advertising Interval in 0.625 ms units (160 * 0.625 = 100 ms) */
#ifndef CONFIG_BLE_ADV_INTERVAL_MIN
#define CONFIG_BLE_ADV_INTERVAL_MIN         0x00A0U
#endif

#ifndef CONFIG_BLE_ADV_INTERVAL_MAX
#define CONFIG_BLE_ADV_INTERVAL_MAX         0x0140U
#endif

/* Advertising Channel Map: bit 0 = Ch 37, bit 1 = Ch 38, bit 2 = Ch 39 (0x07 = all) */
#ifndef CONFIG_BLE_ADV_CHANNEL_MAP
#define CONFIG_BLE_ADV_CHANNEL_MAP          0x07U
#endif

/* ========================================================================= */
/* 6. IEEE 802.15.4 (Zigbee / Thread) Transceiver Defaults                   */
/* ========================================================================= */

#ifndef CONFIG_IEEE802154_DEFAULT_CHANNEL
#define CONFIG_IEEE802154_DEFAULT_CHANNEL   15U
#endif

#ifndef CONFIG_IEEE802154_DEFAULT_PAN_ID
#define CONFIG_IEEE802154_DEFAULT_PAN_ID    0x1A2BU
#endif

#ifndef CONFIG_IEEE802154_DEFAULT_SHORT_ADDR
#define CONFIG_IEEE802154_DEFAULT_SHORT_ADDR 0x1234U
#endif

#ifndef CONFIG_IEEE802154_DEFAULT_TX_POWER
#define CONFIG_IEEE802154_DEFAULT_TX_POWER  0x0FU
#endif

#ifndef CONFIG_IEEE802154_AUTO_ACK_TX
#define CONFIG_IEEE802154_AUTO_ACK_TX       1U
#endif

#ifndef CONFIG_IEEE802154_AUTO_ACK_RX
#define CONFIG_IEEE802154_AUTO_ACK_RX       1U
#endif

/* ========================================================================= */
/* 7. System Heartbeat & Interactive Shell Defaults                          */
/* ========================================================================= */

#ifndef CONFIG_SYSTEM_HEARTBEAT_INTERVAL_SEC
#define CONFIG_SYSTEM_HEARTBEAT_INTERVAL_SEC 10U
#endif

#ifndef CONFIG_CONSOLE_PROMPT
#define CONFIG_CONSOLE_PROMPT               "iron_v> "
#endif

#ifndef CONFIG_CONSOLE_DEFAULT_ECHO
#define CONFIG_CONSOLE_DEFAULT_ECHO         1U
#endif

#endif /* IRON_V_CONFIG_H */
