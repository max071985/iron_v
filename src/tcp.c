/*
 * src/tcp.c
 *
 * Lightweight Bare-Metal TCP State Machine & Connection Engine
 * RFC 793 (Transmission Control Protocol), RFC 6298 (retransmission timer)
 *
 * Implements deterministic static Protocol Control Blocks (PCBs), connection state
 * transitions (LISTEN, SYN_SENT, SYN_RECEIVED, ESTABLISHED, FIN_WAIT, CLOSED),
 * sequence number arithmetic, window management, and zero-allocation socket APIs.
 *
 * Reliability (REV-13): unacknowledged data stays in a send buffer (chunks of a shared pool) and is
 * retransmitted with an RTT-estimated, exponentially backed-off timeout; SYN, SYN-ACK and FIN are
 * retransmitted too. Received data is accepted in order only; anything else is re-ACKed and dropped,
 * so a peer's retransmission is never delivered twice. Each PCB has its own idle policy.
 */

#include "tcp.h"
#include "net.h"
#include "wifi.h"
#include "string.h"

_Static_assert(ETH_HDR_LEN + IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN + TCP_DEFAULT_SEGMENT_MSS <= NET_MAX_FRAME_SIZE,
               "one MSS segment must fit a frame");
_Static_assert(TCP_SNDBUF_CHUNKS < TCP_SNDBUF_NO_CHUNK, "chunk indices fit uint8_t");
_Static_assert(TCP_SNDBUF_PCB_MAX_BYTES <= UINT16_MAX, "sndbuf_len is uint16_t");

#if defined(__riscv)
#include "systimer.h"
#include "console.h"
#include "utils.h"
static inline uint32_t tcp_time_ms(void)
{
    return (uint32_t)systimer_get_ms();
}
#else
static uint32_t s_tcp_host_now_ms = 0U;
static inline uint32_t tcp_time_ms(void)
{
    return s_tcp_host_now_ms;
}

void tcp_host_set_time_ms(uint32_t now_ms)
{
    s_tcp_host_now_ms = now_ms;
}
#endif

/* Sequence-space and clock comparisons modulo 2^32 */
#define TCP_SEQ_LT(a, b)        ((int32_t)((uint32_t)(a) - (uint32_t)(b)) < 0)
#define TCP_SEQ_LEQ(a, b)       ((int32_t)((uint32_t)(a) - (uint32_t)(b)) <= 0)
#define TCP_SEQ_GT(a, b)        TCP_SEQ_LT(b, a)
#define TCP_SEQ_GEQ(a, b)       TCP_SEQ_LEQ(b, a)
#define TCP_TIME_REACHED(now, deadline) ((int32_t)((uint32_t)(now) - (uint32_t)(deadline)) >= 0)
#define TCP_MIN(a, b)           (((a) < (b)) ? (a) : (b))

/* ========================================================================= */
/* Static Storage & Connection State (Zero Dynamic Heap Allocation)          */
/* ========================================================================= */
static tcp_pcb_t s_tcp_pcbs[TCP_MAX_PCBS];
static tcp_telemetry_t s_tcp_telemetry;
static bool s_tcp_initialized = false;
static uint16_t s_tcp_next_port = TCP_EPHEMERAL_PORT_MIN;
/* Inside tcp_input's callbacks: writes/closes are flushed once the callback returns, so a response, its FIN
 * and the ACK of the request leave in one frame (HTTP: write + close inside recv_cb) */
static bool s_tcp_in_callback = false;

static uint8_t s_sndbuf_pool[TCP_SNDBUF_CHUNKS][TCP_SNDBUF_CHUNK_SIZE];
static bool s_sndbuf_used[TCP_SNDBUF_CHUNKS];

/* ========================================================================= */
/* Send Buffer (chunk chain per PCB)                                         */
/* ========================================================================= */

static uint8_t tcp_sndbuf_chunk_alloc(void)
{
    for (uint32_t i = 0U; i < TCP_SNDBUF_CHUNKS; i++)
    {
        if (!s_sndbuf_used[i])
        {
            s_sndbuf_used[i] = true;
            return (uint8_t)i;
        }
    }
    return TCP_SNDBUF_NO_CHUNK;
}

static void tcp_sndbuf_release_all(tcp_pcb_t *pcb)
{
    for (uint32_t k = 0U; k < pcb->sndbuf_chunk_count; k++)
    {
        s_sndbuf_used[pcb->sndbuf_chunk[k]] = false;
    }
    pcb->sndbuf_chunk_count = 0U;
    pcb->sndbuf_head = 0U;
    pcb->sndbuf_len = 0U;
}

uint32_t tcp_sndbuf_free_chunks(void)
{
    uint32_t n = 0U;
    for (uint32_t i = 0U; i < TCP_SNDBUF_CHUNKS; i++)
    {
        if (!s_sndbuf_used[i])
        {
            n++;
        }
    }
    return n;
}

uint32_t tcp_sndbuf_space(const tcp_pcb_t *pcb)
{
    if (pcb == NULL)
    {
        return 0U;
    }
    uint32_t chain_end = (uint32_t)pcb->sndbuf_head + pcb->sndbuf_len;
    uint32_t tail_room = (uint32_t)pcb->sndbuf_chunk_count * TCP_SNDBUF_CHUNK_SIZE - chain_end;
    uint32_t more_chunks = TCP_MIN(tcp_sndbuf_free_chunks(),
                                   TCP_SNDBUF_PCB_MAX_CHUNKS - (uint32_t)pcb->sndbuf_chunk_count);
    uint32_t room = tail_room + more_chunks * TCP_SNDBUF_CHUNK_SIZE;
    return TCP_MIN(room, TCP_SNDBUF_PCB_MAX_BYTES - (uint32_t)pcb->sndbuf_len);
}

