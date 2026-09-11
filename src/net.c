/*
 * src/net.c
 *
 * Bare-Metal Zero-Copy IPv4, ARP, ICMP, UDP & Lightweight TCP Protocol Stack
 * RFC 791 (IPv4), RFC 792 (ICMP), RFC 793 (TCP), RFC 826 (ARP), RFC 1071 (Checksum)
 *
 * Implements deterministic static ARP cache, internet checksum calculation,
 * packet deserialization/routing, ICMP echo reply generation, and interface telemetry.
 */

#include "net.h"
#include "tcp.h"
#include "wifi.h"
#include "string.h"

#if defined(__riscv)
#include "systimer.h"
static inline uint32_t net_time_ms(void)
{
    return (uint32_t)systimer_get_ms();
}
#else
static inline uint32_t net_time_ms(void)
{
    static uint32_t s_mock_net_ms = 0;
    return ++s_mock_net_ms;
}
#endif

/* ========================================================================= */
/* Static Storage & Network Interface State (Zero Dynamic Heap Allocation)   */
/* ========================================================================= */
static net_config_t s_net_config = {
    .ip      = NET_DEFAULT_IP,
    .netmask = NET_DEFAULT_NETMASK,
    .gateway = NET_DEFAULT_GATEWAY,
    .mac     = {0x40U, 0x4CU, 0xCAU, 0x45U, 0x1EU, 0x14U}
};

static arp_entry_t s_arp_table[ARP_TABLE_CAPACITY];
static net_telemetry_t s_net_telemetry;
static bool s_net_initialized = false;

/* ========================================================================= */
/* Subsystem Lifecycle Initialization                                        */
/* ========================================================================= */

net_status_t net_init(void)
{
    /* Clear ARP table */
    for (uint32_t i = 0; i < ARP_TABLE_CAPACITY; i++)
    {
        s_arp_table[i].ip = 0U;
        memset(s_arp_table[i].mac, 0, ETH_ADDR_LEN);
        s_arp_table[i].valid = false;
        s_arp_table[i].updated_ticks = 0U;
    }

    /* Clear telemetry */
    memset(&s_net_telemetry, 0, sizeof(s_net_telemetry));

    /* Initialize default configuration only on first initialization */
    if (!s_net_initialized)
    {
        s_net_config.ip      = NET_DEFAULT_IP;
        s_net_config.netmask = NET_DEFAULT_NETMASK;
        s_net_config.gateway = NET_DEFAULT_GATEWAY;

        /* Extract authentic Station MAC from Wi-Fi subsystem if available */
        uint8_t mac_buf[ETH_ADDR_LEN] = {0};
        if (wifi_get_mac_addr(mac_buf) == WIFI_OK)
        {
            memcpy(s_net_config.mac, mac_buf, ETH_ADDR_LEN);
        }
        else
        {
            s_net_config.mac[0] = 0x40U;
            s_net_config.mac[1] = 0x4CU;
            s_net_config.mac[2] = 0xCAU;
            s_net_config.mac[3] = 0x45U;
            s_net_config.mac[4] = 0x1EU;
            s_net_config.mac[5] = 0x14U;
        }

        s_net_initialized = true;
    }

    return NET_OK;
}

net_status_t net_reset_defaults(void)
{
    s_net_config.ip      = NET_DEFAULT_IP;
    s_net_config.netmask = NET_DEFAULT_NETMASK;
    s_net_config.gateway = NET_DEFAULT_GATEWAY;
    return NET_OK;
}

net_status_t net_set_ip(uint32_t ip, uint32_t netmask, uint32_t gateway)
{
    if (!s_net_initialized)
    {
        net_init();
    }

    s_net_config.ip      = ip;
    s_net_config.netmask = netmask;
    s_net_config.gateway = gateway;
    return NET_OK;
}

net_status_t net_get_config(net_config_t *out_config)
{
    if (out_config == NULL)
    {
        return NET_ERR_INVALID_ARG;
    }

    if (!s_net_initialized)
    {
        net_init();
    }

    *out_config = s_net_config;
    return NET_OK;
}

net_status_t net_get_telemetry(net_telemetry_t *out_telemetry)
{
    if (out_telemetry == NULL)
    {
        return NET_ERR_INVALID_ARG;
    }

    *out_telemetry = s_net_telemetry;
    return NET_OK;
}

/* ========================================================================= */
/* RFC 1071 Internet Checksum Calculations                                   */
/* ========================================================================= */

