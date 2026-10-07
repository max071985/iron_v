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
#include "systimer.h"
#include "hw_rng.h"

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
static dhcp_status_t dhcp_send_udp_frame_from(wifi_tx_if_t ifx, const uint8_t *dest_mac, uint32_t dest_ip,
                                              uint32_t src_ip, uint16_t src_port, uint16_t dest_port,
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
    ip->identification    = net_ip_next_id();
    ip->flags_frag_offset = NET_HTONS(IPV4_FLAGS_DF);
    ip->ttl               = IPV4_TTL_DEFAULT;
    ip->protocol          = IPV4_PROTO_UDP;
    ip->src_ip            = NET_HTONL(src_ip);
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
    wifi_status_t wst = wifi_tx_packet(ifx, frame_buf, total_len);
    return (wst == WIFI_OK) ? DHCP_OK : DHCP_ERR_TX_FAILED;
}

/* Server replies come from our address (the SoftAP gateway until one is set) */
static dhcp_status_t dhcp_send_udp_frame(wifi_tx_if_t ifx, const uint8_t *dest_mac, uint32_t dest_ip,
                                         uint16_t src_port, uint16_t dest_port,
                                         const void *payload, uint16_t payload_len)
{
    net_config_t ncfg;
    net_get_config(&ncfg);
    uint32_t src_ip = (ncfg.ip != 0U) ? ncfg.ip : DHCP_DEFAULT_GATEWAY;
    return dhcp_send_udp_frame_from(ifx, dest_mac, dest_ip, src_ip, src_port, dest_port, payload, payload_len);
}

/*
 * Server reply destination per RFC 2131 section 4.1: OFFER/ACK go unicast to the
 * client's hardware address and yiaddr unless it set the broadcast flag. Unicast
 * 802.11 frames are acknowledged and retried; AP broadcasts are not and can be
 * missed by a phone that just associated.
 */