/* Caller checked tcp_sndbuf_space() */
static void tcp_sndbuf_append(tcp_pcb_t *pcb, const uint8_t *data, uint32_t len)
{
    uint32_t off = (uint32_t)pcb->sndbuf_head + pcb->sndbuf_len;
    while (len > 0U)
    {
        uint32_t idx = off / TCP_SNDBUF_CHUNK_SIZE;
        if (idx >= pcb->sndbuf_chunk_count)
        {
            pcb->sndbuf_chunk[pcb->sndbuf_chunk_count++] = tcp_sndbuf_chunk_alloc();
        }
        uint32_t in = off % TCP_SNDBUF_CHUNK_SIZE;
        uint32_t n = TCP_MIN(TCP_SNDBUF_CHUNK_SIZE - in, len);
        memcpy(&s_sndbuf_pool[pcb->sndbuf_chunk[idx]][in], data, n);
        data += n;
        off += n;
        len -= n;
        pcb->sndbuf_len = (uint16_t)(pcb->sndbuf_len + n);
    }
}

/* Copies len buffered bytes starting buf_off bytes after snd_una */
static void tcp_sndbuf_copy_out(const tcp_pcb_t *pcb, uint32_t buf_off, uint8_t *dst, uint32_t len)
{
    uint32_t off = (uint32_t)pcb->sndbuf_head + buf_off;
    while (len > 0U)
    {
        uint32_t in = off % TCP_SNDBUF_CHUNK_SIZE;
        uint32_t n = TCP_MIN(TCP_SNDBUF_CHUNK_SIZE - in, len);
        memcpy(dst, &s_sndbuf_pool[pcb->sndbuf_chunk[off / TCP_SNDBUF_CHUNK_SIZE]][in], n);
        dst += n;
        off += n;
        len -= n;
    }
}

/* Acknowledged bytes leave the front of the buffer; emptied chunks go back to the pool */
static void tcp_sndbuf_drop_front(tcp_pcb_t *pcb, uint32_t n)
{
    pcb->sndbuf_head = (uint16_t)(pcb->sndbuf_head + n);
    pcb->sndbuf_len = (uint16_t)(pcb->sndbuf_len - n);
    if (pcb->sndbuf_len == 0U)
    {
        tcp_sndbuf_release_all(pcb);
        return;
    }
    while (pcb->sndbuf_head >= TCP_SNDBUF_CHUNK_SIZE && pcb->sndbuf_chunk_count > 0U)
    {
        s_sndbuf_used[pcb->sndbuf_chunk[0]] = false;
        memmove(&pcb->sndbuf_chunk[0], &pcb->sndbuf_chunk[1], (size_t)pcb->sndbuf_chunk_count - 1U);
        pcb->sndbuf_chunk_count--;
        pcb->sndbuf_head = (uint16_t)(pcb->sndbuf_head - TCP_SNDBUF_CHUNK_SIZE);
    }
}

/* ========================================================================= */
/* Segment Transmission                                                      */
/* ========================================================================= */

/* Formats and transmits one segment over Ethernet + IPv4; payload_len bytes come from the send buffer,
 * buf_off bytes after snd_una */
static tcp_status_t tcp_send_segment(tcp_pcb_t *pcb, uint8_t flags, uint32_t seq,
                                     uint32_t buf_off, uint16_t payload_len)
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
            memset(dest_mac, TCP_ETH_BROADCAST_OCTET, ETH_ADDR_LEN);
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
    ip->identification    = (uint16_t)seq;
    ip->flags_frag_offset = NET_HTONS(IPV4_FLAGS_DF);
    ip->ttl               = IPV4_TTL_DEFAULT;
    ip->protocol          = IPV4_PROTO_TCP;
    ip->src_ip            = NET_HTONL(net_cfg.ip);
    ip->dest_ip           = NET_HTONL(pcb->remote_ip);
    ip->checksum          = 0U;
    ip->checksum          = NET_HTONS(net_ipv4_checksum(ip));

    /* TCP Header */
    tcp->src_port              = NET_HTONS(pcb->local_port);
    tcp->dest_port             = NET_HTONS(pcb->remote_port);
    tcp->seq_num               = NET_HTONL(seq);
    tcp->ack_num               = NET_HTONL(pcb->rcv_nxt);
    tcp->data_offset_reserved  = (uint8_t)((TCP_MIN_HDR_LEN / TCP_HDR_WORD_BYTES) << TCP_DATA_OFFSET_SHIFT);
    tcp->flags                 = flags;
    tcp->window                = NET_HTONS(pcb->rcv_wnd);
    tcp->checksum              = 0U;
    tcp->urgent_ptr            = 0U;

    if (payload_len > 0U)
    {
        tcp_sndbuf_copy_out(pcb, buf_off, data_dst, payload_len);
    }

    tcp->checksum = NET_HTONS(net_tcp_checksum(net_cfg.ip, pcb->remote_ip, tcp, TCP_MIN_HDR_LEN,
                                               data_dst, payload_len));

    if ((flags & TCP_FLAG_ACK) != 0U)
    {
        pcb->ack_pending = false;
    }

    wifi_status_t wst = wifi_tx_packet(wifi_get_ip_tx_if(), frame, total_frame_len);
    if (wst == WIFI_OK)
    {
        s_tcp_telemetry.bytes_tx += payload_len;
        net_notify_tx_packet(total_frame_len, true);
        return TCP_OK;
    }

    return TCP_ERR_MEM;
}

/* Control segment without payload at snd_nxt */
static void tcp_send_ctl(tcp_pcb_t *pcb, uint8_t flags)
{
    (void)tcp_send_segment(pcb, flags, pcb->snd_nxt, 0U, 0U);
}

static void tcp_rtx_arm(tcp_pcb_t *pcb, uint32_t now)
{
    pcb->rtx_armed = true;
    pcb->rtx_deadline_ms = now + pcb->rto_ms;
}