uint16_t net_checksum(const void *data, size_t len)
{
    if (data == NULL || len == 0U)
    {
        return 0U;
    }

    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t sum = 0U;

    /* Accumulate 16-bit words (big-endian order on wire) */
    while (len > 1U)
    {
        uint16_t word = ((uint16_t)ptr[0] << 8U) | (uint16_t)ptr[1];
        sum += (uint32_t)word;
        ptr += 2U;
        len -= 2U;
    }

    /* Add trailing odd byte padded with zero */
    if (len > 0U)
    {
        uint16_t word = (uint16_t)ptr[0] << 8U;
        sum += (uint32_t)word;
    }

    /* Fold 32-bit carries into 16 bits */
    while ((sum >> 16U) != 0U)
    {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }

    /* One's complement inversion */
    return (uint16_t)(~sum & 0xFFFFU);
}

uint16_t net_ipv4_checksum(const ipv4_header_t *hdr)
{
    if (hdr == NULL)
    {
        return 0U;
    }

    uint8_t ihl_words = hdr->ver_ihl & 0x0FU;
    if (ihl_words < IPV4_IHL_MIN_WORDS)
    {
        ihl_words = IPV4_IHL_MIN_WORDS;
    }
    size_t hdr_len = (size_t)ihl_words * 4U;

    return net_checksum(hdr, hdr_len);
}

uint16_t net_tcp_checksum(uint32_t src_ip, uint32_t dest_ip,
                          const tcp_header_t *tcp_hdr, uint16_t tcp_len,
                          const void *payload, uint16_t payload_len)
{
    if (tcp_hdr == NULL)
    {
        return 0U;
    }

    ipv4_pseudo_header_t pseudo;
    pseudo.src_ip   = NET_HTONL(src_ip);
    pseudo.dest_ip  = NET_HTONL(dest_ip);
    pseudo.zero     = 0U;
    pseudo.protocol = IPV4_PROTO_TCP;
    pseudo.length   = NET_HTONS(tcp_len + payload_len);

    uint32_t sum = 0U;

    /* 1. Accumulate pseudo-header */
    const uint8_t *p_ptr = (const uint8_t *)&pseudo;
    for (size_t i = 0; i < sizeof(pseudo); i += 2U)
    {
        uint16_t w = ((uint16_t)p_ptr[i] << 8U) | (uint16_t)p_ptr[i + 1U];
        sum += (uint32_t)w;
    }

    /* 2. Accumulate TCP header */
    const uint8_t *t_ptr = (const uint8_t *)tcp_hdr;
    for (size_t i = 0; i < tcp_len; i += 2U)
    {
        uint16_t w = ((uint16_t)t_ptr[i] << 8U) | (uint16_t)t_ptr[i + 1U];
        sum += (uint32_t)w;
    }

    /* 3. Accumulate payload */
    if (payload != NULL && payload_len > 0U)
    {
        const uint8_t *d_ptr = (const uint8_t *)payload;
        size_t rem = payload_len;
        while (rem > 1U)
        {
            uint16_t w = ((uint16_t)d_ptr[0] << 8U) | (uint16_t)d_ptr[1];
            sum += (uint32_t)w;
            d_ptr += 2U;
            rem -= 2U;
        }
        if (rem > 0U)
        {
            uint16_t w = (uint16_t)d_ptr[0] << 8U;
            sum += (uint32_t)w;
        }
    }

    /* Fold and invert */
    while ((sum >> 16U) != 0U)
    {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }

    return (uint16_t)(~sum & 0xFFFFU);
}

