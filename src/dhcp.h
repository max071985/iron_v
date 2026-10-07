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

/* Client retransmission (RFC 2131 4.1): 4 s doubling to 64 s, each wait +/- 1 s */
#define DHCP_CLIENT_BACKOFF_INITIAL_US  4000000ULL
#define DHCP_CLIENT_BACKOFF_MAX_US      64000000ULL
#define DHCP_CLIENT_BACKOFF_JITTER_US   1000000ULL
/* REQUEST after an OFFER, and the INIT-REBOOT REQUEST: sends before going back to INIT (DISCOVER) */
#define DHCP_CLIENT_REQUEST_MAX_TRIES   3U
/* RENEWING/REBINDING retransmit at half the time left to T2/expiry, never sooner than this (RFC 2131 4.4.5) */
#define DHCP_CLIENT_RENEW_MIN_RETRY_US  60000000ULL
/* T1 = 1/2 and T2 = 7/8 of the lease when the server sends no options 58/59 */
#define DHCP_CLIENT_T1_NUM              1U
#define DHCP_CLIENT_T1_DEN              2U
#define DHCP_CLIENT_T2_NUM              7U
#define DHCP_CLIENT_T2_DEN              8U
#define DHCP_CLIENT_FALLBACK_LEASE_SEC  3600U        /* ACK without a lease time */
#define DHCP_LEASE_INFINITE             0xFFFFFFFFU  /* RFC 2132 9.2 */
#define DHCP_US_PER_SEC                 1000000ULL
#define DHCP_CLIENT_TIME_NEVER          UINT64_MAX   /* T1/T2/expiry of an infinite lease */
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
/* BOOTP flags: client asks for broadcast replies (RFC 2131 section 2) */
#define DHCP_FLAG_BROADCAST             0x8000U
#define DHCP_IP_BROADCAST               0xFFFFFFFFU

#define DHCP_OPT_DNS                    6U
#define DHCP_OPT_BROADCAST_ADDR         28U
#define DHCP_OPT_REQUESTED_IP           50U
#define DHCP_OPT_LEASE_TIME             51U
#define DHCP_OPT_MSG_TYPE               53U
#define DHCP_OPT_SERVER_ID              54U
#define DHCP_OPT_PARAM_REQUEST_LIST     55U
#define DHCP_OPT_RENEWAL_TIME           58U    /* T1 */
#define DHCP_OPT_REBINDING_TIME         59U    /* T2 */
#define DHCP_OPT_END                    255U
#define DHCP_OPT_HDR_LEN                2U     /* code + length */
#define DHCP_OPT_U8_LEN                 1U
#define DHCP_OPT_U32_LEN                4U
#define DHCP_OPT_IPV4_LEN               4U

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
#define DNS_FLAGS_RESPONSE_NXDOMAIN     0x8183U
#define DNS_LOCAL_SUFFIX                ".local"
#define DNS_MAX_RESPONSE_LEN            512U   /* RFC 1035 UDP message limit */
#define DNS_A_RECORD_LEN                16U    /* name ptr 2 + type 2 + class 2 + TTL 4 + len 2 + IPv4 4 */
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
/* DHCP Client Data Structures & Telemetry                                   */
/* ========================================================================= */
/* RFC 2131 figure 5 states; INIT is DISCOVERING, SELECTING ends with the first OFFER */
typedef enum {
    DHCP_CLIENT_STATE_IDLE = 0,     /* link down or never started; a lease may still be held */
    DHCP_CLIENT_STATE_DISCOVERING,
    DHCP_CLIENT_STATE_REQUESTING,
    DHCP_CLIENT_STATE_BOUND,
    DHCP_CLIENT_STATE_STATIC,
    DHCP_CLIENT_STATE_REBOOTING,    /* INIT-REBOOT: asking for the previous address after a reconnect */
    DHCP_CLIENT_STATE_RENEWING,     /* after T1: unicast REQUEST to the leasing server */
    DHCP_CLIENT_STATE_REBINDING     /* after T2: broadcast REQUEST to any server */
} dhcp_client_state_t;

typedef struct {
    dhcp_client_state_t state;
    uint32_t assigned_ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
    uint32_t server_ip;
    uint32_t lease_time_sec;
    uint32_t xid;
    uint32_t discovers_sent;
    uint32_t offers_received;
    uint32_t requests_sent;
    uint32_t acks_received;
    /* Lease timing (seconds from the lease start; DHCP_LEASE_INFINITE: never) */
    uint32_t t1_sec;
    uint32_t t2_sec;
    uint32_t lease_left_sec;        /* 0: no lease held */
    uint32_t next_tx_sec;           /* seconds until the next scheduled send (0: none or due) */
    uint32_t naks_received;
    uint32_t retransmits;           /* DISCOVER/REQUEST sends after the first of an exchange */
    uint32_t reboots_confirmed;     /* INIT-REBOOT got the previous address back */
    uint32_t renewals;              /* lease extended in RENEWING or REBINDING */
    uint32_t leases_expired;
} dhcp_client_telemetry_t;

/* ========================================================================= */
/* Public API Declarations                                                   */
/* ========================================================================= */
dhcp_status_t dhcp_init(void);
dhcp_status_t dhcp_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len);
dhcp_status_t dns_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len);
dhcp_status_t dhcp_get_telemetry(dhcp_telemetry_t *out_telemetry);
void dhcp_release_lease(const uint8_t *mac);
const dhcp_lease_t *dhcp_get_lease(uint32_t index);

/* DHCP Client APIs */
dhcp_status_t dhcp_client_init(void);
dhcp_status_t dhcp_client_start(void);
/* Link down: stop sending, keep the lease for INIT-REBOOT on the next start */
dhcp_status_t dhcp_client_stop(void);
/* Different network: drop the held lease so the next start uses DISCOVER */
void dhcp_client_forget(void);
const char *dhcp_client_state_name(dhcp_client_state_t state);
dhcp_status_t dhcp_client_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len);
dhcp_client_state_t dhcp_client_get_state(void);
dhcp_status_t dhcp_client_get_telemetry(dhcp_client_telemetry_t *out_telem);
void dhcp_client_set_static_fallback(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns);
void dhcp_client_tick(void);

#endif /* IRON_V_DHCP_H */