static void tcp_note_sent(tcp_pcb_t *pcb, uint32_t seq_end)
{
    if (TCP_SEQ_GT(seq_end, pcb->snd_max))
    {
        pcb->snd_max = seq_end;
    }
}

/* RFC 6298 section 2: SRTT/RTTVAR from one sample, RTO clamped to [min, max] */
static void tcp_rtt_sample(tcp_pcb_t *pcb, uint32_t r)
{
    if (r == 0U)
    {
        r = 1U;   /* sub-millisecond LAN RTT; keeps srtt_ms == 0 meaning "no sample yet" */
    }
    if (pcb->srtt_ms == 0U)
    {
        pcb->srtt_ms = r;
        pcb->rttvar_ms = r >> TCP_RTT_FIRST_VAR_SHIFT;
    }
    else
    {
        uint32_t delta = (pcb->srtt_ms > r) ? (pcb->srtt_ms - r) : (r - pcb->srtt_ms);
        pcb->rttvar_ms = pcb->rttvar_ms - (pcb->rttvar_ms >> TCP_RTT_BETA_SHIFT) + (delta >> TCP_RTT_BETA_SHIFT);
        pcb->srtt_ms = pcb->srtt_ms - (pcb->srtt_ms >> TCP_RTT_ALPHA_SHIFT) + (r >> TCP_RTT_ALPHA_SHIFT);
    }
    uint32_t rto = pcb->srtt_ms + TCP_RTT_VAR_FACTOR * pcb->rttvar_ms;
    if (rto < TCP_RTO_MIN_MS)
    {
        rto = TCP_RTO_MIN_MS;
    }
    if (rto > TCP_RTO_MAX_MS)
    {
        rto = TCP_RTO_MAX_MS;
    }
    pcb->rto_ms = rto;
    s_tcp_telemetry.rtt_samples++;
}

/* The driver had no TX buffer: nothing left the radio, so this is not a loss (no backoff, no retry count);
 * tcp_tick sends again from snd_nxt shortly */
static bool tcp_tx_blocked(tcp_pcb_t *pcb, uint32_t now)
{
    if (pcb->tx_blocked_count >= TCP_TX_BLOCKED_MAX)
    {
        pcb->tx_blocked_count = 0U;
        return false;   /* still refused: treat as sent and lost, the RTO takes over */
    }
    pcb->tx_blocked_count++;
    s_tcp_telemetry.tx_blocked++;
    pcb->rtt_active = false;
    pcb->tx_blocked = true;
    pcb->rtx_armed = true;
    pcb->rtx_deadline_ms = now + TCP_TX_BLOCKED_RETRY_MS;
    return true;
}

static bool tcp_state_can_send_data(tcp_state_t st)
{
    return st == TCP_STATE_ESTABLISHED || st == TCP_STATE_CLOSE_WAIT || st == TCP_STATE_FIN_WAIT_1 ||
           st == TCP_STATE_CLOSING || st == TCP_STATE_LAST_ACK;
}

/* Sends buffered data from snd_nxt within min(peer window, cwnd), then the FIN if queued.
 * probe: zero window, send one byte anyway (persist). A TX error counts as a loss: the timer resends. */
static void tcp_output(tcp_pcb_t *pcb, bool probe)
{
    if (!tcp_state_can_send_data(pcb->state) || pcb->fin_sent || pcb->tx_blocked)
    {
        return;
    }
    uint32_t now = tcp_time_ms();

    for (;;)
    {
        uint32_t sent = pcb->snd_nxt - pcb->snd_una;
        if (sent >= pcb->sndbuf_len)
        {
            break;
        }
        uint32_t limit = TCP_MIN(pcb->snd_wnd, pcb->cwnd);
        if (probe && limit == 0U && sent == 0U)
        {
            limit = TCP_PERSIST_PROBE_BYTES;
        }
        if (sent >= limit)
        {
            if (!pcb->rtx_armed)
            {
                tcp_rtx_arm(pcb, now);   /* blocked by a zero window: probe on timeout */
            }
            return;
        }
        uint32_t seg = TCP_MIN(pcb->sndbuf_len - sent, (uint32_t)TCP_DEFAULT_SEGMENT_MSS);
        seg = TCP_MIN(seg, limit - sent);
        bool last = (sent + seg == pcb->sndbuf_len);
        uint8_t flags = TCP_FLAG_ACK;
        if (last)
        {
            flags |= TCP_FLAG_PSH;
            if (pcb->fin_queued)
            {
                flags |= TCP_FLAG_FIN;   /* piggybacked: one frame less on air */
            }
        }

        bool resend = TCP_SEQ_LT(pcb->snd_nxt, pcb->snd_max);
        if (resend)
        {
            s_tcp_telemetry.retransmit_count++;
        }
        else if (!pcb->rtt_active)
        {
            pcb->rtt_active = true;
            pcb->rtt_seq = pcb->snd_nxt + seg;
            pcb->rtt_start_ms = now;
        }
        if (tcp_send_segment(pcb, flags, pcb->snd_nxt, sent, (uint16_t)seg) != TCP_OK)
        {
            if (tcp_tx_blocked(pcb, now))
            {
                return;
            }
        }
        else
        {
            pcb->tx_blocked_count = 0U;
        }
        pcb->snd_nxt += seg;
        if ((flags & TCP_FLAG_FIN) != 0U)
        {
            pcb->snd_nxt++;
            pcb->fin_sent = true;
        }
        tcp_note_sent(pcb, pcb->snd_nxt);
        if (!pcb->rtx_armed)
        {
            tcp_rtx_arm(pcb, now);
        }
        if (pcb->fin_sent)
        {
            return;
        }
    }

    if (pcb->fin_queued && (pcb->snd_nxt - pcb->snd_una) == pcb->sndbuf_len)
    {
        if (TCP_SEQ_LT(pcb->snd_nxt, pcb->snd_max))
        {
            s_tcp_telemetry.retransmit_count++;
        }
        if (tcp_send_segment(pcb, TCP_FLAG_FIN | TCP_FLAG_ACK, pcb->snd_nxt, 0U, 0U) != TCP_OK)
        {
            if (tcp_tx_blocked(pcb, now))
            {
                return;
            }
        }
        else
        {
            pcb->tx_blocked_count = 0U;
        }
        pcb->snd_nxt++;
        pcb->fin_sent = true;
        tcp_note_sent(pcb, pcb->snd_nxt);
        if (!pcb->rtx_armed)
        {
            tcp_rtx_arm(pcb, now);
        }
    }
}