uint16_t net_udp_checksum(uint32_t src_ip, uint32_t dest_ip,
                          const udp_header_t *udp_hdr,
                          const void *payload, uint16_t payload_len)
{
    if (udp_hdr == NULL)
    {
        return 0U;
    }

    ipv4_pseudo_header_t pseudo;
    pseudo.src_ip   = NET_HTONL(src_ip);
    pseudo.dest_ip  = NET_HTONL(dest_ip);
    pseudo.zero     = 0U;
    pseudo.protocol = IPV4_PROTO_UDP;
    pseudo.length   = NET_HTONS(UDP_HDR_LEN + payload_len);

    uint32_t sum = 0U;

    const uint8_t *p_ptr = (const uint8_t *)&pseudo;
    for (size_t i = 0; i < sizeof(pseudo); i += 2U)
    {
        uint16_t w = ((uint16_t)p_ptr[i] << 8U) | (uint16_t)p_ptr[i + 1U];
        sum += (uint32_t)w;
    }

    const uint8_t *u_ptr = (const uint8_t *)udp_hdr;
    for (size_t i = 0; i < UDP_HDR_LEN; i += 2U)
    {
        uint16_t w = ((uint16_t)u_ptr[i] << 8U) | (uint16_t)u_ptr[i + 1U];
        sum += (uint32_t)w;
    }

    if (payload != NULL && payload_len > 0U)
    {
        const uint8_t *d_ptr = (const uint8_t *)payload;
        size_t rem = payload_len;
        while (rem > 1U)
        {
            uint16_t w = ((uint16_t)d_ptr[0] << 8U) | (uint16_t)d_ptr[1];
            sum += (uint32_t)w;
            d_ptr += 2U;
            rem -= 2U;
        }
        if (rem > 0U)
        {
            uint16_t w = (uint16_t)d_ptr[0] << 8U;
            sum += (uint32_t)w;
        }
    }

    while ((sum >> 16U) != 0U)
    {
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    }

    uint16_t res = (uint16_t)(~sum & 0xFFFFU);
    return (res == 0U) ? 0xFFFFU : res;
}

/* ========================================================================= */
/* ARP Resolution & Static Cache Management                                  */
/* ========================================================================= */

net_status_t arp_lookup(uint32_t ip, uint8_t *out_mac)
{
    if (out_mac == NULL)
    {
        return NET_ERR_INVALID_ARG;
    }

    for (uint32_t i = 0; i < ARP_TABLE_CAPACITY; i++)
    {
        if (s_arp_table[i].valid && s_arp_table[i].ip == ip)
        {
            memcpy(out_mac, s_arp_table[i].mac, ETH_ADDR_LEN);
            return NET_OK;
        }
    }

    return NET_ERR_NOT_FOUND;
}

net_status_t arp_insert(uint32_t ip, const uint8_t *mac)
{
    if (mac == NULL)
    {
        return NET_ERR_INVALID_ARG;
    }

    /* 1. Check if IP already exists to update in-place */
    for (uint32_t i = 0; i < ARP_TABLE_CAPACITY; i++)
    {
        if (s_arp_table[i].valid && s_arp_table[i].ip == ip)
        {
            memcpy(s_arp_table[i].mac, mac, ETH_ADDR_LEN);
            s_arp_table[i].updated_ticks = net_time_ms();
            return NET_OK;
        }
    }

    /* 2. Find empty slot */
    for (uint32_t i = 0; i < ARP_TABLE_CAPACITY; i++)
    {
        if (!s_arp_table[i].valid)
        {
            s_arp_table[i].ip = ip;
            memcpy(s_arp_table[i].mac, mac, ETH_ADDR_LEN);
            s_arp_table[i].valid = true;
            s_arp_table[i].updated_ticks = net_time_ms();
            return NET_OK;
        }
    }

    /* 3. Evict slot 0 if table full (deterministic FIFO replacement) */
    s_arp_table[0].ip = ip;
    memcpy(s_arp_table[0].mac, mac, ETH_ADDR_LEN);
    s_arp_table[0].valid = true;
    s_arp_table[0].updated_ticks = net_time_ms();

    return NET_OK;
}

