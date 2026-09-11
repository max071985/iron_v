/*
 * src/net.h
 *
 * Bare-Metal Zero-Copy IPv4, ARP, ICMP, UDP & Lightweight TCP Protocol Stack
 * RFC 791 (IPv4), RFC 792 (ICMP), RFC 793 (TCP), RFC 826 (ARP), RFC 1071 (Checksum)
 *
 * Defines packed network protocol headers, big-endian network byte order macros,
 * deterministic static ARP cache, internet checksum engines, and public stack APIs.
 */

#ifndef IRON_V_NET_H
#define IRON_V_NET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"

/* ========================================================================= */
/* Endianness Conversion Macros (Big-Endian Network Byte Order <-> Host)     */
/* ========================================================================= */
#define NET_HTONS(x)   ((uint16_t)((((uint16_t)(x) & 0x00FFU) << 8U) | \
                                   (((uint16_t)(x) & 0xFF00U) >> 8U)))
#define NET_NTOHS(x)   NET_HTONS(x)

#define NET_HTONL(x)   ((uint32_t)((((uint32_t)(x) & 0x000000FFU) << 24U) | \
                                   (((uint32_t)(x) & 0x0000FF00U) << 8U)  | \
                                   (((uint32_t)(x) & 0x00FF0000U) >> 8U)  | \
                                   (((uint32_t)(x) & 0xFF000000U) >> 24U)))
#define NET_NTOHL(x)   NET_HTONL(x)

#define NET_IP4_ADDR(a, b, c, d) \
    ((uint32_t)((((uint32_t)(a) & 0xFFU) << 24U) | \
                (((uint32_t)(b) & 0xFFU) << 16U) | \
                (((uint32_t)(c) & 0xFFU) << 8U)  | \
                ((uint32_t)(d) & 0xFFU)))

/* ========================================================================= */
/* Protocol Constants & Length Definitions                                   */
/* ========================================================================= */
#define ETH_ADDR_LEN                     6U
#define IPV4_ADDR_LEN                    4U

#define ETH_HDR_LEN                      14U
#define ARP_HDR_LEN                      28U
#define IPV4_MIN_HDR_LEN                 20U
#define ICMP_MIN_HDR_LEN                 8U
#define UDP_HDR_LEN                      8U
#define TCP_MIN_HDR_LEN                  20U

/* Standard Ethernet Frame Types */
#define ETHERTYPE_IPV4                   0x0800U
#define ETHERTYPE_ARP                    0x0806U

/* ARP Protocol Constants (RFC 826) */
#define ARP_HW_TYPE_ETHERNET             0x0001U
#define ARP_PROTO_IPV4                   0x0800U
#define ARP_OPCODE_REQUEST               0x0001U
#define ARP_OPCODE_REPLY                 0x0002U

/* IPv4 Header Bitfields & Protocols (RFC 791) */
#define IPV4_VERSION_4                   0x04U
#define IPV4_IHL_MIN_WORDS               0x05U
#define IPV4_VER_IHL_DEFAULT             ((IPV4_VERSION_4 << 4U) | IPV4_IHL_MIN_WORDS)
#define IPV4_TOS_DEFAULT                 0x00U
#define IPV4_FLAGS_DF                    0x4000U
#define IPV4_TTL_DEFAULT                 CONFIG_NET_DEFAULT_TTL

#define IPV4_PROTO_ICMP                  1U
#define IPV4_PROTO_TCP                   6U
#define IPV4_PROTO_UDP                   17U

/* ICMP Protocol Constants (RFC 792) */
#define ICMP_TYPE_ECHO_REPLY             0U
#define ICMP_TYPE_ECHO_REQUEST           8U
#define ICMP_CODE_ECHO                   0U

/* TCP Flag Bitmasks (RFC 793) */
#define TCP_FLAG_FIN                     (1U << 0)
#define TCP_FLAG_SYN                     (1U << 1)
#define TCP_FLAG_RST                     (1U << 2)
#define TCP_FLAG_PSH                     (1U << 3)
#define TCP_FLAG_ACK                     (1U << 4)
#define TCP_FLAG_URG                     (1U << 5)

#define TCP_DATA_OFFSET_SHIFT            4U
#define TCP_DEFAULT_WINDOW_SIZE          2048U
#define TCP_DEFAULT_MSS                  1460U

/* Static Sizing & Capacity Limits (Zero Heap Allocation) */
#define ARP_TABLE_CAPACITY               8U
#define NET_MAX_FRAME_SIZE               1536U
#define NET_IP_STR_BUF_LEN               16U

/* Default Network Interface IP Configuration (sourced from centralized config.h) */
#define NET_DEFAULT_IP                   CONFIG_NET_DEFAULT_IP
#define NET_DEFAULT_NETMASK              CONFIG_NET_DEFAULT_NETMASK
#define NET_DEFAULT_GATEWAY              CONFIG_NET_DEFAULT_GATEWAY

/* RFC 1071 Standard Checksum Test Vector Constants */
#define RFC1071_TEST_HDR_LEN             20U
#define RFC1071_TEST_EXPECTED_CHECKSUM   0xB1E6U