/* ========================================================================= */
/* PCB Lifecycle Helpers                                                     */
/* ========================================================================= */

static tcp_pcb_t *tcp_pcb_claim(uint32_t i, uint32_t now)
{
    tcp_pcb_t *pcb = &s_tcp_pcbs[i];
    tcp_sndbuf_release_all(pcb);
    memset(pcb, 0, sizeof(tcp_pcb_t));
    pcb->in_use           = true;
    pcb->state            = TCP_STATE_CLOSED;
    pcb->rcv_wnd          = TCP_DEFAULT_WINDOW_BYTES;
    pcb->snd_wnd          = TCP_DEFAULT_WINDOW_BYTES;
    pcb->snd_nxt          = TCP_INITIAL_SEQ_NUM + (i * TCP_ISN_SLOT_STRIDE);
    pcb->snd_una          = pcb->snd_nxt;
    pcb->snd_max          = pcb->snd_nxt;
    pcb->cwnd             = TCP_INITIAL_CWND_BYTES;
    pcb->rto_ms           = TCP_RTO_INITIAL_MS;
    pcb->idle_timeout_ms  = TCP_IDLE_TIMEOUT_DEFAULT_MS;
    pcb->last_activity_ms = now;
    return pcb;
}

/* Releases the PCB; err != TCP_OK tells the application why the stack dropped it */
static void tcp_pcb_free(tcp_pcb_t *pcb, tcp_status_t err)
{
    tcp_err_fn err_cb = pcb->err_cb;
    void *arg = pcb->callback_arg;

    tcp_sndbuf_release_all(pcb);
    if (pcb->counted_active && s_tcp_telemetry.active_connections > 0U)
    {
        s_tcp_telemetry.active_connections--;
    }
    if (pcb->state == TCP_STATE_LISTEN && s_tcp_telemetry.listening_pcbs > 0U)
    {
        s_tcp_telemetry.listening_pcbs--;
    }
    pcb->counted_active = false;
    pcb->rtx_armed = false;
    pcb->state = TCP_STATE_CLOSED;
    pcb->in_use = false;

    if (err != TCP_OK && err_cb != NULL)
    {
        err_cb(arg, err);
    }
}

static void tcp_set_established(tcp_pcb_t *pcb)
{
    pcb->state = TCP_STATE_ESTABLISHED;
    pcb->counted_active = true;
    pcb->retries = 0U;
    pcb->rtx_armed = false;
    s_tcp_telemetry.established_count++;
    s_tcp_telemetry.active_connections++;
}