net_status_t arp_process_packet(const uint8_t *in_frame, uint16_t in_len,
                                uint8_t *out_reply, uint16_t max_out_len,
                                uint16_t *out_reply_len)
{
    if (in_frame == NULL || in_len < (ETH_HDR_LEN + ARP_HDR_LEN))
    {
        return NET_ERR_INVALID_ARG;
    }

    const arp_frame_t *frame = (const arp_frame_t *)in_frame;
    uint16_t ethertype = NET_NTOHS(frame->eth.ethertype);
    if (ethertype != ETHERTYPE_ARP)
    {
        return NET_ERR_UNKNOWN_PROTO;
    }

    uint16_t hw_type    = NET_NTOHS(frame->arp.hw_type);
    uint16_t proto_type = NET_NTOHS(frame->arp.proto_type);
    uint16_t opcode     = NET_NTOHS(frame->arp.opcode);

    if (hw_type != ARP_HW_TYPE_ETHERNET || proto_type != ARP_PROTO_IPV4)
    {
        return NET_ERR_UNKNOWN_PROTO;
    }

    uint32_t sender_ip = NET_NTOHL(frame->arp.sender_ip);
    uint32_t target_ip = NET_NTOHL(frame->arp.target_ip);

    /* Update ARP cache with sender details */
    arp_insert(sender_ip, frame->arp.sender_mac);

    s_net_telemetry.arp_requests_rx++;

    /* Only generate a reply if this is an ARP Request for our IP */
    if (opcode == ARP_OPCODE_REQUEST && target_ip == s_net_config.ip)
    {
        if (out_reply == NULL || max_out_len < sizeof(arp_frame_t) || out_reply_len == NULL)
        {
            return NET_ERR_BUFFER_TOO_SMALL;
        }

        arp_frame_t *reply = (arp_frame_t *)out_reply;

        /* Ethernet Header */
        memcpy(reply->eth.dest_mac, frame->arp.sender_mac, ETH_ADDR_LEN);
        memcpy(reply->eth.src_mac, s_net_config.mac, ETH_ADDR_LEN);
        reply->eth.ethertype = NET_HTONS(ETHERTYPE_ARP);

        /* ARP Packet Body */
        reply->arp.hw_type    = NET_HTONS(ARP_HW_TYPE_ETHERNET);
        reply->arp.proto_type = NET_HTONS(ARP_PROTO_IPV4);
        reply->arp.hw_size    = ETH_ADDR_LEN;
        reply->arp.proto_size = IPV4_ADDR_LEN;
        reply->arp.opcode     = NET_HTONS(ARP_OPCODE_REPLY);

        memcpy(reply->arp.sender_mac, s_net_config.mac, ETH_ADDR_LEN);
        reply->arp.sender_ip  = NET_HTONL(s_net_config.ip);

        memcpy(reply->arp.target_mac, frame->arp.sender_mac, ETH_ADDR_LEN);
        reply->arp.target_ip  = NET_HTONL(sender_ip);

        *out_reply_len = (uint16_t)sizeof(arp_frame_t);
        s_net_telemetry.arp_replies_tx++;
        return NET_OK;
    }

    return NET_OK;
}

/* ========================================================================= */
/* ICMP Echo Processing                                                      */
/* ========================================================================= */

net_status_t icmp_process_packet(const uint8_t *in_packet, uint16_t in_len,
                                 uint8_t *out_reply, uint16_t max_out_len,
                                 uint16_t *out_reply_len)
{
    size_t min_len = ETH_HDR_LEN + IPV4_MIN_HDR_LEN + ICMP_MIN_HDR_LEN;
    if (in_packet == NULL || in_len < min_len)
    {
        return NET_ERR_INVALID_ARG;
    }

    const ethernet_header_t *eth_in = (const ethernet_header_t *)in_packet;
    const ipv4_header_t *ip_in = (const ipv4_header_t *)(in_packet + ETH_HDR_LEN);

    uint8_t ihl = (ip_in->ver_ihl & 0x0FU) * 4U;
    if (ihl < IPV4_MIN_HDR_LEN || in_len < (ETH_HDR_LEN + ihl + ICMP_MIN_HDR_LEN))
    {
        return NET_ERR_FRAME_CORRUPT;
    }

    const icmp_header_t *icmp_in = (const icmp_header_t *)(in_packet + ETH_HDR_LEN + ihl);

    if (icmp_in->type != ICMP_TYPE_ECHO_REQUEST || icmp_in->code != ICMP_CODE_ECHO)
    {
        return NET_OK;
    }

    s_net_telemetry.icmp_rx++;

    if (out_reply == NULL || max_out_len < in_len || out_reply_len == NULL)
    {
        return NET_ERR_BUFFER_TOO_SMALL;
    }

    /* Build reply by copying incoming frame and mutating headers in-place */
    memcpy(out_reply, in_packet, in_len);

    ethernet_header_t *eth_out = (ethernet_header_t *)out_reply;
    ipv4_header_t *ip_out = (ipv4_header_t *)(out_reply + ETH_HDR_LEN);
    icmp_header_t *icmp_out = (icmp_header_t *)(out_reply + ETH_HDR_LEN + ihl);

    /* Swap Ethernet MAC addresses */
    memcpy(eth_out->dest_mac, eth_in->src_mac, ETH_ADDR_LEN);
    memcpy(eth_out->src_mac, s_net_config.mac, ETH_ADDR_LEN);

    /* Swap IP addresses & reset TTL */
    uint32_t src_ip = ip_in->src_ip;
    ip_out->src_ip  = NET_HTONL(s_net_config.ip);
    ip_out->dest_ip = src_ip;
    ip_out->ttl     = IPV4_TTL_DEFAULT;
    ip_out->checksum = 0U;
    ip_out->checksum = net_ipv4_checksum(ip_out);

    /* Convert to ICMP Echo Reply */
    icmp_out->type = ICMP_TYPE_ECHO_REPLY;
    icmp_out->code = ICMP_CODE_ECHO;
    icmp_out->checksum = 0U;

    size_t icmp_len = (size_t)in_len - (ETH_HDR_LEN + ihl);
    icmp_out->checksum = net_checksum(icmp_out, icmp_len);

    *out_reply_len = in_len;
    s_net_telemetry.icmp_tx++;
    return NET_OK;
}

