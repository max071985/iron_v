/*
 * src/config.h
 *
 * System & Subsystem Default Configuration Manifest
 *
 * Provides centralized default settings for networking, wireless PHY,
 * device identification, and system parameters across the Iron V bare-metal runtime.
 * Values can be modified directly in this file or overridden at compile-time via CFLAGS.
 */

#ifndef IRON_V_CONFIG_H
#define IRON_V_CONFIG_H

#include <stdint.h>

/* ========================================================================= */
/* Network Subsystem Defaults (IPv4 / Subnet / Gateway / DNS)                */
/* ========================================================================= */

#ifndef CONFIG_NET_IP_OCTET_1
#define CONFIG_NET_IP_OCTET_1       10U
#endif
#ifndef CONFIG_NET_IP_OCTET_2
#define CONFIG_NET_IP_OCTET_2       0U
#endif
#ifndef CONFIG_NET_IP_OCTET_3
#define CONFIG_NET_IP_OCTET_3       0U
#endif
#ifndef CONFIG_NET_IP_OCTET_4
#define CONFIG_NET_IP_OCTET_4       100U
#endif

#ifndef CONFIG_NET_NETMASK_OCTET_1
#define CONFIG_NET_NETMASK_OCTET_1  255U
#endif
#ifndef CONFIG_NET_NETMASK_OCTET_2
#define CONFIG_NET_NETMASK_OCTET_2  255U
#endif
#ifndef CONFIG_NET_NETMASK_OCTET_3
#define CONFIG_NET_NETMASK_OCTET_3  255U
#endif
#ifndef CONFIG_NET_NETMASK_OCTET_4
#define CONFIG_NET_NETMASK_OCTET_4  0U
#endif

#ifndef CONFIG_NET_GATEWAY_OCTET_1
#define CONFIG_NET_GATEWAY_OCTET_1  10U
#endif
#ifndef CONFIG_NET_GATEWAY_OCTET_2
#define CONFIG_NET_GATEWAY_OCTET_2  0U
#endif
#ifndef CONFIG_NET_GATEWAY_OCTET_3
#define CONFIG_NET_GATEWAY_OCTET_3  0U
#endif
#ifndef CONFIG_NET_GATEWAY_OCTET_4
#define CONFIG_NET_GATEWAY_OCTET_4  138U
#endif

#ifndef CONFIG_NET_DNS_OCTET_1
#define CONFIG_NET_DNS_OCTET_1      10U
#endif
#ifndef CONFIG_NET_DNS_OCTET_2
#define CONFIG_NET_DNS_OCTET_2      0U
#endif
#ifndef CONFIG_NET_DNS_OCTET_3
#define CONFIG_NET_DNS_OCTET_3      0U
#endif
#ifndef CONFIG_NET_DNS_OCTET_4
#define CONFIG_NET_DNS_OCTET_4      138U
#endif

/* Convenience IP packing helper */
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

/* ========================================================================= */
/* Device Identification & Wireless Network Defaults                         */
/* ========================================================================= */
#ifndef CONFIG_DEVICE_HOSTNAME
#define CONFIG_DEVICE_HOSTNAME              "iron-v"
#endif

#ifndef CONFIG_BLE_DEVICE_NAME
#define CONFIG_BLE_DEVICE_NAME              "Iron-V-C6"
#endif

#ifndef CONFIG_WIFI_SSID
#define CONFIG_WIFI_SSID                    "IronV-AP"
#endif

#ifndef CONFIG_IEEE802154_DEFAULT_CHANNEL
#define CONFIG_IEEE802154_DEFAULT_CHANNEL   15U
#endif

#ifndef CONFIG_IEEE802154_DEFAULT_PAN_ID
#define CONFIG_IEEE802154_DEFAULT_PAN_ID    0x1A2BU
#endif

#ifndef CONFIG_IEEE802154_DEFAULT_SHORT_ADDR
#define CONFIG_IEEE802154_DEFAULT_SHORT_ADDR 0x1234U
#endif

#endif /* IRON_V_CONFIG_H */
