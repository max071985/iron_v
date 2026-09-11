/*
 * src/tcp.c
 *
 * Lightweight Bare-Metal TCP State Machine & Connection Engine
 * RFC 793 (Transmission Control Protocol)
 *
 * Implements deterministic static Protocol Control Blocks (PCBs), connection state
 * transitions (LISTEN, SYN_SENT, SYN_RECEIVED, ESTABLISHED, FIN_WAIT, CLOSED),
 * sequence number arithmetic, window management, and zero-allocation socket APIs.
 */

#include "tcp.h"
#include "net.h"
#include "wifi.h"
#include "string.h"

#if defined(__riscv)
#include "systimer.h"
static inline uint32_t tcp_time_ms(void)
{
    return (uint32_t)systimer_get_ms();
}
#else
static inline uint32_t tcp_time_ms(void)
{
    static uint32_t s_mock_tcp_ms = 0;
    return ++s_mock_tcp_ms;
}
#endif

/* ========================================================================= */
/* Static Storage & Connection State (Zero Dynamic Heap Allocation)          */
/* ========================================================================= */
static tcp_pcb_t s_tcp_pcbs[TCP_MAX_PCBS];
static tcp_telemetry_t s_tcp_telemetry;
static bool s_tcp_initialized = false;

/* Helper to format and transmit a TCP segment over Ethernet + IPv4 */
static tcp_status_t tcp_send_segment(tcp_pcb_t *pcb, uint8_t flags,
                                     const void *payload, uint16_t payload_len)
{
    if (pcb == NULL)
    {
        return TCP_ERR_ARG;
    }

    net_config_t net_cfg;
    net_get_config(&net_cfg);

    uint8_t dest_mac[ETH_ADDR_LEN];
    net_status_t ast = arp_lookup(pcb->remote_ip, dest_mac);
    if (ast != NET_OK)
    {
        ast = arp_lookup(net_cfg.gateway, dest_mac);
        if (ast != NET_OK)
        {
            memset(dest_mac, 0xFF, ETH_ADDR_LEN);
        }
    }

    uint16_t tcp_len = (uint16_t)(TCP_MIN_HDR_LEN + payload_len);
    uint16_t total_frame_len = (uint16_t)(ETH_HDR_LEN + IPV4_MIN_HDR_LEN + tcp_len);

    if (total_frame_len > NET_MAX_FRAME_SIZE)
    {
        return TCP_ERR_MEM;
    }

    uint8_t frame[NET_MAX_FRAME_SIZE];
    ethernet_header_t *eth = (ethernet_header_t *)frame;
    ipv4_header_t *ip = (ipv4_header_t *)(frame + ETH_HDR_LEN);
    tcp_header_t *tcp = (tcp_header_t *)(frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    uint8_t *data_dst = frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN;

    /* Ethernet Header */
    memcpy(eth->dest_mac, dest_mac, ETH_ADDR_LEN);
    memcpy(eth->src_mac, net_cfg.mac, ETH_ADDR_LEN);
    eth->ethertype = NET_HTONS(ETHERTYPE_IPV4);

    /* IPv4 Header */
    ip->ver_ihl           = IPV4_VER_IHL_DEFAULT;
    ip->tos               = IPV4_TOS_DEFAULT;
    ip->total_len         = NET_HTONS(IPV4_MIN_HDR_LEN + tcp_len);
    ip->identification    = (uint16_t)(pcb->snd_nxt & 0xFFFFU);
    ip->flags_frag_offset = NET_HTONS(IPV4_FLAGS_DF);
    ip->ttl               = IPV4_TTL_DEFAULT;
    ip->protocol          = IPV4_PROTO_TCP;
    ip->src_ip            = NET_HTONL(net_cfg.ip);
    ip->dest_ip           = NET_HTONL(pcb->remote_ip);
    ip->checksum          = 0U;
    ip->checksum          = net_ipv4_checksum(ip);

    /* TCP Header */
    tcp->src_port              = NET_HTONS(pcb->local_port);
    tcp->dest_port             = NET_HTONS(pcb->remote_port);
    tcp->seq_num               = NET_HTONL(pcb->snd_nxt);
    tcp->ack_num               = NET_HTONL(pcb->rcv_nxt);
    tcp->data_offset_reserved  = (uint8_t)((TCP_MIN_HDR_LEN / 4U) << TCP_DATA_OFFSET_SHIFT);
    tcp->flags                 = flags;
    tcp->window                = NET_HTONS(pcb->rcv_wnd);
    tcp->checksum              = 0U;
    tcp->urgent_ptr            = 0U;

    if (payload != NULL && payload_len > 0U)
    {
        memcpy(data_dst, payload, payload_len);
    }

    tcp->checksum = net_tcp_checksum(net_cfg.ip, pcb->remote_ip, tcp, TCP_MIN_HDR_LEN, payload, payload_len);

    wifi_status_t wst = wifi_tx_packet(frame, total_frame_len);
    if (wst == WIFI_OK)
    {
        s_tcp_telemetry.bytes_tx += payload_len;
        return TCP_OK;
    }

    return TCP_ERR_MEM;
}

/* ========================================================================= */
/* Core Lifecycle Initialization                                             */
/* ========================================================================= */

tcp_status_t tcp_init(void)
{
    for (uint32_t i = 0; i < TCP_MAX_PCBS; i++)
    {
        memset(&s_tcp_pcbs[i], 0, sizeof(tcp_pcb_t));
        s_tcp_pcbs[i].state   = TCP_STATE_CLOSED;
        s_tcp_pcbs[i].in_use  = false;
        s_tcp_pcbs[i].rcv_wnd = TCP_DEFAULT_WINDOW_BYTES;
        s_tcp_pcbs[i].snd_wnd = TCP_DEFAULT_WINDOW_BYTES;
    }

    memset(&s_tcp_telemetry, 0, sizeof(s_tcp_telemetry));
    s_tcp_initialized = true;
    return TCP_OK;
}

/* ========================================================================= */
/* PCB Allocation & Binding APIs                                             */
/* ========================================================================= */

tcp_pcb_t *tcp_new(void)
{
    if (!s_tcp_initialized)
    {
        tcp_init();
    }

    for (uint32_t i = 0; i < TCP_MAX_PCBS; i++)
    {
        if (!s_tcp_pcbs[i].in_use)
        {
            memset(&s_tcp_pcbs[i], 0, sizeof(tcp_pcb_t));
            s_tcp_pcbs[i].in_use  = true;
            s_tcp_pcbs[i].state   = TCP_STATE_CLOSED;
            s_tcp_pcbs[i].rcv_wnd = TCP_DEFAULT_WINDOW_BYTES;
            s_tcp_pcbs[i].snd_wnd = TCP_DEFAULT_WINDOW_BYTES;
            s_tcp_pcbs[i].snd_nxt = TCP_INITIAL_SEQ_NUM + (i * 0x01000000U);
            s_tcp_pcbs[i].snd_una = s_tcp_pcbs[i].snd_nxt;
            return &s_tcp_pcbs[i];
        }
    }

    return NULL;
}

tcp_status_t tcp_bind(tcp_pcb_t *pcb, uint16_t port)
{
    if (pcb == NULL || port == 0U)
    {
        return TCP_ERR_ARG;
    }

    net_config_t cfg;
    net_get_config(&cfg);

    pcb->local_ip   = cfg.ip;
    pcb->local_port = port;
    return TCP_OK;
}

tcp_status_t tcp_listen(tcp_pcb_t *pcb, tcp_accept_fn accept_cb)
{
    if (pcb == NULL || pcb->local_port == 0U)
    {
        return TCP_ERR_ARG;
    }

    pcb->state     = TCP_STATE_LISTEN;
    pcb->accept_cb = accept_cb;
    s_tcp_telemetry.listening_pcbs++;
    return TCP_OK;
}

tcp_status_t tcp_connect(tcp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port)
{
    if (pcb == NULL || remote_ip == 0U || remote_port == 0U)
    {
        return TCP_ERR_ARG;
    }

    net_config_t cfg;
    net_get_config(&cfg);

    if (pcb->local_port == 0U)
    {
        pcb->local_port = 49152U; /* Ephemeral port */
    }
    pcb->local_ip    = cfg.ip;
    pcb->remote_ip   = remote_ip;
    pcb->remote_port = remote_port;

    /* Transition to SYN_SENT and dispatch initial SYN segment */
    pcb->state = TCP_STATE_SYN_SENT;
    tcp_send_segment(pcb, TCP_FLAG_SYN, NULL, 0U);
    pcb->snd_nxt++;
    pcb->last_activity_ms = tcp_time_ms();

    return TCP_OK;
}

/* ========================================================================= */
/* Data Transfer & Teardown APIs                                             */
/* ========================================================================= */

tcp_status_t tcp_write(tcp_pcb_t *pcb, const void *data, uint16_t len)
{
    if (pcb == NULL || pcb->state != TCP_STATE_ESTABLISHED)
    {
        return TCP_ERR_STATE;
    }

    if (data == NULL || len == 0U)
    {
        return TCP_OK;
    }

    uint8_t flags = TCP_FLAG_ACK | TCP_FLAG_PSH;
    tcp_status_t st = tcp_send_segment(pcb, flags, data, len);
    if (st == TCP_OK)
    {
        pcb->snd_nxt += len;
        pcb->last_activity_ms = tcp_time_ms();
    }
    return st;
}

tcp_status_t tcp_close(tcp_pcb_t *pcb)
{
    if (pcb == NULL)
    {
        return TCP_ERR_ARG;
    }

    if (pcb->state == TCP_STATE_LISTEN || pcb->state == TCP_STATE_CLOSED)
    {
        pcb->state = TCP_STATE_CLOSED;
        pcb->in_use = false;
        return TCP_OK;
    }

    if (pcb->state == TCP_STATE_ESTABLISHED)
    {
        pcb->state = TCP_STATE_FIN_WAIT_1;
        tcp_send_segment(pcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0U);
        pcb->snd_nxt++;
        return TCP_OK;
    }

    if (pcb->state == TCP_STATE_CLOSE_WAIT)
    {
        pcb->state = TCP_STATE_LAST_ACK;
        tcp_send_segment(pcb, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0U);
        pcb->snd_nxt++;
        return TCP_OK;
    }

    return TCP_OK;
}

tcp_status_t tcp_abort(tcp_pcb_t *pcb)
{
    if (pcb == NULL)
    {
        return TCP_ERR_ARG;
    }

    tcp_send_segment(pcb, TCP_FLAG_RST | TCP_FLAG_ACK, NULL, 0U);
    s_tcp_telemetry.rst_sent_count++;

    pcb->state  = TCP_STATE_CLOSED;
    pcb->in_use = false;
    return TCP_OK;
}

/* ========================================================================= */
/* Inbound Segment Processing (RFC 793 State Transitions)                    */
/* ========================================================================= */

tcp_status_t tcp_input(const uint8_t *ip_packet, uint16_t ip_len)
{
    if (ip_packet == NULL || ip_len < (IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN))
    {
        return TCP_ERR_ARG;
    }

    const ipv4_header_t *ip = (const ipv4_header_t *)ip_packet;
    uint8_t ihl = (ip->ver_ihl & 0x0FU) * 4U;
    if (ip_len < (ihl + TCP_MIN_HDR_LEN))
    {
        return TCP_ERR_ARG;
    }

    const tcp_header_t *tcp = (const tcp_header_t *)(ip_packet + ihl);
    uint8_t data_offset = ((tcp->data_offset_reserved >> TCP_DATA_OFFSET_SHIFT) & 0x0FU) * 4U;
    if (data_offset < TCP_MIN_HDR_LEN || ip_len < (ihl + data_offset))
    {
        return TCP_ERR_ARG;
    }

    uint32_t src_ip   = NET_NTOHL(ip->src_ip);
    uint32_t dest_ip  = NET_NTOHL(ip->dest_ip);
    uint16_t src_port = NET_NTOHS(tcp->src_port);
    uint16_t dest_port = NET_NTOHS(tcp->dest_port);
    uint32_t seq_num  = NET_NTOHL(tcp->seq_num);
    uint32_t ack_num  = NET_NTOHL(tcp->ack_num);
    uint8_t flags     = tcp->flags;

    uint16_t tcp_seg_len = (uint16_t)(ip_len - ihl);
    uint16_t payload_len = (uint16_t)(tcp_seg_len - data_offset);
    const uint8_t *payload = ip_packet + ihl + data_offset;

    /* Verify TCP Checksum */
    uint16_t chk = net_tcp_checksum(src_ip, dest_ip, tcp, data_offset, payload, payload_len);
    if (chk != 0U)
    {
        return TCP_ERR_ARG;
    }

    /* 1. Locate matching established/active PCB */
    tcp_pcb_t *match = NULL;
    tcp_pcb_t *listener = NULL;

    for (uint32_t i = 0; i < TCP_MAX_PCBS; i++)
    {
        if (!s_tcp_pcbs[i].in_use) continue;

        if (s_tcp_pcbs[i].state == TCP_STATE_LISTEN && s_tcp_pcbs[i].local_port == dest_port)
        {
            listener = &s_tcp_pcbs[i];
        }
        else if (s_tcp_pcbs[i].local_port == dest_port &&
                 s_tcp_pcbs[i].remote_port == src_port &&
                 s_tcp_pcbs[i].remote_ip == src_ip)
        {
            match = &s_tcp_pcbs[i];
            break;
        }
    }

    /* 2. Handle connection establishment on listening PCB */
    if (match == NULL && listener != NULL && (flags & TCP_FLAG_SYN) != 0U)
    {
        tcp_pcb_t *conn = tcp_new();
        if (conn == NULL)
        {
            return TCP_ERR_MEM;
        }

        conn->local_ip    = dest_ip;
        conn->local_port  = dest_port;
        conn->remote_ip   = src_ip;
        conn->remote_port = src_port;
        conn->rcv_nxt     = seq_num + 1U;
        conn->snd_nxt     = TCP_INITIAL_SEQ_NUM;
        conn->snd_una     = conn->snd_nxt;
        conn->rcv_wnd     = TCP_DEFAULT_WINDOW_BYTES;
        conn->snd_wnd     = NET_NTOHS(tcp->window);
        conn->state       = TCP_STATE_SYN_RECEIVED;
        conn->accept_cb   = listener->accept_cb;
        conn->recv_cb     = listener->recv_cb;

        s_tcp_telemetry.syn_received_count++;

        /* Send SYN + ACK */
        tcp_send_segment(conn, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0U);
        conn->snd_nxt++;
        return TCP_OK;
    }

    if (match == NULL)
    {
        return TCP_ERR_CONN;
    }

    match->last_activity_ms = tcp_time_ms();

    /* 3. Handle state transitions per RFC 793 */
    switch (match->state)
    {
        case TCP_STATE_SYN_SENT:
            if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK))
            {
                match->rcv_nxt = seq_num + 1U;
                match->snd_una = ack_num;
                match->state   = TCP_STATE_ESTABLISHED;
                s_tcp_telemetry.established_count++;
                s_tcp_telemetry.active_connections++;

                /* Send ACK completing 3-way handshake */
                tcp_send_segment(match, TCP_FLAG_ACK, NULL, 0U);
            }
            break;

        case TCP_STATE_SYN_RECEIVED:
            if ((flags & TCP_FLAG_ACK) != 0U)
            {
                match->snd_una = ack_num;
                match->state   = TCP_STATE_ESTABLISHED;
                s_tcp_telemetry.established_count++;
                s_tcp_telemetry.active_connections++;

                if (match->accept_cb != NULL)
                {
                    match->accept_cb(match->callback_arg, match);
                }
            }
            break;

        case TCP_STATE_ESTABLISHED:
            if (payload_len > 0U)
            {
                match->rcv_nxt += payload_len;
                s_tcp_telemetry.bytes_rx += payload_len;

                /* Dispatch to application callback */
                if (match->recv_cb != NULL)
                {
                    match->recv_cb(match->callback_arg, match, payload, payload_len);
                }

                /* Acknowledge received payload */
                tcp_send_segment(match, TCP_FLAG_ACK, NULL, 0U);
            }

            if ((flags & TCP_FLAG_FIN) != 0U)
            {
                match->rcv_nxt++;
                match->state = TCP_STATE_CLOSE_WAIT;
                tcp_send_segment(match, TCP_FLAG_ACK, NULL, 0U);
            }
            break;

        case TCP_STATE_FIN_WAIT_1:
            if ((flags & TCP_FLAG_ACK) != 0U)
            {
                match->state = TCP_STATE_FIN_WAIT_2;
            }
            if ((flags & TCP_FLAG_FIN) != 0U)
            {
                match->rcv_nxt++;
                tcp_send_segment(match, TCP_FLAG_ACK, NULL, 0U);
                match->state = TCP_STATE_TIME_WAIT;
            }
            break;

        case TCP_STATE_FIN_WAIT_2:
            if ((flags & TCP_FLAG_FIN) != 0U)
            {
                match->rcv_nxt++;
                tcp_send_segment(match, TCP_FLAG_ACK, NULL, 0U);
                match->state = TCP_STATE_CLOSED;
                match->in_use = false;
                if (s_tcp_telemetry.active_connections > 0U)
                {
                    s_tcp_telemetry.active_connections--;
                }
            }
            break;

        case TCP_STATE_LAST_ACK:
            if ((flags & TCP_FLAG_ACK) != 0U)
            {
                match->state = TCP_STATE_CLOSED;
                match->in_use = false;
                if (s_tcp_telemetry.active_connections > 0U)
                {
                    s_tcp_telemetry.active_connections--;
                }
            }
            break;

        default:
            break;
    }

    return TCP_OK;
}