/* ========================================================================= */
/* Inbound Packet Processing & Protocol Routing                              */
/* ========================================================================= */

net_status_t net_input(const uint8_t *frame, uint16_t len)
{
    if (frame == NULL || len < ETH_HDR_LEN)
    {
        s_net_telemetry.dropped_packets++;
        return NET_ERR_INVALID_ARG;
    }

    s_net_telemetry.rx_packets++;
    s_net_telemetry.rx_bytes += len;

    const ethernet_header_t *eth = (const ethernet_header_t *)frame;
    uint16_t ethertype = NET_NTOHS(eth->ethertype);

    if (ethertype == ETHERTYPE_ARP)
    {
        uint8_t reply_buf[sizeof(arp_frame_t)];
        uint16_t reply_len = 0U;
        net_status_t st = arp_process_packet(frame, len, reply_buf, sizeof(reply_buf), &reply_len);
        if (st == NET_OK && reply_len > 0U)
        {
            wifi_tx_packet(reply_buf, reply_len);
            s_net_telemetry.tx_packets++;
            s_net_telemetry.tx_bytes += reply_len;
        }
        return st;
    }
    else if (ethertype == ETHERTYPE_IPV4)
    {
        if (len < (ETH_HDR_LEN + IPV4_MIN_HDR_LEN))
        {
            s_net_telemetry.dropped_packets++;
            return NET_ERR_FRAME_CORRUPT;
        }

        const ipv4_header_t *ip = (const ipv4_header_t *)(frame + ETH_HDR_LEN);

        /* Verify IPv4 checksum */
        if (net_ipv4_checksum(ip) != 0U)
        {
            s_net_telemetry.checksum_errors++;
            s_net_telemetry.dropped_packets++;
            return NET_ERR_CHECKSUM;
        }

        s_net_telemetry.ipv4_rx++;

        uint8_t ihl = (ip->ver_ihl & 0x0FU) * 4U;
        if (len < (ETH_HDR_LEN + ihl))
        {
            s_net_telemetry.dropped_packets++;
            return NET_ERR_FRAME_CORRUPT;
        }

        if (ip->protocol == IPV4_PROTO_ICMP)
        {
            uint8_t icmp_reply[NET_MAX_FRAME_SIZE];
            uint16_t icmp_reply_len = 0U;
            net_status_t st = icmp_process_packet(frame, len, icmp_reply, sizeof(icmp_reply), &icmp_reply_len);
            if (st == NET_OK && icmp_reply_len > 0U)
            {
                wifi_tx_packet(icmp_reply, icmp_reply_len);
                s_net_telemetry.tx_packets++;
                s_net_telemetry.tx_bytes += icmp_reply_len;
            }
            return st;
        }
        else if (ip->protocol == IPV4_PROTO_TCP)
        {
            s_net_telemetry.tcp_rx++;
            tcp_status_t tst = tcp_input(frame + ETH_HDR_LEN, len - ETH_HDR_LEN);
            return (tst == TCP_OK) ? NET_OK : NET_ERR_INVALID_ARG;
        }
        else if (ip->protocol == IPV4_PROTO_UDP)
        {
            s_net_telemetry.udp_rx++;
            return NET_OK;
        }

        return NET_ERR_UNKNOWN_PROTO;
    }

    return NET_OK;
}

/* ========================================================================= */
/* Outbound Transport Transmission                                           */
/* ========================================================================= */