/* ========================================================================= */
/* Status & Error Enumerations                                               */
/* ========================================================================= */
typedef enum {
    NET_OK = 0,
    NET_ERR_INVALID_ARG = -1,
    NET_ERR_BUFFER_TOO_SMALL = -2,
    NET_ERR_CHECKSUM = -3,
    NET_ERR_NOT_FOUND = -4,
    NET_ERR_QUEUE_FULL = -5,
    NET_ERR_UNKNOWN_PROTO = -6,
    NET_ERR_FRAME_CORRUPT = -7
} net_status_t;

/* ========================================================================= */
/* Packed Wire Protocol Header Structures                                    */
/* ========================================================================= */

/**
 * @brief Standard 14-Byte Ethernet II Frame Header
 */
typedef struct {
    uint8_t dest_mac[ETH_ADDR_LEN];
    uint8_t src_mac[ETH_ADDR_LEN];
    uint16_t ethertype;
} __attribute__((packed)) ethernet_header_t;

/**
 * @brief Standard 28-Byte ARP Packet Structure (RFC 826)
 */
typedef struct {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t hw_size;
    uint8_t proto_size;
    uint16_t opcode;
    uint8_t sender_mac[ETH_ADDR_LEN];
    uint32_t sender_ip;
    uint8_t target_mac[ETH_ADDR_LEN];
    uint32_t target_ip;
} __attribute__((packed)) arp_header_t;

/**
 * @brief Combined 42-Byte Ethernet + ARP Frame Structure
 */
typedef struct {
    ethernet_header_t eth;
    arp_header_t arp;
} __attribute__((packed)) arp_frame_t;

/**
 * @brief Standard 20-Byte IPv4 Base Header Structure (RFC 791)
 */
typedef struct {
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_len;
    uint16_t identification;
    uint16_t flags_frag_offset;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed)) ipv4_header_t;

/**
 * @brief Standard 8-Byte ICMP Echo Header Structure (RFC 792)
 */
typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} __attribute__((packed)) icmp_header_t;

/**
 * @brief Standard 8-Byte UDP Header Structure (RFC 768)
 */
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

/**
 * @brief Standard 20-Byte TCP Base Header Structure (RFC 793)
 */
typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset_reserved; /* Data offset (4b) + Reserved (4b) */
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed)) tcp_header_t;

/**
 * @brief 12-Byte TCP/UDP IPv4 Pseudo-Header for Checksum Calculation
 */
typedef struct {
    uint32_t src_ip;
    uint32_t dest_ip;
    uint8_t zero;
    uint8_t protocol;
    uint16_t length;
} __attribute__((packed)) ipv4_pseudo_header_t;

/* ========================================================================= */
/* Static State & Telemetry Structures                                       */
/* ========================================================================= */

typedef struct {
    uint32_t ip;
    uint8_t mac[ETH_ADDR_LEN];
    bool valid;
    uint32_t updated_ticks;
} arp_entry_t;

typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint8_t mac[ETH_ADDR_LEN];
} net_config_t;

typedef struct {
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t arp_requests_rx;
    uint32_t arp_replies_tx;
    uint32_t ipv4_rx;
    uint32_t ipv4_tx;
    uint32_t icmp_rx;
    uint32_t icmp_tx;
    uint32_t udp_rx;
    uint32_t udp_tx;
    uint32_t tcp_rx;
    uint32_t tcp_tx;
    uint32_t checksum_errors;
    uint32_t dropped_packets;
} net_telemetry_t;

/* ========================================================================= */
/* Public Driver & Protocol APIs                                             */
/* ========================================================================= */

/* Core lifecycle initialization */
net_status_t net_init(void);

/* Interface Configuration & Telemetry */
net_status_t net_set_ip(uint32_t ip, uint32_t netmask, uint32_t gateway);
net_status_t net_reset_defaults(void);
net_status_t net_get_config(net_config_t *out_config);
net_status_t net_get_telemetry(net_telemetry_t *out_telemetry);

/* RFC 1071 Internet Checksum Engines */
uint16_t net_checksum(const void *data, size_t len);
uint16_t net_ipv4_checksum(const ipv4_header_t *hdr);
uint16_t net_tcp_checksum(uint32_t src_ip, uint32_t dest_ip,
                          const tcp_header_t *tcp_hdr, uint16_t tcp_len,
                          const void *payload, uint16_t payload_len);
uint16_t net_udp_checksum(uint32_t src_ip, uint32_t dest_ip,
                          const udp_header_t *udp_hdr,
                          const void *payload, uint16_t payload_len);

/* ARP Resolution & Cache Controls */
net_status_t arp_lookup(uint32_t ip, uint8_t *out_mac);
net_status_t arp_insert(uint32_t ip, const uint8_t *mac);
net_status_t arp_process_packet(const uint8_t *in_frame, uint16_t in_len,
                                uint8_t *out_reply, uint16_t max_out_len,
                                uint16_t *out_reply_len);

/* Inbound Packet Processing & Dispatches */
net_status_t net_input(const uint8_t *frame, uint16_t len);
net_status_t icmp_process_packet(const uint8_t *in_packet, uint16_t in_len,
                                 uint8_t *out_reply, uint16_t max_out_len,
                                 uint16_t *out_reply_len);

/* Outbound Transport Transmission */
net_status_t net_send_udp(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port,
                          const void *data, uint16_t len);

/* String Formatting Helpers */
void net_ip_to_str(uint32_t ip, char *buf, size_t buf_len);
uint32_t net_str_to_ip(const char *str);

#endif /* IRON_V_NET_H */
