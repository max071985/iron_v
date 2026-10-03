/*
 * src/dhcp.c
 *
 * Lightweight Zero-Allocation Freestanding DHCP Server & DNS Captive Portal
 * RFC 2131 (Dynamic Host Configuration Protocol), RFC 1035 (DNS Protocol)
 *
 * Implements deterministic static lease pooling, DHCPDISCOVER/DHCPREQUEST handling,
 * DHCPOFFER/DHCPACK synthesis, and DNS catch-all redirection to local gateway.
 */

#include "dhcp.h"
#include "net.h"
#include "wifi.h"
#include "string.h"
#include "config.h"
#include "mdns.h"

#if defined(__riscv)
#include "console.h"
#include "utils.h"
#endif

/* ========================================================================= */
/* Static Storage: Pre-Allocated Lease Table & Telemetry Tracking             */
/* Zero dynamic heap memory calls permitted (AGENTS.md execution standard)   */
/* ========================================================================= */
static dhcp_lease_t s_dhcp_leases[DHCP_MAX_LEASES];
static dhcp_telemetry_t s_dhcp_telemetry;
static bool s_dhcp_initialized = false;

/* ========================================================================= */
/* Outbound Frame Transmission Helper                                        */
/* ========================================================================= */
static dhcp_status_t dhcp_send_udp_frame(const uint8_t *dest_mac, uint32_t dest_ip,
                                         uint16_t src_port, uint16_t dest_port,
                                         const void *payload, uint16_t payload_len)
{
    if (dest_mac == NULL || payload == NULL || payload_len == 0U)
    {
        return DHCP_ERR_INVALID_ARG;
    }

    uint16_t total_len = (uint16_t)(ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN + payload_len);
    if (total_len > NET_MAX_FRAME_SIZE)
    {
        return DHCP_ERR_INVALID_ARG;
    }

    uint8_t frame_buf[NET_MAX_FRAME_SIZE];
    ethernet_header_t *eth = (ethernet_header_t *)frame_buf;
    ipv4_header_t *ip = (ipv4_header_t *)(frame_buf + ETH_HDR_LEN);
    udp_header_t *udp = (udp_header_t *)(frame_buf + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    uint8_t *data_dst = frame_buf + ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN;

    /* Query local interface MAC */
    uint8_t src_mac[ETH_ADDR_LEN];
    net_config_t ncfg;
    net_get_config(&ncfg);
    memcpy(src_mac, ncfg.mac, ETH_ADDR_LEN);

    /* 1. Ethernet Header: If destination IP is broadcast (255.255.255.255), Ethernet destination MUST be broadcast */
    static const uint8_t s_bcast_mac[ETH_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (dest_ip == 0xFFFFFFFFU)
    {
        memcpy(eth->dest_mac, s_bcast_mac, ETH_ADDR_LEN);
    }
    else
    {
        memcpy(eth->dest_mac, dest_mac, ETH_ADDR_LEN);
    }
    memcpy(eth->src_mac, src_mac, ETH_ADDR_LEN);
    eth->ethertype = NET_HTONS(ETHERTYPE_IPV4);

    /* 2. IPv4 Header */
    ip->ver_ihl           = IPV4_VER_IHL_DEFAULT;
    ip->tos               = IPV4_TOS_DEFAULT;
    ip->total_len         = NET_HTONS(IPV4_MIN_HDR_LEN + UDP_HDR_LEN + payload_len);
    ip->identification    = 0x4321U;
    ip->flags_frag_offset = NET_HTONS(IPV4_FLAGS_DF);
    ip->ttl               = IPV4_TTL_DEFAULT;
    ip->protocol          = IPV4_PROTO_UDP;
    ip->src_ip            = NET_HTONL(DHCP_DEFAULT_GATEWAY);
    ip->dest_ip           = NET_HTONL(dest_ip);
    ip->checksum          = 0U;
    ip->checksum          = NET_HTONS(net_ipv4_checksum(ip));

    /* 3. UDP Header: Zero checksum for IPv4 UDP per RFC 768 / RFC 2131 */
    udp->src_port  = NET_HTONS(src_port);
    udp->dest_port = NET_HTONS(dest_port);
    udp->length    = NET_HTONS(UDP_HDR_LEN + payload_len);
    udp->checksum  = 0U;

    /* 4. Payload */
    memcpy(data_dst, payload, payload_len);

    /* Transmit frame via Wi-Fi subsystem */
    wifi_status_t wst = wifi_tx_packet(frame_buf, total_len);
    return (wst == WIFI_OK) ? DHCP_OK : DHCP_ERR_TX_FAILED;
}

/* ========================================================================= */
/* Subsystem Lifecycle Initialization                                        */
/* ========================================================================= */
dhcp_status_t dhcp_init(void)
{
    for (uint32_t i = 0U; i < DHCP_MAX_LEASES; i++)
    {
        memset(s_dhcp_leases[i].mac, 0, sizeof(s_dhcp_leases[i].mac));
        s_dhcp_leases[i].ip = DHCP_DEFAULT_BASE_IP + i;
        s_dhcp_leases[i].lease_start_sec = 0U;
        s_dhcp_leases[i].active = false;
    }

    memset(&s_dhcp_telemetry, 0, sizeof(s_dhcp_telemetry));
    s_dhcp_initialized = true;
    return DHCP_OK;
}

/* ========================================================================= */
/* Static Lease Management                                                   */
/* ========================================================================= */
static dhcp_lease_t *dhcp_find_lease_by_mac(const uint8_t *mac)
{
    for (uint32_t i = 0U; i < DHCP_MAX_LEASES; i++)
    {
        if (s_dhcp_leases[i].active && memcmp(s_dhcp_leases[i].mac, mac, 6) == 0)
        {
            return &s_dhcp_leases[i];
        }
    }
    return NULL;
}

static dhcp_lease_t *dhcp_allocate_lease(const uint8_t *mac)
{
    /* 1. Return existing match if previously allocated */
    dhcp_lease_t *existing = dhcp_find_lease_by_mac(mac);
    if (existing != NULL)
    {
        return existing;
    }

    /* 2. Find first inactive slot */
    for (uint32_t i = 0U; i < DHCP_MAX_LEASES; i++)
    {
        if (!s_dhcp_leases[i].active)
        {
            memcpy(s_dhcp_leases[i].mac, mac, 6);
            s_dhcp_leases[i].active = true;
            s_dhcp_leases[i].lease_start_sec = 0U;
            s_dhcp_telemetry.active_leases++;
            return &s_dhcp_leases[i];
        }
    }

    /* 3. If full, deterministically recycle oldest slot */
    memcpy(s_dhcp_leases[0].mac, mac, 6);
    s_dhcp_leases[0].active = true;
    s_dhcp_leases[0].lease_start_sec = 0U;
    return &s_dhcp_leases[0];
}

void dhcp_release_lease(const uint8_t *mac)
{
    if (mac == NULL) return;
    for (uint32_t i = 0U; i < DHCP_MAX_LEASES; i++)
    {
        if (s_dhcp_leases[i].active && memcmp(s_dhcp_leases[i].mac, mac, 6) == 0)
        {
            s_dhcp_leases[i].active = false;
            memset(s_dhcp_leases[i].mac, 0, 6);
            if (s_dhcp_telemetry.active_leases > 0U)
            {
                s_dhcp_telemetry.active_leases--;
            }
            s_dhcp_telemetry.release_rx++;
            break;
        }
    }
}

const dhcp_lease_t *dhcp_get_lease(uint32_t index)
{
    if (index >= DHCP_MAX_LEASES)
    {
        return NULL;
    }
    return &s_dhcp_leases[index];
}

/* ========================================================================= */
/* Inbound DHCP Packet Dispatch & Response Engine (RFC 2131)                  */
/* ========================================================================= */
dhcp_status_t dhcp_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len)
{
    (void)eth_frame;
    if (!s_dhcp_initialized)
    {
        dhcp_init();
    }

    if (payload == NULL || len < 240U)
    {
        return DHCP_ERR_INVALID_ARG;
    }

    const dhcp_packet_t *req = (const dhcp_packet_t *)payload;
    if (req->op != DHCP_OP_BOOTREQUEST ||
        req->htype != DHCP_HTYPE_ETHERNET ||
        req->hlen != DHCP_HLEN_ETHERNET ||
        NET_NTOHL(req->magic_cookie) != DHCP_MAGIC_COOKIE)
    {
        return DHCP_ERR_CORRUPT_FRAME;
    }

    /* Parse DHCP Options */
    uint8_t msg_type = 0U;
    uint32_t requested_ip = 0U;
    uint32_t server_id = 0U;

    const uint8_t *opts = req->options;
    uint16_t max_opts_len = (len > 240U) ? (len - 240U) : 0U;
    if (max_opts_len > sizeof(req->options))
    {
        max_opts_len = (uint16_t)sizeof(req->options);
    }

    uint16_t idx = 0U;
    while (idx < max_opts_len)
    {
        uint8_t opt = opts[idx++];
        if (opt == DHCP_OPT_PAD)
        {
            continue;
        }
        if (opt == DHCP_OPT_END)
        {
            break;
        }
        if (idx >= max_opts_len)
        {
            break;
        }
        uint8_t opt_len = opts[idx++];
        if ((idx + opt_len) > max_opts_len)
        {
            break;
        }

        if (opt == DHCP_OPT_MSG_TYPE && opt_len == 1U)
        {
            msg_type = opts[idx];
        }
        else if (opt == DHCP_OPT_REQUESTED_IP && opt_len == 4U)
        {
            requested_ip = ((uint32_t)opts[idx] << 24U) |
                           ((uint32_t)opts[idx + 1U] << 16U) |
                           ((uint32_t)opts[idx + 2U] << 8U) |
                           (uint32_t)opts[idx + 3U];
        }
        else if (opt == DHCP_OPT_SERVER_ID && opt_len == 4U)
        {
            server_id = ((uint32_t)opts[idx] << 24U) |
                        ((uint32_t)opts[idx + 1U] << 16U) |
                        ((uint32_t)opts[idx + 2U] << 8U) |
                        (uint32_t)opts[idx + 3U];
        }
        idx = (uint16_t)(idx + opt_len);
    }

    /* Build DHCP Response Packet */
    dhcp_packet_t resp;
    memset(&resp, 0, sizeof(resp));

    resp.op           = DHCP_OP_BOOTREPLY;
    resp.htype        = DHCP_HTYPE_ETHERNET;
    resp.hlen         = DHCP_HLEN_ETHERNET;
    resp.hops         = 0U;
    resp.xid          = req->xid;
    resp.secs         = 0U;
    resp.flags        = req->flags;
    resp.ciaddr       = 0U;
    resp.siaddr       = 0U;
    resp.giaddr       = 0U;
    memcpy(resp.chaddr, req->chaddr, DHCP_CHADDR_LEN);
    resp.magic_cookie = NET_HTONL(DHCP_MAGIC_COOKIE);

    uint8_t *out_opt = resp.options;
    uint16_t opt_offset = 0U;

    if (msg_type == DHCP_MSG_DISCOVER)
    {
        s_dhcp_telemetry.discover_rx++;

        dhcp_lease_t *lease = dhcp_allocate_lease(req->chaddr);
        arp_insert(lease->ip, req->chaddr);
        resp.yiaddr = NET_HTONL(lease->ip);

        /* Option 53: Message Type = DHCPOFFER (2) */
        out_opt[opt_offset++] = DHCP_OPT_MSG_TYPE;
        out_opt[opt_offset++] = 1U;
        out_opt[opt_offset++] = DHCP_MSG_OFFER;

        /* Option 54: Server Identifier */
        out_opt[opt_offset++] = DHCP_OPT_SERVER_ID;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY & 0xFFU);

        /* Option 51: IP Lease Time (86400 seconds) */
        out_opt[opt_offset++] = DHCP_OPT_LEASE_TIME;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_LEASE_TIME_SEC >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_LEASE_TIME_SEC >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_LEASE_TIME_SEC >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_LEASE_TIME_SEC & 0xFFU);

        /* Option 1: Subnet Mask (255.255.255.0) */
        out_opt[opt_offset++] = DHCP_OPT_SUBNET_MASK;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_NETMASK >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_NETMASK >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_NETMASK >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_NETMASK & 0xFFU);

        /* Option 3: Router / Default Gateway (192.168.1.1) */
        out_opt[opt_offset++] = DHCP_OPT_ROUTER;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY & 0xFFU);

        /* Option 6: Domain Name Server (192.168.1.1) */
        out_opt[opt_offset++] = DHCP_OPT_DNS;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY & 0xFFU);

        /* Option 28: Broadcast Address (192.168.1.255) */
        out_opt[opt_offset++] = 28U;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
        out_opt[opt_offset++] = 255U;

        /* Option 255: End */
        out_opt[opt_offset++] = DHCP_OPT_END;

        uint16_t resp_len = (uint16_t)(240U + opt_offset);
        if (resp_len < 300U)
        {
            memset(&resp.options[opt_offset], 0, 300U - resp_len);
            resp_len = 300U;
        }
        dhcp_status_t st = dhcp_send_udp_frame(req->chaddr, 0xFFFFFFFFU,
                                               DHCP_SERVER_PORT, DHCP_CLIENT_PORT,
                                               &resp, resp_len);
        if (st == DHCP_OK)
        {
            s_dhcp_telemetry.offer_tx++;
#if defined(__riscv)
            console_puts("[DHCP] Sent DHCPOFFER (192.168.1.");
            put_dec(lease->ip & 0xFFU);
            console_puts(") to client\r\n");
#endif
        }
        return st;
    }
    else if (msg_type == DHCP_MSG_REQUEST)
    {
        s_dhcp_telemetry.request_rx++;

        /* Verify server identifier if present */
        if (server_id != 0U && server_id != DHCP_DEFAULT_GATEWAY)
        {
            return DHCP_OK; /* Targeted at a different DHCP server */
        }

        dhcp_lease_t *lease = dhcp_allocate_lease(req->chaddr);
        arp_insert(lease->ip, req->chaddr);
        if (requested_ip != 0U && requested_ip != lease->ip)
        {
            /* Requested IP does not match assigned pool; issue NAK */
            resp.yiaddr = 0U;
            out_opt[opt_offset++] = DHCP_OPT_MSG_TYPE;
            out_opt[opt_offset++] = 1U;
            out_opt[opt_offset++] = DHCP_MSG_NAK;

            out_opt[opt_offset++] = DHCP_OPT_SERVER_ID;
            out_opt[opt_offset++] = 4U;
            out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
            out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
            out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
            out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY & 0xFFU);

            out_opt[opt_offset++] = DHCP_OPT_END;

            uint16_t resp_len = (uint16_t)(240U + opt_offset);
            if (resp_len < 300U)
            {
                memset(&resp.options[opt_offset], 0, 300U - resp_len);
                resp_len = 300U;
            }
            dhcp_send_udp_frame(req->chaddr, 0xFFFFFFFFU,
                                DHCP_SERVER_PORT, DHCP_CLIENT_PORT,
                                &resp, resp_len);
            s_dhcp_telemetry.nak_tx++;
            return DHCP_OK;
        }

        resp.yiaddr = NET_HTONL(lease->ip);

        /* Option 53: Message Type = DHCPACK (5) */
        out_opt[opt_offset++] = DHCP_OPT_MSG_TYPE;
        out_opt[opt_offset++] = 1U;
        out_opt[opt_offset++] = DHCP_MSG_ACK;

        /* Option 54: Server Identifier */
        out_opt[opt_offset++] = DHCP_OPT_SERVER_ID;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY & 0xFFU);

        /* Option 51: IP Lease Time */
        out_opt[opt_offset++] = DHCP_OPT_LEASE_TIME;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_LEASE_TIME_SEC >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_LEASE_TIME_SEC >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_LEASE_TIME_SEC >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_LEASE_TIME_SEC & 0xFFU);

        /* Option 1: Subnet Mask */
        out_opt[opt_offset++] = DHCP_OPT_SUBNET_MASK;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_NETMASK >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_NETMASK >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_NETMASK >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_NETMASK & 0xFFU);

        /* Option 3: Router */
        out_opt[opt_offset++] = DHCP_OPT_ROUTER;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY & 0xFFU);

        /* Option 6: DNS Server */
        out_opt[opt_offset++] = DHCP_OPT_DNS;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY & 0xFFU);

        /* Option 28: Broadcast Address (192.168.1.255) */
        out_opt[opt_offset++] = 28U;
        out_opt[opt_offset++] = 4U;
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
        out_opt[opt_offset++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
        out_opt[opt_offset++] = 255U;

        /* Option 255: End */
        out_opt[opt_offset++] = DHCP_OPT_END;

        uint16_t resp_len = (uint16_t)(240U + opt_offset);
        if (resp_len < 300U)
        {
            memset(&resp.options[opt_offset], 0, 300U - resp_len);
            resp_len = 300U;
        }
        dhcp_status_t st = dhcp_send_udp_frame(req->chaddr, 0xFFFFFFFFU,
                                               DHCP_SERVER_PORT, DHCP_CLIENT_PORT,
                                               &resp, resp_len);
        if (st == DHCP_OK)
        {
            s_dhcp_telemetry.ack_tx++;
#if defined(__riscv)
            console_puts("[DHCP] Assigned lease 192.168.1.");
            put_dec(lease->ip & 0xFFU);
            console_puts(" (DHCPACK sent)\r\n");
#endif
        }
        return st;
    }
    else if (msg_type == DHCP_MSG_RELEASE)
    {
        dhcp_release_lease(req->chaddr);
        return DHCP_OK;
    }

    return DHCP_OK;
}