static uint32_t dhcp_reply_dest_ip(const dhcp_packet_t *req, uint32_t yiaddr)
{
    if ((NET_NTOHS(req->flags) & DHCP_FLAG_BROADCAST) != 0U)
    {
        return DHCP_IP_BROADCAST;
    }
    return yiaddr;
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
        out_opt[opt_offset++] = DHCP_OPT_BROADCAST_ADDR;
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
        dhcp_status_t st = dhcp_send_udp_frame(WIFI_TX_IF_AP, req->chaddr, dhcp_reply_dest_ip(req, lease->ip),
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
            dhcp_send_udp_frame(WIFI_TX_IF_AP, req->chaddr, 0xFFFFFFFFU,
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
        out_opt[opt_offset++] = DHCP_OPT_BROADCAST_ADDR;
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
        dhcp_status_t st = dhcp_send_udp_frame(WIFI_TX_IF_AP, req->chaddr, dhcp_reply_dest_ip(req, lease->ip),
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
/* Minimal SoftAP DNS Resolver (RFC 1035)                                    */
/* Captive mode: every query resolves to the AP gateway (192.168.1.1).       */
/* Otherwise only A queries for the board's own names resolve; the rest get  */
/* NXDOMAIN so the phone sees a network without internet.                    */
/* ========================================================================= */
#if !CONFIG_SOFTAP_CAPTIVE_PORTAL
/* True when the question name is CONFIG_DEVICE_HOSTNAME or <hostname>.local (case-insensitive) */
static bool dns_name_is_local(const uint8_t *payload, uint16_t name_start, uint16_t name_end)
{
    char name[DNS_MAX_NAME_LEN];
    uint16_t n = 0U;
    uint16_t pos = name_start;
    while (pos < name_end)
    {
        uint8_t label_len = payload[pos++];
        if (label_len == 0U) break;
        if (n != 0U)
        {
            if (n + 1U >= sizeof(name)) return false;
            name[n++] = '.';
        }
        for (uint8_t i = 0U; i < label_len; i++)
        {
            if (n + 1U >= sizeof(name)) return false;
            char c = (char)payload[pos++];
            name[n++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
    }
    name[n] = '\0';

    size_t host_len = strlen(CONFIG_DEVICE_HOSTNAME);
    if (strncmp(name, CONFIG_DEVICE_HOSTNAME, host_len) != 0) return false;
    return name[host_len] == '\0' || strcmp(&name[host_len], DNS_LOCAL_SUFFIX) == 0;
}
#endif

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

#if CONFIG_SOFTAP_CAPTIVE_PORTAL
    bool answer = true;
#else
    uint16_t qtype = (uint16_t)(((uint16_t)payload[q_offset] << 8U) | payload[q_offset + 1U]);
    bool answer = (qtype == DNS_TYPE_A) &&
                  dns_name_is_local(payload, (uint16_t)sizeof(dns_header_t), q_offset);
#endif

    /* Assemble DNS Response: header + echoed question + one A record */
    uint8_t resp_buf[DNS_MAX_RESPONSE_LEN];
    if ((sizeof(dns_header_t) + q_total_len + DNS_A_RECORD_LEN) > sizeof(resp_buf))
    {
        return DHCP_ERR_CORRUPT_FRAME;
    }
    dns_header_t *resp_hdr = (dns_header_t *)resp_buf;
    resp_hdr->id      = dns_req->id;
    resp_hdr->flags   = NET_HTONS(answer ? DNS_FLAGS_RESPONSE_OK : DNS_FLAGS_RESPONSE_NXDOMAIN);
    resp_hdr->qdcount = NET_HTONS(1U);
    resp_hdr->ancount = NET_HTONS(answer ? 1U : 0U);
    resp_hdr->nscount = 0U;
    resp_hdr->arcount = 0U;

    uint16_t out_len = sizeof(dns_header_t);

    /* Copy question section */
    memcpy(&resp_buf[out_len], &payload[sizeof(dns_header_t)], q_total_len);
    out_len = (uint16_t)(out_len + q_total_len);

    if (answer)
    {
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
    }

    /* Extract client MAC, client IP and client UDP port from inbound frame */
    const ethernet_header_t *in_eth = (const ethernet_header_t *)eth_frame;
    const ipv4_header_t *in_ip = (const ipv4_header_t *)(eth_frame + ETH_HDR_LEN);
    uint8_t ihl = (in_ip->ver_ihl & 0x0FU) * 4U;
    const udp_header_t *in_udp = (const udp_header_t *)(eth_frame + ETH_HDR_LEN + ihl);

    uint32_t client_ip = NET_NTOHL(in_ip->src_ip);
    uint16_t client_port = NET_NTOHS(in_udp->src_port);

    dhcp_status_t st = dhcp_send_udp_frame(WIFI_TX_IF_AP, in_eth->src_mac, client_ip,
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
/* DHCP Client (RFC 2131 4.4: INIT, INIT-REBOOT, RENEWING, REBINDING)        */
/* ========================================================================= */
static dhcp_client_telemetry_t s_dhcp_client_telem;
static bool s_dhcp_client_initialized = false;

/* Lease held across link loss (RAM only, O-20); times are systimer microseconds */
static bool     s_lease_valid = false;
static uint64_t s_lease_t1_us = 0ULL;
static uint64_t s_lease_t2_us = 0ULL;
static uint64_t s_lease_end_us = 0ULL;
static uint8_t  s_server_mac[ETH_ADDR_LEN];   /* Ethernet source of the last ACK: RENEWING unicasts here */

/* Current exchange */
static uint64_t s_exchange_start_us = 0ULL;  /* first REQUEST of the exchange: the lease counts from here */
static uint64_t s_next_tx_us = 0ULL;
static uint64_t s_request_backoff_us = DHCP_CLIENT_BACKOFF_INITIAL_US;
static uint64_t s_discover_backoff_us = DHCP_CLIENT_BACKOFF_INITIAL_US;  /* reset only on BOUND and start */
static uint32_t s_tries = 0U;                /* sends in the current exchange */

static const uint8_t s_dhcp_client_bcast_mac[ETH_ADDR_LEN] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};

/* Options the client asks for in every DISCOVER/REQUEST (RFC 2131 4.4.1: same list each time) */
static const uint8_t s_dhcp_client_prl[] = {
    DHCP_OPT_SUBNET_MASK, DHCP_OPT_ROUTER, DHCP_OPT_DNS,
    DHCP_OPT_LEASE_TIME, DHCP_OPT_RENEWAL_TIME, DHCP_OPT_REBINDING_TIME
};

const char *dhcp_client_state_name(dhcp_client_state_t state)
{
    switch (state)
    {
        case DHCP_CLIENT_STATE_IDLE:        return "IDLE";
        case DHCP_CLIENT_STATE_DISCOVERING: return "DISCOVERING";
        case DHCP_CLIENT_STATE_REQUESTING:  return "REQUESTING";
        case DHCP_CLIENT_STATE_BOUND:       return "BOUND";
        case DHCP_CLIENT_STATE_STATIC:      return "STATIC";
        case DHCP_CLIENT_STATE_REBOOTING:   return "INIT-REBOOT";
        case DHCP_CLIENT_STATE_RENEWING:    return "RENEWING";
        case DHCP_CLIENT_STATE_REBINDING:   return "REBINDING";
        default:                            return "UNKNOWN";
    }
}

#if defined(__riscv)
static void dhcp_client_log_ip(const char *label, uint32_t ip)
{
    char s[NET_IP_STR_BUF_LEN];
    net_ip_to_str(ip, s, sizeof(s));
    console_puts(label);
    console_puts(s);
}
#endif

/* A fresh random transaction ID for each exchange (RFC 2131 4.1), never the previous one */
static void dhcp_client_new_xid(void)
{
    uint32_t xid = hw_rng_u32();
    if (xid == s_dhcp_client_telem.xid)
    {
        xid++;
    }
    s_dhcp_client_telem.xid = xid;
}

/* The wait before a retransmission: delay +/- DHCP_CLIENT_BACKOFF_JITTER_US, uniform (RFC 2131 4.1) */
static uint64_t dhcp_client_jittered(uint64_t delay_us)
{
    uint32_t rand32 = hw_rng_u32();
    uint64_t span = (2ULL * DHCP_CLIENT_BACKOFF_JITTER_US) + 1ULL;
    return delay_us - DHCP_CLIENT_BACKOFF_JITTER_US + ((uint64_t)rand32 % span);
}

static uint64_t dhcp_client_next_backoff(uint64_t delay_us)
{
    uint64_t next = delay_us * 2ULL;
    return (next > DHCP_CLIENT_BACKOFF_MAX_US) ? DHCP_CLIENT_BACKOFF_MAX_US : next;
}

/* RENEWING/REBINDING: half the time left to the deadline, at least DHCP_CLIENT_RENEW_MIN_RETRY_US */
static uint64_t dhcp_client_renew_retry_at(uint64_t now, uint64_t deadline_us)
{
    uint64_t half = (deadline_us > now) ? ((deadline_us - now) / 2ULL) : 0ULL;
    if (half < DHCP_CLIENT_RENEW_MIN_RETRY_US)
    {
        half = DHCP_CLIENT_RENEW_MIN_RETRY_US;
    }
    return now + half;
}

static size_t dhcp_client_put_u32_opt(uint8_t *opts, size_t idx, uint8_t code, uint32_t value)
{
    opts[idx++] = code;
    opts[idx++] = DHCP_OPT_U32_LEN;
    uint32_t be = NET_HTONL(value);
    memcpy(&opts[idx], &be, DHCP_OPT_U32_LEN);
    return idx + DHCP_OPT_U32_LEN;
}

/*
 * Builds and sends the message the current state calls for (RFC 2131 table 5):
 *   DISCOVERING  DISCOVER  broadcast, ciaddr 0
 *   REQUESTING   REQUEST   broadcast, ciaddr 0, requested IP + server id
 *   REBOOTING    REQUEST   broadcast, ciaddr 0, requested IP, no server id
 *   RENEWING     REQUEST   unicast to the leasing server, ciaddr = our address
 *   REBINDING    REQUEST   broadcast, ciaddr = our address
 */
static dhcp_status_t dhcp_client_send(void)
{
    dhcp_client_state_t state = s_dhcp_client_telem.state;
    bool discover = (state == DHCP_CLIENT_STATE_DISCOVERING);
    bool have_ip = (state == DHCP_CLIENT_STATE_RENEWING || state == DHCP_CLIENT_STATE_REBINDING);
    uint32_t our_ip = s_dhcp_client_telem.assigned_ip;

    dhcp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.op = DHCP_OP_BOOTREQUEST;
    pkt.htype = DHCP_HTYPE_ETHERNET;
    pkt.hlen = DHCP_HLEN_ETHERNET;
    pkt.hops = DHCP_HOPS_DEFAULT;
    pkt.xid = NET_HTONL(s_dhcp_client_telem.xid);
    /* Without an address the reply must be broadcast; with one the server unicasts to ciaddr */
    pkt.flags = have_ip ? 0U : NET_HTONS(DHCP_FLAG_BROADCAST);
    pkt.ciaddr = have_ip ? NET_HTONL(our_ip) : 0U;

    net_config_t ncfg;
    net_get_config(&ncfg);
    memcpy(pkt.chaddr, ncfg.mac, ETH_ADDR_LEN);
    pkt.magic_cookie = NET_HTONL(DHCP_MAGIC_COOKIE);

    size_t idx = 0U;
    pkt.options[idx++] = DHCP_OPT_MSG_TYPE;
    pkt.options[idx++] = DHCP_OPT_U8_LEN;
    pkt.options[idx++] = discover ? DHCP_MSG_DISCOVER : DHCP_MSG_REQUEST;
    if (state == DHCP_CLIENT_STATE_REQUESTING || state == DHCP_CLIENT_STATE_REBOOTING)
    {
        idx = dhcp_client_put_u32_opt(pkt.options, idx, DHCP_OPT_REQUESTED_IP, our_ip);
    }
    if (state == DHCP_CLIENT_STATE_REQUESTING && s_dhcp_client_telem.server_ip != 0U)
    {
        idx = dhcp_client_put_u32_opt(pkt.options, idx, DHCP_OPT_SERVER_ID, s_dhcp_client_telem.server_ip);
    }
    pkt.options[idx++] = DHCP_OPT_PARAM_REQUEST_LIST;
    pkt.options[idx++] = (uint8_t)sizeof(s_dhcp_client_prl);
    memcpy(&pkt.options[idx], s_dhcp_client_prl, sizeof(s_dhcp_client_prl));
    idx += sizeof(s_dhcp_client_prl);
    pkt.options[idx++] = DHCP_OPT_END;

    const uint8_t *dest_mac = s_dhcp_client_bcast_mac;
    uint32_t dest_ip = DHCP_IP_BROADCAST;
    if (state == DHCP_CLIENT_STATE_RENEWING && s_dhcp_client_telem.server_ip != 0U)
    {
        dest_mac = s_server_mac;
        dest_ip = s_dhcp_client_telem.server_ip;
    }

    uint16_t pkt_len = (uint16_t)(sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN + idx);
    dhcp_status_t st = dhcp_send_udp_frame_from(WIFI_TX_IF_STA, dest_mac, dest_ip, have_ip ? our_ip : 0U,
                                                DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &pkt, pkt_len);
    if (s_tries > 0U)
    {
        s_dhcp_client_telem.retransmits++;
    }
    s_tries++;
    if (st == DHCP_OK)
    {
        if (discover)
        {
            s_dhcp_client_telem.discovers_sent++;
        }
        else
        {
            s_dhcp_client_telem.requests_sent++;
        }
    }
#if defined(__riscv)
    console_puts(discover ? "[DHCP] DISCOVER" : "[DHCP] REQUEST");
    console_puts(" (");
    console_puts(dhcp_client_state_name(state));
    console_puts(", try ");
    put_dec(s_tries);
    console_puts(st == DHCP_OK ? ")\r\n" : ", TX failed)\r\n");
#endif
    return st;
}

/* Sends for the current state and schedules the retransmission */
static dhcp_status_t dhcp_client_transmit(uint64_t now)
{
    dhcp_status_t st = dhcp_client_send();
    switch (s_dhcp_client_telem.state)
    {
        case DHCP_CLIENT_STATE_DISCOVERING:
            s_next_tx_us = now + dhcp_client_jittered(s_discover_backoff_us);
            s_discover_backoff_us = dhcp_client_next_backoff(s_discover_backoff_us);
            break;
        case DHCP_CLIENT_STATE_REQUESTING:
        case DHCP_CLIENT_STATE_REBOOTING:
            s_next_tx_us = now + dhcp_client_jittered(s_request_backoff_us);
            s_request_backoff_us = dhcp_client_next_backoff(s_request_backoff_us);
            break;
        case DHCP_CLIENT_STATE_RENEWING:
            s_next_tx_us = dhcp_client_renew_retry_at(now, s_lease_t2_us);
            break;
        case DHCP_CLIENT_STATE_REBINDING:
            s_next_tx_us = dhcp_client_renew_retry_at(now, s_lease_end_us);
            break;
        default:
            break;
    }
    return st;
}

/* INIT: a new DISCOVER exchange, first DISCOVER after delay_us (0: on the next tick) */
static void dhcp_client_enter_init(uint64_t now, uint64_t delay_us)
{
    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_DISCOVERING;
    dhcp_client_new_xid();
    s_tries = 0U;
    s_next_tx_us = now + delay_us;
}

/* A REQUEST exchange (REQUESTING, REBOOTING, RENEWING) starts: first send now */
static dhcp_status_t dhcp_client_start_request(uint64_t now, dhcp_client_state_t state)
{
    s_dhcp_client_telem.state = state;
    s_tries = 0U;
    s_request_backoff_us = DHCP_CLIENT_BACKOFF_INITIAL_US;
    s_exchange_start_us = now;
    return dhcp_client_transmit(now);
}

/* The address is gone (expiry or NAK): stop using it */
static void dhcp_client_drop_lease(void)
{
    bool in_use = (s_dhcp_client_telem.state == DHCP_CLIENT_STATE_BOUND ||
                   s_dhcp_client_telem.state == DHCP_CLIENT_STATE_RENEWING ||
                   s_dhcp_client_telem.state == DHCP_CLIENT_STATE_REBINDING ||
                   s_dhcp_client_telem.state == DHCP_CLIENT_STATE_REBOOTING);
    s_lease_valid = false;
    s_dhcp_client_telem.assigned_ip = 0U;
    if (in_use)
    {
        net_set_ip(0U, 0U, 0U);
    }
}

dhcp_status_t dhcp_client_init(void)
{
    memset(&s_dhcp_client_telem, 0, sizeof(s_dhcp_client_telem));
    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_IDLE;
    s_lease_valid = false;
    s_lease_t1_us = 0ULL;
    s_lease_t2_us = 0ULL;
    s_lease_end_us = 0ULL;
    memset(s_server_mac, 0, sizeof(s_server_mac));
    s_exchange_start_us = 0ULL;
    s_next_tx_us = 0ULL;
    s_request_backoff_us = DHCP_CLIENT_BACKOFF_INITIAL_US;
    s_discover_backoff_us = DHCP_CLIENT_BACKOFF_INITIAL_US;
    s_tries = 0U;
    s_dhcp_client_initialized = true;
    return DHCP_OK;
}

/* Link up: ask for the held address again (INIT-REBOOT) or start with DISCOVER */
dhcp_status_t dhcp_client_start(void)
{
    if (!s_dhcp_client_initialized)
    {
        dhcp_client_init();
    }

    uint64_t now = systimer_get_us();
    s_discover_backoff_us = DHCP_CLIENT_BACKOFF_INITIAL_US;
    dhcp_client_new_xid();

    if (s_lease_valid && now < s_lease_end_us)
    {
#if defined(__riscv)
        dhcp_client_log_ip("[DHCP] INIT-REBOOT: asking for ", s_dhcp_client_telem.assigned_ip);
        console_puts(" again\r\n");
#endif
        return dhcp_client_start_request(now, DHCP_CLIENT_STATE_REBOOTING);
    }

    s_lease_valid = false;
    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_DISCOVERING;
    s_tries = 0U;
    return dhcp_client_transmit(now);
}

void dhcp_client_tick(void)
{
    if (!s_dhcp_client_initialized)
    {
        return;
    }

    uint64_t now = systimer_get_us();

    if (s_lease_valid && now >= s_lease_end_us)
    {
        /* RFC 2131 4.4.5: lease over without an ACK; the address must not be used any more */
        bool running = (s_dhcp_client_telem.state != DHCP_CLIENT_STATE_IDLE &&
                        s_dhcp_client_telem.state != DHCP_CLIENT_STATE_STATIC);
        s_dhcp_client_telem.leases_expired++;
        dhcp_client_drop_lease();
#if defined(__riscv)
        console_puts("[DHCP] Lease expired\r\n");
#endif
        if (running)
        {
            dhcp_client_enter_init(now, 0ULL);
        }
    }

    switch (s_dhcp_client_telem.state)
    {
        case DHCP_CLIENT_STATE_DISCOVERING:
            if (now >= s_next_tx_us)
            {
                (void)dhcp_client_transmit(now);
            }
            break;
        case DHCP_CLIENT_STATE_REQUESTING:
        case DHCP_CLIENT_STATE_REBOOTING:
            if (now >= s_next_tx_us)
            {
                if (s_tries >= DHCP_CLIENT_REQUEST_MAX_TRIES)
                {
                    /* No ACK/NAK after the retransmissions: start over (an unanswered INIT-REBOOT
                     * does not keep the old address) */
#if defined(__riscv)
                    console_puts("[DHCP] No answer to REQUEST, back to DISCOVER\r\n");
#endif
                    if (s_dhcp_client_telem.state == DHCP_CLIENT_STATE_REBOOTING)
                    {
                        dhcp_client_drop_lease();
                    }
                    dhcp_client_enter_init(now, 0ULL);
                    (void)dhcp_client_transmit(now);
                }
                else
                {
                    (void)dhcp_client_transmit(now);
                }
            }
            break;
        case DHCP_CLIENT_STATE_BOUND:
            if (now >= s_lease_t1_us)
            {
                /* T1: one unicast REQUEST to the server that leased the address */
                dhcp_client_new_xid();
                (void)dhcp_client_start_request(now, DHCP_CLIENT_STATE_RENEWING);
            }
            break;
        case DHCP_CLIENT_STATE_RENEWING:
            if (now >= s_lease_t2_us)
            {
                /* T2: the leasing server did not answer; ask any server */
                s_dhcp_client_telem.state = DHCP_CLIENT_STATE_REBINDING;
                (void)dhcp_client_transmit(now);
            }
            else if (now >= s_next_tx_us)
            {
                (void)dhcp_client_transmit(now);
            }
            break;
        case DHCP_CLIENT_STATE_REBINDING:
            if (now >= s_next_tx_us)
            {
                (void)dhcp_client_transmit(now);
            }
            break;
        default:
            break;
    }
}

dhcp_status_t dhcp_client_stop(void)
{
    /* The lease stays for INIT-REBOOT; tick still drops it at expiry */
    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_IDLE;
    return DHCP_OK;
}

void dhcp_client_forget(void)
{
    s_lease_valid = false;
    if (s_dhcp_client_telem.state == DHCP_CLIENT_STATE_IDLE)
    {
        s_dhcp_client_telem.assigned_ip = 0U;
    }
}

dhcp_client_state_t dhcp_client_get_state(void)
{
    return s_dhcp_client_telem.state;
}

static uint32_t dhcp_client_secs_until(uint64_t now, uint64_t when_us)
{
    if (when_us == DHCP_CLIENT_TIME_NEVER)
    {
        return DHCP_LEASE_INFINITE;
    }
    return (when_us > now) ? (uint32_t)((when_us - now) / DHCP_US_PER_SEC) : 0U;
}

dhcp_status_t dhcp_client_get_telemetry(dhcp_client_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return DHCP_ERR_INVALID_ARG;
    }
    *out_telem = s_dhcp_client_telem;

    uint64_t now = systimer_get_us();
    out_telem->lease_left_sec = s_lease_valid ? dhcp_client_secs_until(now, s_lease_end_us) : 0U;
    switch (s_dhcp_client_telem.state)
    {
        case DHCP_CLIENT_STATE_BOUND:
            out_telem->next_tx_sec = dhcp_client_secs_until(now, s_lease_t1_us);
            break;
        case DHCP_CLIENT_STATE_DISCOVERING:
        case DHCP_CLIENT_STATE_REQUESTING:
        case DHCP_CLIENT_STATE_REBOOTING:
        case DHCP_CLIENT_STATE_RENEWING:
        case DHCP_CLIENT_STATE_REBINDING:
            out_telem->next_tx_sec = dhcp_client_secs_until(now, s_next_tx_us);
            break;
        default:
            out_telem->next_tx_sec = 0U;
            break;
    }
    return DHCP_OK;
}

void dhcp_client_set_static_fallback(uint32_t ip, uint32_t netmask, uint32_t gateway, uint32_t dns)
{
    s_lease_valid = false;
    s_dhcp_client_telem.assigned_ip = ip;
    s_dhcp_client_telem.netmask = netmask;
    s_dhcp_client_telem.gateway = gateway;
    s_dhcp_client_telem.dns_server = dns;
    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_STATIC;
    net_set_ip(ip, netmask, gateway);
    mdns_announce();
}

/* Options of one server reply */
typedef struct {
    uint8_t  msg_type;
    uint32_t server_id;
    uint32_t subnet_mask;
    uint32_t router;
    uint32_t dns;
    uint32_t lease_sec;
    uint32_t t1_sec;
    uint32_t t2_sec;
} dhcp_client_reply_opts_t;

static uint32_t dhcp_client_opt_u32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, DHCP_OPT_U32_LEN);
    return NET_NTOHL(v);
}

static void dhcp_client_parse_options(const dhcp_packet_t *pkt, size_t opts_len, dhcp_client_reply_opts_t *o)
{
    memset(o, 0, sizeof(*o));
    size_t off = 0U;
    while (off < opts_len)
    {
        uint8_t opt = pkt->options[off++];
        if (opt == DHCP_OPT_END) break;
        if (opt == DHCP_OPT_PAD) continue;
        if (off >= opts_len) break;
        uint8_t opt_len = pkt->options[off++];
        if (off + opt_len > opts_len) break;
        const uint8_t *val = &pkt->options[off];

        if (opt == DHCP_OPT_MSG_TYPE && opt_len == DHCP_OPT_U8_LEN)
        {
            o->msg_type = val[0];
        }
        else if (opt_len == DHCP_OPT_U32_LEN || (opt_len >= DHCP_OPT_IPV4_LEN &&
                 (opt == DHCP_OPT_ROUTER || opt == DHCP_OPT_DNS)))
        {
            /* Router and DNS may list several addresses: the first one is used */
            uint32_t v = dhcp_client_opt_u32(val);
            switch (opt)
            {
                case DHCP_OPT_SERVER_ID:      o->server_id = v; break;
                case DHCP_OPT_SUBNET_MASK:    o->subnet_mask = v; break;
                case DHCP_OPT_ROUTER:         o->router = v; break;
                case DHCP_OPT_DNS:            o->dns = v; break;
                case DHCP_OPT_LEASE_TIME:     o->lease_sec = v; break;
                case DHCP_OPT_RENEWAL_TIME:   o->t1_sec = v; break;
                case DHCP_OPT_REBINDING_TIME: o->t2_sec = v; break;
                default: break;
            }
        }
        off += opt_len;
    }
}

static uint64_t dhcp_client_lease_point_us(uint32_t secs)
{
    return s_exchange_start_us + ((uint64_t)secs * DHCP_US_PER_SEC);
}

/* DHCPACK: (re)bind, set T1/T2/expiry from the time the exchange's first REQUEST went out */
static void dhcp_client_bind(const dhcp_packet_t *pkt, const uint8_t *eth_frame, const dhcp_client_reply_opts_t *o)
{
    dhcp_client_state_t prev = s_dhcp_client_telem.state;
    uint32_t prev_ip = s_dhcp_client_telem.assigned_ip;
    uint32_t ip = NET_NTOHL(pkt->yiaddr);

    s_dhcp_client_telem.acks_received++;
    s_dhcp_client_telem.assigned_ip = ip;
    if (o->subnet_mask != 0U) s_dhcp_client_telem.netmask = o->subnet_mask;
    if (o->router != 0U) s_dhcp_client_telem.gateway = o->router;
    if (o->dns != 0U) s_dhcp_client_telem.dns_server = o->dns;
    if (o->server_id != 0U) s_dhcp_client_telem.server_ip = o->server_id;
    if (eth_frame != NULL)
    {
        memcpy(s_server_mac, ((const ethernet_header_t *)eth_frame)->src_mac, ETH_ADDR_LEN);
    }

    uint32_t lease = (o->lease_sec != 0U) ? o->lease_sec : DHCP_CLIENT_FALLBACK_LEASE_SEC;
    s_dhcp_client_telem.lease_time_sec = lease;
    if (lease == DHCP_LEASE_INFINITE)
    {
        s_dhcp_client_telem.t1_sec = DHCP_LEASE_INFINITE;
        s_dhcp_client_telem.t2_sec = DHCP_LEASE_INFINITE;
        s_lease_t1_us = DHCP_CLIENT_TIME_NEVER;
        s_lease_t2_us = DHCP_CLIENT_TIME_NEVER;
        s_lease_end_us = DHCP_CLIENT_TIME_NEVER;
    }
    else
    {
        /* Server values only when 0 < T1 < T2 < lease holds (RFC 2131 4.4.5) */
        uint32_t t2 = (uint32_t)(((uint64_t)lease * DHCP_CLIENT_T2_NUM) / DHCP_CLIENT_T2_DEN);
        if (o->t2_sec != 0U && o->t2_sec < lease)
        {
            t2 = o->t2_sec;
        }
        uint32_t t1 = (uint32_t)(((uint64_t)lease * DHCP_CLIENT_T1_NUM) / DHCP_CLIENT_T1_DEN);
        if (o->t1_sec != 0U && o->t1_sec < t2)
        {
            t1 = o->t1_sec;
        }
        else if (t1 > t2)
        {
            t1 = t2;
        }
        s_dhcp_client_telem.t1_sec = t1;
        s_dhcp_client_telem.t2_sec = t2;
        s_lease_t1_us = dhcp_client_lease_point_us(t1);
        s_lease_t2_us = dhcp_client_lease_point_us(t2);
        s_lease_end_us = dhcp_client_lease_point_us(lease);
    }

    bool renewed = (prev == DHCP_CLIENT_STATE_RENEWING || prev == DHCP_CLIENT_STATE_REBINDING);
    if (renewed)
    {
        s_dhcp_client_telem.renewals++;
    }
    if (prev == DHCP_CLIENT_STATE_REBOOTING && ip == prev_ip)
    {
        s_dhcp_client_telem.reboots_confirmed++;
    }

    s_lease_valid = true;
    s_dhcp_client_telem.state = DHCP_CLIENT_STATE_BOUND;
    s_discover_backoff_us = DHCP_CLIENT_BACKOFF_INITIAL_US;
    s_tries = 0U;

    net_set_ip(ip, s_dhcp_client_telem.netmask, s_dhcp_client_telem.gateway);

#if defined(__riscv)
    dhcp_client_log_ip(renewed ? "[DHCP] ACK: lease renewed, IP " : "[DHCP] ACK: bound, IP ", ip);
    dhcp_client_log_ip(", GW ", s_dhcp_client_telem.gateway);
    console_puts(", lease ");
    put_dec(lease);
    console_puts(" s, T1 ");
    put_dec(s_dhcp_client_telem.t1_sec);
    console_puts(" s\r\n");
#endif

    /* Announce on a new bind or (re)join, not on a quiet renewal of the same address (review 7.2) */
    if (!renewed || ip != prev_ip)
    {
        mdns_announce();
    }
}

dhcp_status_t dhcp_client_process_packet(const uint8_t *eth_frame, const uint8_t *payload, uint16_t len)
{
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

    dhcp_client_reply_opts_t o;
    dhcp_client_parse_options(pkt, len - (sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN), &o);

    dhcp_client_state_t state = s_dhcp_client_telem.state;
    bool requesting = (state == DHCP_CLIENT_STATE_REQUESTING || state == DHCP_CLIENT_STATE_REBOOTING ||
                       state == DHCP_CLIENT_STATE_RENEWING || state == DHCP_CLIENT_STATE_REBINDING);
    uint64_t now = systimer_get_us();

    if (o.msg_type == DHCP_MSG_OFFER && state == DHCP_CLIENT_STATE_DISCOVERING && pkt->yiaddr != 0U)
    {
        /* First OFFER wins (SELECTING); same xid for the REQUEST */
        s_dhcp_client_telem.offers_received++;
        s_dhcp_client_telem.assigned_ip = NET_NTOHL(pkt->yiaddr);
        s_dhcp_client_telem.server_ip = o.server_id;
        s_dhcp_client_telem.netmask = o.subnet_mask;
        s_dhcp_client_telem.gateway = o.router;
        s_dhcp_client_telem.dns_server = o.dns;
        s_dhcp_client_telem.lease_time_sec = o.lease_sec;
#if defined(__riscv)
        dhcp_client_log_ip("[DHCP] OFFER: ", s_dhcp_client_telem.assigned_ip);
        console_puts("\r\n");
#endif
        (void)dhcp_client_start_request(now, DHCP_CLIENT_STATE_REQUESTING);
        return DHCP_OK;
    }
    if (o.msg_type == DHCP_MSG_ACK && requesting)
    {
        dhcp_client_bind(pkt, eth_frame, &o);
        return DHCP_OK;
    }
    if (o.msg_type == DHCP_MSG_NAK && requesting)
    {
        /* Address refused (other network, lease gone): INIT after the DISCOVER backoff, so a server
         * that keeps refusing does not cause a fast loop */
        s_dhcp_client_telem.naks_received++;
#if defined(__riscv)
        console_puts("[DHCP] NAK in ");
        console_puts(dhcp_client_state_name(state));
        console_puts(", back to DISCOVER\r\n");
#endif
        dhcp_client_drop_lease();
        dhcp_client_enter_init(now, dhcp_client_jittered(s_discover_backoff_us));
        s_discover_backoff_us = dhcp_client_next_backoff(s_discover_backoff_us);
        return DHCP_OK;
    }

    return DHCP_OK;
}