static uint16_t tcp_next_ephemeral_port(void)
{
    for (uint32_t tries = 0U; tries <= (TCP_EPHEMERAL_PORT_MAX - TCP_EPHEMERAL_PORT_MIN); tries++)
    {
        uint16_t port = s_tcp_next_port;
        s_tcp_next_port = (port >= TCP_EPHEMERAL_PORT_MAX) ? (uint16_t)TCP_EPHEMERAL_PORT_MIN
                                                           : (uint16_t)(port + 1U);
        bool busy = false;
        for (uint32_t i = 0U; i < TCP_MAX_PCBS; i++)
        {
            if (s_tcp_pcbs[i].in_use && s_tcp_pcbs[i].local_port == port)
            {
                busy = true;
                break;
            }
        }
        if (!busy)
        {
            return port;
        }
    }
    return TCP_EPHEMERAL_PORT_MIN;
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
    memset(s_sndbuf_used, 0, sizeof(s_sndbuf_used));
    s_tcp_next_port = TCP_EPHEMERAL_PORT_MIN;

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

    uint32_t now = tcp_time_ms();

    /* 1. First look for an unused PCB slot */
    for (uint32_t i = 0; i < TCP_MAX_PCBS; i++)
    {
        if (!s_tcp_pcbs[i].in_use)
        {
            return tcp_pcb_claim(i, now);
        }
    }

    /* 2. If all slots in use, recycle closed or TIME_WAIT slots (RFC 1122 tw_recycle) */
    for (uint32_t i = 0; i < TCP_MAX_PCBS; i++)
    {
        if (s_tcp_pcbs[i].state == TCP_STATE_TIME_WAIT || s_tcp_pcbs[i].state == TCP_STATE_CLOSED)
        {
            tcp_pcb_free(&s_tcp_pcbs[i], TCP_OK);
            return tcp_pcb_claim(i, now);
        }
    }

    /* 3. Recycle stale non-listening connections; persistent ones are never taken */
    for (uint32_t i = 0; i < TCP_MAX_PCBS; i++)
    {
        tcp_pcb_t *p = &s_tcp_pcbs[i];
        if (p->state != TCP_STATE_LISTEN && p->idle_timeout_ms != TCP_IDLE_TIMEOUT_NEVER &&
            (now - p->last_activity_ms) >= TCP_STALE_RECYCLE_MS)
        {
            tcp_pcb_free(p, TCP_ERR_TIMEOUT);
            return tcp_pcb_claim(i, now);
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
    if (pcb->state != TCP_STATE_CLOSED)
    {
        return TCP_ERR_STATE;
    }

    net_config_t cfg;
    net_get_config(&cfg);

    if (pcb->local_port == 0U)
    {
        pcb->local_port = tcp_next_ephemeral_port();
    }
    pcb->local_ip    = cfg.ip;
    pcb->remote_ip   = remote_ip;
    pcb->remote_port = remote_port;

    /* Transition to SYN_SENT and dispatch the SYN; retransmitted by tcp_tick until answered */
    uint32_t now = tcp_time_ms();
    pcb->state = TCP_STATE_SYN_SENT;
    pcb->snd_una = pcb->snd_nxt;
    tcp_send_ctl(pcb, TCP_FLAG_SYN);
    pcb->snd_nxt++;
    tcp_note_sent(pcb, pcb->snd_nxt);
    pcb->rtt_active = true;
    pcb->rtt_seq = pcb->snd_nxt;
    pcb->rtt_start_ms = now;
    tcp_rtx_arm(pcb, now);
    pcb->last_activity_ms = now;

    return TCP_OK;
}

void tcp_set_idle_timeout(tcp_pcb_t *pcb, uint32_t idle_timeout_ms)
{
    if (pcb != NULL)
    {
        pcb->idle_timeout_ms = idle_timeout_ms;
    }
}

void tcp_set_arg(tcp_pcb_t *pcb, void *arg)
{
    if (pcb != NULL)
    {
        pcb->callback_arg = arg;
    }
}

void tcp_set_recv_cb(tcp_pcb_t *pcb, tcp_recv_fn recv_cb)
{
    if (pcb != NULL)
    {
        pcb->recv_cb = recv_cb;
    }
}

void tcp_set_err_cb(tcp_pcb_t *pcb, tcp_err_fn err_cb)
{
    if (pcb != NULL)
    {
        pcb->err_cb = err_cb;
    }
}

/* ========================================================================= */
/* Data Transfer & Teardown APIs                                             */
/* ========================================================================= */

tcp_status_t tcp_write(tcp_pcb_t *pcb, const void *data, uint16_t len)
{
    if (pcb == NULL || !pcb->in_use || pcb->fin_queued ||
        (pcb->state != TCP_STATE_ESTABLISHED && pcb->state != TCP_STATE_CLOSE_WAIT))
    {
        return TCP_ERR_STATE;
    }

    if (data == NULL || len == 0U)
    {
        return TCP_OK;
    }

    if ((uint32_t)len > tcp_sndbuf_space(pcb))
    {
        s_tcp_telemetry.sndbuf_full++;
        return TCP_ERR_MEM;
    }

    tcp_sndbuf_append(pcb, (const uint8_t *)data, len);
    pcb->last_activity_ms = tcp_time_ms();
    if (!s_tcp_in_callback)
    {
        tcp_output(pcb, false);
    }
    return TCP_OK;
}

tcp_status_t tcp_close(tcp_pcb_t *pcb)
{
    if (pcb == NULL)
    {
        return TCP_ERR_ARG;
    }

    switch (pcb->state)
    {
        case TCP_STATE_LISTEN:
        case TCP_STATE_CLOSED:
        case TCP_STATE_SYN_SENT:
        case TCP_STATE_SYN_RECEIVED:
            tcp_pcb_free(pcb, TCP_OK);
            return TCP_OK;

        case TCP_STATE_ESTABLISHED:
            pcb->state = TCP_STATE_FIN_WAIT_1;
            pcb->fin_queued = true;
            if (!s_tcp_in_callback)
            {
                tcp_output(pcb, false);
            }
            return TCP_OK;

        case TCP_STATE_CLOSE_WAIT:
            pcb->state = TCP_STATE_LAST_ACK;
            pcb->fin_queued = true;
            if (!s_tcp_in_callback)
            {
                tcp_output(pcb, false);
            }
            return TCP_OK;

        default:
            return TCP_OK;
    }
}

tcp_status_t tcp_abort(tcp_pcb_t *pcb)
{
    if (pcb == NULL)
    {
        return TCP_ERR_ARG;
    }

    if (pcb->state != TCP_STATE_CLOSED && pcb->state != TCP_STATE_LISTEN)
    {
        tcp_send_ctl(pcb, TCP_FLAG_RST | TCP_FLAG_ACK);
        s_tcp_telemetry.rst_sent_count++;
    }

    tcp_pcb_free(pcb, TCP_OK);
    return TCP_OK;
}

/* ========================================================================= */
/* Inbound Segment Processing (RFC 793 State Transitions)                    */
/* ========================================================================= */

/* Acceptable ACK: frees acknowledged data, updates RTT/timer/window. Returns true once our FIN is acked. */
static bool tcp_process_ack(tcp_pcb_t *pcb, uint32_t ack, uint16_t wnd, uint32_t now)
{
    if (TCP_SEQ_LT(ack, pcb->snd_una) || TCP_SEQ_GT(ack, pcb->snd_max))
    {
        return false;   /* old, or acknowledges something never sent */
    }
    pcb->snd_wnd = wnd;

    if (TCP_SEQ_GT(ack, pcb->snd_una))
    {
        uint32_t acked = ack - pcb->snd_una;
        bool fin_acked = pcb->fin_queued && acked > pcb->sndbuf_len;
        tcp_sndbuf_drop_front(pcb, TCP_MIN(acked, (uint32_t)pcb->sndbuf_len));
        pcb->snd_una = ack;
        if (TCP_SEQ_LT(pcb->snd_nxt, pcb->snd_una))
        {
            pcb->snd_nxt = pcb->snd_una;   /* originals arrived after a timeout rewound snd_nxt */
        }
        if (fin_acked)
        {
            pcb->fin_sent = true;
        }

        /* Karn: rtt_active is cleared by every timeout, so this sample never covers a resend */
        if (pcb->rtt_active && TCP_SEQ_GEQ(ack, pcb->rtt_seq))
        {
            pcb->rtt_active = false;
            tcp_rtt_sample(pcb, now - pcb->rtt_start_ms);
        }
        pcb->retries = 0U;
        pcb->cwnd = TCP_MIN(pcb->cwnd + TCP_DEFAULT_SEGMENT_MSS, (uint32_t)TCP_CWND_MAX_BYTES);
        if (pcb->tx_blocked)
        {
            /* keep the short local-TX retry timer */
        }
        else if (pcb->snd_una == pcb->snd_max)
        {
            pcb->rtx_armed = false;
        }
        else
        {
            tcp_rtx_arm(pcb, now);   /* RFC 6298 5.3 */
        }
    }

    return pcb->fin_queued && pcb->fin_sent && pcb->snd_una == pcb->snd_max;
}

/* In-order data and FIN. Anything not starting at rcv_nxt (after trimming an overlap) is re-ACKed and
 * dropped, so a retransmitted segment is never delivered twice. Returns true if a FIN was consumed. */
static bool tcp_receive(tcp_pcb_t *pcb, uint32_t seq, const uint8_t *payload, uint16_t payload_len,
                        uint8_t flags)
{
    bool fin = (flags & TCP_FLAG_FIN) != 0U;
    if (payload_len == 0U && !fin)
    {
        return false;
    }

    if (seq != pcb->rcv_nxt)
    {
        uint32_t seg_end = seq + payload_len;
        if (TCP_SEQ_LT(seq, pcb->rcv_nxt) && TCP_SEQ_GT(seg_end, pcb->rcv_nxt))
        {
            uint32_t skip = pcb->rcv_nxt - seq;
            payload += skip;
            payload_len = (uint16_t)(payload_len - skip);
        }
        else
        {
            s_tcp_telemetry.dup_segments++;
            pcb->ack_pending = true;
            return false;
        }
    }

    bool can_take_data = pcb->state == TCP_STATE_ESTABLISHED || pcb->state == TCP_STATE_FIN_WAIT_1 ||
                         pcb->state == TCP_STATE_FIN_WAIT_2;
    if (!can_take_data)
    {
        /* Peer already sent its FIN: nothing new can arrive; re-ACK */
        s_tcp_telemetry.dup_segments++;
        pcb->ack_pending = true;
        return false;
    }

    if (payload_len > 0U)
    {
        pcb->rcv_nxt += payload_len;
        s_tcp_telemetry.bytes_rx += payload_len;
        pcb->ack_pending = true;
        if (pcb->recv_cb != NULL)
        {
            s_tcp_in_callback = true;
            net_status_t rs = pcb->recv_cb(pcb->callback_arg, pcb, payload, payload_len);
            s_tcp_in_callback = false;
            if (rs == NET_ERR_BUSY && pcb->in_use)
            {
                /* Backpressure: un-take the segment (and a FIN riding on it); the peer retransmits */
                pcb->rcv_nxt -= payload_len;
                s_tcp_telemetry.bytes_rx -= payload_len;
                s_tcp_telemetry.rx_deferred++;
                pcb->ack_pending = false;
                return false;
            }
        }
    }

    if (fin && pcb->in_use)
    {
        pcb->rcv_nxt++;
        pcb->ack_pending = true;
        return true;
    }
    return false;
}

tcp_status_t tcp_input(const uint8_t *ip_packet, uint16_t ip_len)
{
    if (ip_packet == NULL || ip_len < (IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN))
    {
        return TCP_ERR_ARG;
    }

    const ipv4_header_t *ip = (const ipv4_header_t *)ip_packet;
    uint8_t ihl = (uint8_t)((ip->ver_ihl & TCP_HDR_NIBBLE_MASK) * TCP_HDR_WORD_BYTES);
    if (ip_len < (ihl + TCP_MIN_HDR_LEN))
    {
        return TCP_ERR_ARG;
    }

    /* Clamp ip_len to actual IPv4 total_len if smaller to strip carrier padding */
    uint16_t ip_total_len = NET_NTOHS(ip->total_len);
    if (ip_total_len >= (ihl + TCP_MIN_HDR_LEN) && ip_total_len <= ip_len)
    {
        ip_len = ip_total_len;
    }

    const tcp_header_t *tcp = (const tcp_header_t *)(ip_packet + ihl);
    uint8_t data_offset = (uint8_t)(((tcp->data_offset_reserved >> TCP_DATA_OFFSET_SHIFT) & TCP_HDR_NIBBLE_MASK) *
                                    TCP_HDR_WORD_BYTES);
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
    uint16_t window   = NET_NTOHS(tcp->window);
    uint8_t flags     = tcp->flags;

    uint16_t tcp_seg_len = (uint16_t)(ip_len - ihl);
    uint16_t payload_len = (uint16_t)(tcp_seg_len - data_offset);
    const uint8_t *payload = ip_packet + ihl + data_offset;

    /* Verify TCP Checksum */
    uint16_t chk = net_tcp_checksum(src_ip, dest_ip, tcp, data_offset, payload, payload_len);
    if (chk != 0U)
    {
#if defined(__riscv)
        console_puts("[TCP] BAD CHK: 0x");
        put_hex(chk);
        console_puts("\r\n");
#endif
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

#if defined(__riscv)
    if (dest_port == CONFIG_TCP_DEFAULT_HTTP_PORT || src_port == CONFIG_TCP_DEFAULT_HTTP_PORT)
    {
        console_puts("[TCP] in fl=0x");
        put_hex(flags);
        console_puts(" dp=");
        put_dec(dest_port);
        console_puts(" sp=");
        put_dec(src_port);
        console_puts(" m=");
        put_dec(match != NULL ? 1 : 0);
        console_puts(" st=");
        put_dec(match != NULL ? (uint32_t)match->state :
                (listener != NULL ? (uint32_t)listener->state : TCP_DEBUG_NO_PCB_STATE));
        console_puts(" pl=");
        put_dec(payload_len);
        console_puts("\r\n");
    }
#endif

    /* Handle reset flags */
    if ((flags & TCP_FLAG_RST) != 0U)
    {
        if (match != NULL && match->state != TCP_STATE_LISTEN)
        {
            tcp_pcb_free(match, TCP_ERR_RST);
        }
        return TCP_OK;
    }

    uint32_t now = tcp_time_ms();

    /* 2. Handle connection establishment on listening PCB */
    if (match == NULL && listener != NULL && (flags & TCP_FLAG_SYN) != 0U)
    {
        tcp_pcb_t *conn = tcp_new();
        if (conn == NULL)
        {
            return TCP_ERR_MEM;
        }

        conn->local_ip         = dest_ip;
        conn->local_port       = dest_port;
        conn->remote_ip        = src_ip;
        conn->remote_port      = src_port;
        conn->rcv_nxt          = seq_num + 1U;
        conn->snd_wnd          = window;
        conn->state            = TCP_STATE_SYN_RECEIVED;
        conn->accept_cb        = listener->accept_cb;
        conn->recv_cb          = listener->recv_cb;
        conn->err_cb           = listener->err_cb;
        conn->callback_arg     = listener->callback_arg;
        conn->idle_timeout_ms  = listener->idle_timeout_ms;
        conn->last_activity_ms = now;

        s_tcp_telemetry.syn_received_count++;

        /* Send SYN + ACK; retransmitted by tcp_tick until the handshake completes */
        tcp_send_ctl(conn, TCP_FLAG_SYN | TCP_FLAG_ACK);
        conn->snd_nxt++;
        tcp_note_sent(conn, conn->snd_nxt);
        conn->rtt_active = true;
        conn->rtt_seq = conn->snd_nxt;
        conn->rtt_start_ms = now;
        tcp_rtx_arm(conn, now);
        return TCP_OK;
    }

    if (match == NULL)
    {
        return TCP_ERR_CONN;
    }

    match->last_activity_ms = now;
    bool has_ack = (flags & TCP_FLAG_ACK) != 0U;

    /* 3. Handshake states */
    if (match->state == TCP_STATE_SYN_SENT)
    {
        if ((flags & TCP_FLAG_SYN) != 0U && has_ack && ack_num == match->snd_max)
        {
            match->rcv_nxt = seq_num + 1U;
            if (match->rtt_active)
            {
                match->rtt_active = false;
                tcp_rtt_sample(match, now - match->rtt_start_ms);
            }
            match->snd_una = ack_num;
            match->snd_wnd = window;
            tcp_set_established(match);
            tcp_send_ctl(match, TCP_FLAG_ACK);   /* completes the 3-way handshake */
        }
        return TCP_OK;
    }

    if (match->state == TCP_STATE_SYN_RECEIVED)
    {
        if ((flags & TCP_FLAG_SYN) != 0U)
        {
            /* Our SYN-ACK was lost: answer the repeated SYN with the same SYN-ACK */
            (void)tcp_send_segment(match, TCP_FLAG_SYN | TCP_FLAG_ACK, match->snd_una, 0U, 0U);
            s_tcp_telemetry.retransmit_count++;
            return TCP_OK;
        }
        if (!has_ack || ack_num != match->snd_max)
        {
            return TCP_OK;
        }
        if (match->rtt_active)
        {
            match->rtt_active = false;
            tcp_rtt_sample(match, now - match->rtt_start_ms);
        }
        match->snd_una = ack_num;
        match->snd_wnd = window;
        tcp_set_established(match);
        if (match->accept_cb != NULL)
        {
            s_tcp_in_callback = true;
            match->accept_cb(match->callback_arg, match);
            s_tcp_in_callback = false;
        }
        if (!match->in_use)
        {
            return TCP_OK;
        }
        /* the ACK may carry the request: fall through to data processing */
    }

    /* 4. Synchronized states */
    if (match->state == TCP_STATE_TIME_WAIT && (flags & TCP_FLAG_SYN) != 0U)
    {
        /* New connection from the same peer port: reuse the slot */
        uint32_t idx = (uint32_t)(match - s_tcp_pcbs);
        tcp_pcb_t saved = *match;
        tcp_pcb_free(match, TCP_OK);
        tcp_pcb_t *conn = tcp_pcb_claim(idx, now);
        conn->local_ip        = saved.local_ip;
        conn->local_port      = saved.local_port;
        conn->remote_ip       = saved.remote_ip;
        conn->remote_port     = saved.remote_port;
        conn->accept_cb       = saved.accept_cb;
        conn->recv_cb         = saved.recv_cb;
        conn->err_cb          = saved.err_cb;
        conn->callback_arg    = saved.callback_arg;
        conn->idle_timeout_ms = saved.idle_timeout_ms;
        conn->rcv_nxt         = seq_num + 1U;
        conn->snd_wnd         = window;
        conn->state           = TCP_STATE_SYN_RECEIVED;
        s_tcp_telemetry.syn_received_count++;
        tcp_send_ctl(conn, TCP_FLAG_SYN | TCP_FLAG_ACK);
        conn->snd_nxt++;
        tcp_note_sent(conn, conn->snd_nxt);
        tcp_rtx_arm(conn, now);
        return TCP_OK;
    }

    if (has_ack)
    {
        bool fin_acked = tcp_process_ack(match, ack_num, window, now);
        if (fin_acked)
        {
            if (match->state == TCP_STATE_FIN_WAIT_1)
            {
                match->state = TCP_STATE_FIN_WAIT_2;
            }
            else if (match->state == TCP_STATE_CLOSING)
            {
                match->state = TCP_STATE_TIME_WAIT;
            }
            else if (match->state == TCP_STATE_LAST_ACK)
            {
                tcp_pcb_free(match, TCP_OK);
                return TCP_OK;
            }
        }
    }

    if (tcp_receive(match, seq_num, payload, payload_len, flags))
    {
        /* Peer's FIN; recv_cb may have closed our side meanwhile */
        if (match->state == TCP_STATE_ESTABLISHED)
        {
            match->state = TCP_STATE_CLOSE_WAIT;
        }
        else if (match->state == TCP_STATE_FIN_WAIT_1)
        {
            bool our_fin_acked = match->fin_sent && match->snd_una == match->snd_max;
            match->state = our_fin_acked ? TCP_STATE_TIME_WAIT : TCP_STATE_CLOSING;
        }
        else if (match->state == TCP_STATE_FIN_WAIT_2)
        {
            match->state = TCP_STATE_TIME_WAIT;
        }
    }
    else if (match->in_use && match->state == TCP_STATE_TIME_WAIT && (flags & TCP_FLAG_FIN) != 0U)
    {
        match->ack_pending = true;   /* our last ACK was lost: re-ACK the FIN, TIME_WAIT restarts */
    }

    if (!match->in_use)
    {
        return TCP_OK;
    }
    tcp_output(match, false);
    if (match->ack_pending)
    {
        tcp_send_ctl(match, TCP_FLAG_ACK);
    }

    return TCP_OK;
}

/* ========================================================================= */
/* Timers                                                                    */
/* ========================================================================= */

static uint32_t tcp_retry_limit(tcp_state_t st)
{
    if (st == TCP_STATE_SYN_SENT)
    {
        return TCP_SYN_MAX_RETRIES;
    }
    if (st == TCP_STATE_SYN_RECEIVED)
    {
        return TCP_SYNACK_MAX_RETRIES;
    }
    return TCP_MAX_RETRIES;
}

/* Retransmission timeout: back off, resend the oldest unacknowledged segment, or give up */
static void tcp_on_rto(tcp_pcb_t *pcb, uint32_t now)
{
    pcb->rtx_armed = false;
    bool outstanding = pcb->snd_una != pcb->snd_max || pcb->sndbuf_len > 0U ||
                       (pcb->fin_queued && !pcb->fin_sent);
    if (!outstanding)
    {
        return;   /* nothing outstanding */
    }

    if ((uint32_t)pcb->retries + 1U > tcp_retry_limit(pcb->state))
    {
        s_tcp_telemetry.rto_giveups++;
        tcp_pcb_free(pcb, TCP_ERR_TIMEOUT);
        return;
    }
    pcb->retries++;
    pcb->rto_ms = TCP_MIN(pcb->rto_ms * TCP_RTO_BACKOFF_FACTOR, (uint32_t)TCP_RTO_MAX_MS);
    pcb->rtt_active = false;
    pcb->cwnd = TCP_LOSS_CWND_BYTES;

    if (pcb->state == TCP_STATE_SYN_SENT)
    {
        (void)tcp_send_segment(pcb, TCP_FLAG_SYN, pcb->snd_una, 0U, 0U);
        s_tcp_telemetry.retransmit_count++;
    }
    else if (pcb->state == TCP_STATE_SYN_RECEIVED)
    {
        (void)tcp_send_segment(pcb, TCP_FLAG_SYN | TCP_FLAG_ACK, pcb->snd_una, 0U, 0U);
        s_tcp_telemetry.retransmit_count++;
    }
    else
    {
        pcb->snd_nxt = pcb->snd_una;   /* go back: cwnd lets one segment out, ACKs open it again */
        if (pcb->fin_sent && pcb->snd_una != pcb->snd_max)
        {
            pcb->fin_sent = false;
        }
        tcp_output(pcb, pcb->snd_wnd == 0U);
    }
    tcp_rtx_arm(pcb, now);
}

void tcp_tick(void)
{
    uint32_t now = tcp_time_ms();

    for (uint32_t i = 0; i < TCP_MAX_PCBS; i++)
    {
        tcp_pcb_t *pcb = &s_tcp_pcbs[i];
        if (!pcb->in_use) continue;

        if (pcb->rtx_armed && TCP_TIME_REACHED(now, pcb->rtx_deadline_ms))
        {
            if (pcb->tx_blocked)
            {
                pcb->tx_blocked = false;
                pcb->rtx_armed = false;
                tcp_output(pcb, false);
                if (!pcb->rtx_armed && pcb->snd_una != pcb->snd_max)
                {
                    tcp_rtx_arm(pcb, now);   /* earlier segments still in flight */
                }
                continue;
            }
            tcp_on_rto(pcb, now);
            if (!pcb->in_use) continue;
        }
        if (!pcb->rtx_armed)
        {
            tcp_output(pcb, false);   /* data or FIN queued from another connection's callback */
        }

        /* Below: only connections with nothing in flight (in-flight ones end by retransmission) */
        if (pcb->rtx_armed)
        {
            continue;
        }
        uint32_t elapsed = now - pcb->last_activity_ms;

        switch (pcb->state)
        {
            case TCP_STATE_TIME_WAIT:
                if (elapsed >= TCP_TIME_WAIT_MS)
                {
                    tcp_pcb_free(pcb, TCP_OK);
                }
                break;

            case TCP_STATE_FIN_WAIT_2:
                if (elapsed >= TCP_FIN_WAIT_2_TIMEOUT_MS)
                {
                    tcp_pcb_free(pcb, TCP_OK);
                }
                break;

            case TCP_STATE_CLOSE_WAIT:
                if (elapsed >= TCP_CLOSE_WAIT_TIMEOUT_MS)
                {
                    tcp_pcb_free(pcb, TCP_ERR_TIMEOUT);
                }
                break;

            case TCP_STATE_ESTABLISHED:
                if (pcb->idle_timeout_ms != TCP_IDLE_TIMEOUT_NEVER && elapsed >= pcb->idle_timeout_ms)
                {
                    s_tcp_telemetry.idle_expired++;
                    tcp_pcb_free(pcb, TCP_ERR_TIMEOUT);
                }
                break;

            default:
                break;
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