/* ========================================================================= */
/* Minimal DNS Captive Portal Catch-All Resolver (RFC 1035)                  */
/* Resolves all A-record queries to AP Gateway IP (192.168.1.1)               */
/* ========================================================================= */
DHCP_FLASH_TEXT
dhcp_status_t dns_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len)
{
    if (eth_frame == NULL || payload == NULL || len < sizeof(dns_header_t))
    {
        return DHCP_ERR_INVALID_ARG;
    }

    const dns_header_t *dns_req = (const dns_header_t *)payload;
    uint16_t qdcount = NET_NTOHS(dns_req->qdcount);
    if (qdcount == 0U)
    {
        return DHCP_ERR_CORRUPT_FRAME;
    }

    s_dhcp_telemetry.dns_queries_rx++;

    /* Find end of Question section: walk domain name labels */
    uint16_t q_offset = sizeof(dns_header_t);
    while (q_offset < len)
    {
        uint8_t label_len = payload[q_offset++];
        if (label_len == 0U)
        {
            break; /* Zero-length root label marks end of name */
        }
        if ((q_offset + label_len) > len)
        {
            return DHCP_ERR_CORRUPT_FRAME;
        }
        q_offset = (uint16_t)(q_offset + label_len);
    }

    /* Skip QTYPE (2) and QCLASS (2) */
    if ((q_offset + 4U) > len)
    {
        return DHCP_ERR_CORRUPT_FRAME;
    }
    uint16_t q_end = (uint16_t)(q_offset + 4U);
    uint16_t q_total_len = (uint16_t)(q_end - sizeof(dns_header_t));

    /* Assemble DNS Response */
    uint8_t resp_buf[512];
    dns_header_t *resp_hdr = (dns_header_t *)resp_buf;
    resp_hdr->id      = dns_req->id;
    resp_hdr->flags   = NET_HTONS(DNS_FLAGS_RESPONSE_OK);
    resp_hdr->qdcount = NET_HTONS(1U);
    resp_hdr->ancount = NET_HTONS(1U);
    resp_hdr->nscount = 0U;
    resp_hdr->arcount = 0U;

    uint16_t out_len = sizeof(dns_header_t);

    /* Copy question section */
    memcpy(&resp_buf[out_len], &payload[sizeof(dns_header_t)], q_total_len);
    out_len = (uint16_t)(out_len + q_total_len);

    /* Append Answer: Compression pointer to question domain (0xC00C) */
    resp_buf[out_len++] = 0xC0U;
    resp_buf[out_len++] = 0x0CU;

    /* Type: A (Host Address) */
    resp_buf[out_len++] = (uint8_t)(DNS_TYPE_A >> 8U);
    resp_buf[out_len++] = (uint8_t)(DNS_TYPE_A & 0xFFU);

    /* Class: IN (Internet) */
    resp_buf[out_len++] = (uint8_t)(DNS_CLASS_IN >> 8U);
    resp_buf[out_len++] = (uint8_t)(DNS_CLASS_IN & 0xFFU);

    /* TTL: 60 seconds */
    resp_buf[out_len++] = (uint8_t)(DNS_DEFAULT_TTL_SEC >> 24U);
    resp_buf[out_len++] = (uint8_t)(DNS_DEFAULT_TTL_SEC >> 16U);
    resp_buf[out_len++] = (uint8_t)(DNS_DEFAULT_TTL_SEC >> 8U);
    resp_buf[out_len++] = (uint8_t)(DNS_DEFAULT_TTL_SEC & 0xFFU);

    /* Data Length: 4 bytes (IPv4) */
    resp_buf[out_len++] = 0x00U;
    resp_buf[out_len++] = 0x04U;

    /* IP Address: 192.168.1.1 */
    resp_buf[out_len++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
    resp_buf[out_len++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
    resp_buf[out_len++] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
    resp_buf[out_len++] = (uint8_t)(DHCP_DEFAULT_GATEWAY & 0xFFU);

    /* Extract client MAC, client IP and client UDP port from inbound frame */
    const ethernet_header_t *in_eth = (const ethernet_header_t *)eth_frame;
    const ipv4_header_t *in_ip = (const ipv4_header_t *)(eth_frame + ETH_HDR_LEN);
    uint8_t ihl = (in_ip->ver_ihl & 0x0FU) * 4U;
    const udp_header_t *in_udp = (const udp_header_t *)(eth_frame + ETH_HDR_LEN + ihl);

    uint32_t client_ip = NET_NTOHL(in_ip->src_ip);
    uint16_t client_port = NET_NTOHS(in_udp->src_port);

    dhcp_status_t st = dhcp_send_udp_frame(in_eth->src_mac, client_ip,
                                           DNS_SERVER_PORT, client_port,
                                           resp_buf, out_len);
    if (st == DHCP_OK)
    {
        s_dhcp_telemetry.dns_replies_tx++;
    }
    return st;
}

/* ========================================================================= */
/* Telemetry Accessor                                                        */
/* ========================================================================= */
dhcp_status_t dhcp_get_telemetry(dhcp_telemetry_t *out_telemetry)
{
    if (out_telemetry == NULL)
    {
        return DHCP_ERR_INVALID_ARG;
    }
    *out_telemetry = s_dhcp_telemetry;
    return DHCP_OK;
}

/* ========================================================================= */
/* DHCP Client Subsystem (Task 8.2)                                          */
/* ========================================================================= */
static dhcp_client_telemetry_t s_dhcp_client_telem;
static bool s_dhcp_client_initialized = false;

DHCP_FLASH_TEXT
dhcp_status_t dhcp_client_init(void)
{
    memset(&s_dhcp_client_telem, 0, sizeof(s_dhcp_client_telem));
    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_IDLE;
    s_dhcp_client_telem.xid = 0x5A4F0001U;
    s_dhcp_client_initialized = true;
    return DHCP_OK;
}

DHCP_FLASH_TEXT
dhcp_status_t dhcp_client_start(void)
{
    if (!s_dhcp_client_initialized)
    {
        dhcp_client_init();
    }

    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_DISCOVERING;
    s_dhcp_client_telem.xid++;

    /* Format DHCPDISCOVER packet */
    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    pkt.op = DHCP_OP_BOOTREQUEST;
    pkt.htype = DHCP_HTYPE_ETHERNET;
    pkt.hlen = DHCP_HLEN_ETHERNET;
    pkt.hops = DHCP_HOPS_DEFAULT;
    pkt.xid = NET_HTONL(s_dhcp_client_telem.xid);
    pkt.flags = NET_HTONS(0x8000U); /* Broadcast */

    net_config_t ncfg;
    net_get_config(&ncfg);
    memcpy(pkt.chaddr, ncfg.mac, ETH_ADDR_LEN);
    pkt.magic_cookie = NET_HTONL(DHCP_MAGIC_COOKIE);

    /* Options */
    size_t opt_idx = 0U;
    pkt.options[opt_idx++] = DHCP_OPT_MSG_TYPE;
    pkt.options[opt_idx++] = 1U;
    pkt.options[opt_idx++] = DHCP_MSG_DISCOVER;

    pkt.options[opt_idx++] = DHCP_OPT_PARAM_REQUEST_LIST;
    pkt.options[opt_idx++] = 3U;
    pkt.options[opt_idx++] = DHCP_OPT_SUBNET_MASK;
    pkt.options[opt_idx++] = DHCP_OPT_ROUTER;
    pkt.options[opt_idx++] = DHCP_OPT_DNS;

    pkt.options[opt_idx++] = DHCP_OPT_END;

    uint16_t pkt_len = (uint16_t)(sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN + opt_idx);
    uint8_t broadcast_mac[ETH_ADDR_LEN] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};

    dhcp_status_t st = dhcp_send_udp_frame(broadcast_mac, 0xFFFFFFFFU,
                                           DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                                           &pkt, pkt_len);
    if (st == DHCP_OK)
    {
        s_dhcp_client_telem.discovers_sent++;
    }
    return st;
}

DHCP_FLASH_TEXT
dhcp_status_t dhcp_client_stop(void)
{
    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_IDLE;
    return DHCP_OK;
}

DHCP_FLASH_TEXT
dhcp_client_state_t dhcp_client_get_state(void)
{
    return s_dhcp_client_telem.state;
}

DHCP_FLASH_TEXT
dhcp_status_t dhcp_client_get_telemetry(dhcp_client_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return DHCP_ERR_INVALID_ARG;
    }
    *out_telem = s_dhcp_client_telem;
    return DHCP_OK;
}