net_status_t net_send_udp(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port,
                          const void *data, uint16_t len)
{
    if (data == NULL && len > 0U)
    {
        return NET_ERR_INVALID_ARG;
    }

    uint8_t dest_mac[ETH_ADDR_LEN];
    net_status_t st = arp_lookup(dest_ip, dest_mac);
    if (st != NET_OK)
    {
        /* If destination IP is on local subnet, fail; else look up gateway */
        st = arp_lookup(s_net_config.gateway, dest_mac);
        if (st != NET_OK)
        {
            /* Broadcast MAC as fallback */
            memset(dest_mac, 0xFF, ETH_ADDR_LEN);
        }
    }

    uint16_t total_frame_len = (uint16_t)(ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN + len);
    if (total_frame_len > NET_MAX_FRAME_SIZE)
    {
        return NET_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t frame_buf[NET_MAX_FRAME_SIZE];
    ethernet_header_t *eth = (ethernet_header_t *)frame_buf;
    ipv4_header_t *ip = (ipv4_header_t *)(frame_buf + ETH_HDR_LEN);
    udp_header_t *udp = (udp_header_t *)(frame_buf + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    uint8_t *payload = frame_buf + ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN;

    /* Ethernet Header */
    memcpy(eth->dest_mac, dest_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, s_net_config.mac, ETH_ADDR_LEN);
    eth->ethertype = NET_HTONS(ETHERTYPE_IPV4);

    /* IPv4 Header */
    ip->ver_ihl           = IPV4_VER_IHL_DEFAULT;
    ip->tos               = IPV4_TOS_DEFAULT;
    ip->total_len         = NET_HTONS(IPV4_MIN_HDR_LEN + UDP_HDR_LEN + len);
    ip->identification    = 0x1234U;
    ip->flags_frag_offset = NET_HTONS(IPV4_FLAGS_DF);
    ip->ttl               = IPV4_TTL_DEFAULT;
    ip->protocol          = IPV4_PROTO_UDP;
    ip->src_ip            = NET_HTONL(s_net_config.ip);
    ip->dest_ip           = NET_HTONL(dest_ip);
    ip->checksum          = 0U;
    ip->checksum          = net_ipv4_checksum(ip);

    /* UDP Header */
    udp->src_port  = NET_HTONS(src_port);
    udp->dest_port = NET_HTONS(dest_port);
    udp->length    = NET_HTONS(UDP_HDR_LEN + len);
    udp->checksum  = 0U;

    if (data != NULL && len > 0U)
    {
        memcpy(payload, data, len);
    }
    udp->checksum = net_udp_checksum(s_net_config.ip, dest_ip, udp, data, len);

    wifi_status_t wst = wifi_tx_packet(frame_buf, total_frame_len);
    if (wst == WIFI_OK)
    {
        s_net_telemetry.tx_packets++;
        s_net_telemetry.tx_bytes += total_frame_len;
        s_net_telemetry.udp_tx++;
        s_net_telemetry.ipv4_tx++;
        return NET_OK;
    }

    return NET_ERR_QUEUE_FULL;
}

/* ========================================================================= */
/* String Formatting Helpers                                                 */
/* ========================================================================= */

void net_ip_to_str(uint32_t ip, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len < NET_IP_STR_BUF_LEN)
    {
        return;
    }

    uint8_t octets[4];
    octets[0] = (uint8_t)((ip >> 24U) & 0xFFU);
    octets[1] = (uint8_t)((ip >> 16U) & 0xFFU);
    octets[2] = (uint8_t)((ip >> 8U)  & 0xFFU);
    octets[3] = (uint8_t)(ip & 0xFFU);

    size_t idx = 0U;
    for (int i = 0; i < 4; i++)
    {
        uint8_t val = octets[i];
        if (val >= 100U)
        {
            buf[idx++] = (char)('0' + (val / 100U));
            val %= 100U;
            buf[idx++] = (char)('0' + (val / 10U));
            val %= 10U;
        }
        else if (val >= 10U)
        {
            buf[idx++] = (char)('0' + (val / 10U));
            val %= 10U;
        }
        buf[idx++] = (char)('0' + val);

        if (i < 3)
        {
            buf[idx++] = '.';
        }
    }
    buf[idx] = '\0';
}

uint32_t net_str_to_ip(const char *str)
{
    if (str == NULL)
    {
        return 0U;
    }

    uint32_t ip = 0U;
    uint32_t octet = 0U;
    int octet_count = 0;

    while (*str != '\0')
    {
        if (*str >= '0' && *str <= '9')
        {
            octet = (octet * 10U) + (uint32_t)(*str - '0');
            if (octet > 255U)
            {
                return 0U;
            }
        }
        else if (*str == '.')
        {
            ip = (ip << 8U) | (octet & 0xFFU);
            octet = 0U;
            octet_count++;
            if (octet_count > 3)
            {
                return 0U;
            }
        }
        else
        {
            break;
        }
        str++;
    }

    if (octet_count == 3)
    {
        ip = (ip << 8U) | (octet & 0xFFU);
        return ip;
    }

    return 0U;
}
