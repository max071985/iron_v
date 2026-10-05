/*
 * src/mdns.h
 *
 * Bare-Metal Multicast DNS (mDNS) Responder Engine
 * RFC 6762 (Multicast DNS), RFC 6763 (DNS-Based Service Discovery)
 *
 * Implements zero-allocation freestanding mDNS resolution:
 * - Listens on UDP port 5353 (multicast group 224.0.0.251)
 * - Broadcasts and resolves "iron-v.local" IPv4 host address (Type A)
 * - Synthesizes unsolicited announcement packets on LAN join / DHCP acquisition
 * - Provides case-insensitive DNS name compression and question matching.
 */

#ifndef IRON_V_MDNS_H
#define IRON_V_MDNS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Protocol Constants & Geometry                                             */
/* ========================================================================= */
#define MDNS_PORT                           5353U
#define MDNS_MULTICAST_IPV4                 0xE00000FBU /* 224.0.0.251 */
#define MDNS_DEFAULT_HOSTNAME               "iron-v"
#define MDNS_DOMAIN                         "local"
#define MDNS_DEFAULT_TTL_SEC                120U
#define MDNS_LEGACY_UNICAST_TTL_SEC         10U   /* RFC 6762 6.7: one-shot queriers cache briefly */
#define MDNS_QCLASS_MASK                    0x7FFFU /* top bit: unicast-response requested (QU) */
#define MDNS_MAX_HOSTNAME_LEN               32U
#define MDNS_MAX_PACKET_LEN                 512U

/* Multicast MAC Address (RFC 1112: 01:00:5E:00:00:FB for 224.0.0.251) */
#define MDNS_MULTICAST_MAC_0                0x01U
#define MDNS_MULTICAST_MAC_1                0x00U
#define MDNS_MULTICAST_MAC_2                0x5EU
#define MDNS_MULTICAST_MAC_3                0x00U
#define MDNS_MULTICAST_MAC_4                0x00U
#define MDNS_MULTICAST_MAC_5                0xFBU

/* DNS Record Types */
#define MDNS_TYPE_A                         0x0001U
#define MDNS_TYPE_AAAA                      0x001CU
#define MDNS_TYPE_PTR                       0x000CU
#define MDNS_TYPE_TXT                       0x0010U
#define MDNS_TYPE_SRV                       0x0021U
#define MDNS_TYPE_ANY                       0x00FFU

/* DNS Classes & Flags */
#define MDNS_CLASS_IN                       0x0001U
#define MDNS_CLASS_CACHE_FLUSH              0x8001U
#define MDNS_FLAGS_RESPONSE_AA              0x8400U /* Response + Authoritative Answer */

/* ========================================================================= */
/* Status & Telemetry Structures                                             */
/* ========================================================================= */
typedef enum {
    MDNS_OK                         =  0,
    MDNS_ERR_INVALID_ARG            = -1,
    MDNS_ERR_NOT_QUERY              = -2,
    MDNS_ERR_NO_MATCH               = -3,
    MDNS_ERR_TX_FAIL                = -4,
    MDNS_ERR_BUFFER_SMALL           = -5,
    MDNS_ERR_CORRUPT_FRAME          = -6
} mdns_status_t;

typedef struct {
    bool     active;
    char     hostname[MDNS_MAX_HOSTNAME_LEN + 1U];
    uint32_t advertised_ip;
    uint32_t queries_received;
    uint32_t responses_sent;
    uint32_t announcements_sent;
    uint32_t host_queries_matched;
    uint32_t invalid_packets;
} mdns_telemetry_t;

/* ========================================================================= */
/* Public API Declarations                                                   */
/* ========================================================================= */

/* Subsystem Lifecycle */
mdns_status_t mdns_init(void);
mdns_status_t mdns_start(const char *hostname);
mdns_status_t mdns_stop(void);
bool          mdns_is_active(void);
const char   *mdns_get_hostname(void);
mdns_status_t mdns_set_hostname(const char *hostname);

/* Network Processing & Advertising */
mdns_status_t mdns_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len);
/* Query from src_ip:src_port. A source port other than 5353 is a one-shot (legacy) querier,
 * e.g. a phone's system resolver: it gets a unicast reply with its ID and question (RFC 6762 6.7) */
mdns_status_t mdns_process_query(const uint8_t *payload, uint16_t len, uint32_t src_ip, uint16_t src_port);
mdns_status_t mdns_announce(void);
mdns_status_t mdns_get_telemetry(mdns_telemetry_t *out_telem);

/* Diagnostic Visualizer (Flash XIP) */
void mdns_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* IRON_V_MDNS_H */