DHCP_FLASH_TEXT
void dhcp_client_set_static_fallback(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns)
{
    s_dhcp_client_telem.assigned_ip = ip;
    s_dhcp_client_telem.netmask = netmask;
    s_dhcp_client_telem.gateway = gateway;
    s_dhcp_client_telem.dns_server = dns;
    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_STATIC;
    net_set_ip(ip, netmask, gateway);
    mdns_announce();
}

DHCP_FLASH_TEXT
dhcp_status_t dhcp_client_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len)
{
    (void)eth_frame;
    if (payload == NULL || len < (sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN))
    {
        return DHCP_ERR_CORRUPT_FRAME;
    }

    const dhcp_packet_t *pkt = (const dhcp_packet_t *)payload;
    if (pkt->op != DHCP_OP_BOOTREPLY || NET_NTOHL(pkt->magic_cookie) != DHCP_MAGIC_COOKIE)
    {
        return DHCP_ERR_CORRUPT_FRAME;
    }

    if (NET_NTOHL(pkt->xid) != s_dhcp_client_telem.xid)
    {
        return DHCP_ERR_INVALID_ARG;
    }

    /* Parse options */
    uint8_t msg_type = 0U;
    uint32_t server_id = 0U;
    uint32_t subnet_mask = 0U;
    uint32_t router = 0U;
    uint32_t dns = 0U;
    uint32_t lease_time = 0U;

    size_t opt_offset = 0U;
    size_t max_opts = len - (sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN);
    while (opt_offset < max_opts)
    {
        uint8_t opt = pkt->options[opt_offset++];
        if (opt == DHCP_OPT_END) break;
        if (opt == DHCP_OPT_PAD) continue;
        if (opt_offset >= max_opts) break;
        uint8_t opt_len = pkt->options[opt_offset++];
        if (opt_offset + opt_len > max_opts) break;

        if (opt == DHCP_OPT_MSG_TYPE && opt_len == 1U)
        {
            msg_type = pkt->options[opt_offset];
        }
        else if (opt == DHCP_OPT_SERVER_ID && opt_len == 4U)
        {
            memcpy(&server_id, &pkt->options[opt_offset], 4U);
            server_id = NET_NTOHL(server_id);
        }
        else if (opt == DHCP_OPT_SUBNET_MASK && opt_len == 4U)
        {
            memcpy(&subnet_mask, &pkt->options[opt_offset], 4U);
            subnet_mask = NET_NTOHL(subnet_mask);
        }
        else if (opt == DHCP_OPT_ROUTER && opt_len >= 4U)
        {
            memcpy(&router, &pkt->options[opt_offset], 4U);
            router = NET_NTOHL(router);
        }
        else if (opt == DHCP_OPT_DNS && opt_len >= 4U)
        {
            memcpy(&dns, &pkt->options[opt_offset], 4U);
            dns = NET_NTOHL(dns);
        }
        else if (opt == DHCP_OPT_LEASE_TIME && opt_len == 4U)
        {
            memcpy(&lease_time, &pkt->options[opt_offset], 4U);
            lease_time = NET_NTOHL(lease_time);
        }
        opt_offset += opt_len;
    }

    if (msg_type == DHCP_MSG_OFFER && s_dhcp_client_telem.state == DHCP_CLIENT_STATE_DISCOVERING)
    {
        s_dhcp_client_telem.offers_received++;
        uint32_t offered_ip = NET_NTOHL(pkt->yiaddr);
        s_dhcp_client_telem.assigned_ip = offered_ip;
        s_dhcp_client_telem.server_ip = server_id;
        s_dhcp_client_telem.netmask = subnet_mask;
        s_dhcp_client_telem.gateway = router;
        s_dhcp_client_telem.dns_server = dns;
        s_dhcp_client_telem.lease_time_sec = lease_time;

        /* Synthesize DHCPREQUEST */
        dhcp_packet_t req;
        memset(&req, 0, sizeof(req));
        req.op = DHCP_OP_BOOTREQUEST;
        req.htype = DHCP_HTYPE_ETHERNET;
        req.hlen = DHCP_HLEN_ETHERNET;
        req.xid = NET_HTONL(s_dhcp_client_telem.xid);
        req.flags = NET_HTONS(0x8000U);

        net_config_t ncfg;
        net_get_config(&ncfg);
        memcpy(req.chaddr, ncfg.mac, ETH_ADDR_LEN);
        req.magic_cookie = NET_HTONL(DHCP_MAGIC_COOKIE);

        size_t idx = 0U;
        req.options[idx++] = DHCP_OPT_MSG_TYPE;
        req.options[idx++] = 1U;
        req.options[idx++] = DHCP_MSG_REQUEST;

        req.options[idx++] = DHCP_OPT_REQUESTED_IP;
        req.options[idx++] = 4U;
        uint32_t req_ip = NET_HTONL(offered_ip);
        memcpy(&req.options[idx], &req_ip, 4U);
        idx += 4U;

        if (server_id != 0U)
        {
            req.options[idx++] = DHCP_OPT_SERVER_ID;
            req.options[idx++] = 4U;
            uint32_t srv_be = NET_HTONL(server_id);
            memcpy(&req.options[idx], &srv_be, 4U);
            idx += 4U;
        }

        req.options[idx++] = DHCP_OPT_END;

        uint16_t req_len = (uint16_t)(sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN + idx);
        uint8_t bcast_mac[ETH_ADDR_LEN] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
        dhcp_send_udp_frame(bcast_mac, 0xFFFFFFFFU, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &req, req_len);

        s_dhcp_client_telem.requests_sent++;
        s_dhcp_client_telem.state = DHCP_CLIENT_STATE_REQUESTING;
        return DHCP_OK;
    }
    else if (msg_type == DHCP_MSG_ACK && s_dhcp_client_telem.state == DHCP_CLIENT_STATE_REQUESTING)
    {
        s_dhcp_client_telem.acks_received++;
        s_dhcp_client_telem.assigned_ip = NET_NTOHL(pkt->yiaddr);
        if (subnet_mask != 0U) s_dhcp_client_telem.netmask = subnet_mask;
        if (router != 0U) s_dhcp_client_telem.gateway = router;
        if (dns != 0U) s_dhcp_client_telem.dns_server = dns;
        if (lease_time != 0U) s_dhcp_client_telem.lease_time_sec = lease_time;

        s_dhcp_client_telem.state = DHCP_CLIENT_STATE_BOUND;

        /* Apply IP configuration to network stack */
        net_set_ip(s_dhcp_client_telem.assigned_ip, s_dhcp_client_telem.netmask, s_dhcp_client_telem.gateway);

        /* Trigger mDNS announcement */
        mdns_announce();
        return DHCP_OK;
    }

    return DHCP_OK;
}