void tcp_tick(void)
{
    /* Service periodic retransmission and connection timeouts */
    uint32_t now = tcp_time_ms();

    for (uint32_t i = 0; i < TCP_MAX_PCBS; i++)
    {
        if (!s_tcp_pcbs[i].in_use) continue;

        if (s_tcp_pcbs[i].state == TCP_STATE_TIME_WAIT)
        {
            if ((now - s_tcp_pcbs[i].last_activity_ms) >= 2000U)
            {
                s_tcp_pcbs[i].state  = TCP_STATE_CLOSED;
                s_tcp_pcbs[i].in_use = false;
                if (s_tcp_telemetry.active_connections > 0U)
                {
                    s_tcp_telemetry.active_connections--;
                }
            }
        }
    }
}

/* ========================================================================= */
/* Status & Telemetry Accessors                                              */
/* ========================================================================= */

tcp_status_t tcp_get_telemetry(tcp_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return TCP_ERR_ARG;
    }

    *out_telem = s_tcp_telemetry;
    return TCP_OK;
}

const tcp_pcb_t *tcp_get_pcb(uint32_t index)
{
    if (index >= TCP_MAX_PCBS)
    {
        return NULL;
    }
    return &s_tcp_pcbs[index];
}

const char *tcp_state_to_str(tcp_state_t state)
{
    switch (state)
    {
        case TCP_STATE_CLOSED:       return "CLOSED";
        case TCP_STATE_LISTEN:       return "LISTEN";
        case TCP_STATE_SYN_SENT:     return "SYN_SENT";
        case TCP_STATE_SYN_RECEIVED: return "SYN_RCVD";
        case TCP_STATE_ESTABLISHED:  return "ESTABLISHED";
        case TCP_STATE_FIN_WAIT_1:   return "FIN_WAIT_1";
        case TCP_STATE_FIN_WAIT_2:   return "FIN_WAIT_2";
        case TCP_STATE_CLOSE_WAIT:   return "CLOSE_WAIT";
        case TCP_STATE_CLOSING:      return "CLOSING";
        case TCP_STATE_LAST_ACK:     return "LAST_ACK";
        case TCP_STATE_TIME_WAIT:    return "TIME_WAIT";
        default:                     return "UNKNOWN";
    }
}
