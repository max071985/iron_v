/*
 * src/dhcp.h
 *
 * Lightweight Zero-Allocation Freestanding DHCP Server & DNS Captive Portal
 * RFC 2131 (Dynamic Host Configuration Protocol), RFC 1035 (DNS Protocol)
 *
 * Implements deterministic static lease pooling, DHCPDISCOVER/DHCPREQUEST handling,
 * DHCPOFFER/DHCPACK synthesis, and DNS catch-all redirection to local gateway.
 */

#ifndef IRON_V_DHCP_H
#define IRON_V_DHCP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================= */
/* Protocol Port & Transport Constants                                       */
/* ========================================================================= */
#define DHCP_SERVER_PORT                67U
#define DHCP_CLIENT_PORT                68U
#define DNS_SERVER_PORT                 53U

/* ========================================================================= */
/* DHCP Base Protocol Constants (RFC 2131)                                   */
/* ========================================================================= */
#define DHCP_OP_BOOTREQUEST             1U
#define DHCP_OP_BOOTREPLY               2U
#define DHCP_HTYPE_ETHERNET             1U
#define DHCP_HLEN_ETHERNET              6U
#define DHCP_HOPS_DEFAULT               0U

#define DHCP_MAGIC_COOKIE               0x63825363U

#define DHCP_OPT_PAD                    0U
#define DHCP_OPT_SUBNET_MASK            1U
#define DHCP_OPT_ROUTER                 3U
#define DHCP_OPT_DNS                    6U
#define DHCP_OPT_REQUESTED_IP           50U
#define DHCP_OPT_LEASE_TIME             51U
#define DHCP_OPT_MSG_TYPE               53U
#define DHCP_OPT_SERVER_ID              54U
#define DHCP_OPT_PARAM_REQUEST_LIST     55U
#define DHCP_OPT_END                    255U

#define DHCP_MSG_DISCOVER               1U
#define DHCP_MSG_OFFER                  2U
#define DHCP_MSG_REQUEST                3U
#define DHCP_MSG_DECLINE                4U
#define DHCP_MSG_ACK                    5U
#define DHCP_MSG_NAK                    6U
#define DHCP_MSG_RELEASE                7U
#define DHCP_MSG_INFORM                 8U

/* ========================================================================= */
/* Configuration & Geometry Constants                                        */
/* ========================================================================= */
#define DHCP_MAX_LEASES                 4U
#define DHCP_DEFAULT_LEASE_TIME_SEC     86400U /* 24 hours */
#define DHCP_CHADDR_LEN                 16U
#define DHCP_SNAME_LEN                  64U
#define DHCP_FILE_LEN                   128U
#define DHCP_MIN_OPTIONS_LEN            308U

#define DHCP_DEFAULT_BASE_IP            0xC0A80102U /* 192.168.1.2 */
#define DHCP_DEFAULT_NETMASK            0xFFFFFF00U /* 255.255.255.0 */
#define DHCP_DEFAULT_GATEWAY            0xC0A80101U /* 192.168.1.1 */

/* DNS Flags: Standard Query Response, No Error */
#define DNS_FLAGS_RESPONSE_OK           0x8180U
#define DNS_TYPE_A                      0x0001U
#define DNS_CLASS_IN                    0x0001U
#define DNS_DEFAULT_TTL_SEC             60U
#define DNS_MAX_NAME_LEN                128U

/* ========================================================================= */
/* Concrete Data Structures                                                  */
/* ========================================================================= */
typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[DHCP_CHADDR_LEN];
    uint8_t  sname[DHCP_SNAME_LEN];
    uint8_t  file[DHCP_FILE_LEN];
    uint32_t magic_cookie;
    uint8_t  options[DHCP_MIN_OPTIONS_LEN];
} __attribute__((packed)) dhcp_packet_t;

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

typedef struct {
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t lease_start_sec;
    bool     active;
} dhcp_lease_t;

typedef struct {
    uint32_t discover_rx;
    uint32_t offer_tx;
    uint32_t request_rx;
    uint32_t ack_tx;
    uint32_t nak_tx;
    uint32_t release_rx;
    uint32_t dns_queries_rx;
    uint32_t dns_replies_tx;
    uint32_t active_leases;
} dhcp_telemetry_t;

typedef enum {
    DHCP_OK = 0,
    DHCP_ERR_INVALID_ARG = -1,
    DHCP_ERR_CORRUPT_FRAME = -2,
    DHCP_ERR_NO_LEASE_AVAIL = -3,
    DHCP_ERR_TX_FAILED = -4
} dhcp_status_t;

/* ========================================================================= */
/* Public API Declarations                                                   */
/* ========================================================================= */
dhcp_status_t dhcp_init(void);
dhcp_status_t dhcp_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len);
dhcp_status_t dns_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len);
dhcp_status_t dhcp_get_telemetry(dhcp_telemetry_t *out_telemetry);
void dhcp_release_lease(const uint8_t *mac);
const dhcp_lease_t *dhcp_get_lease(uint32_t index);

#endif /* IRON_V_DHCP_H */
