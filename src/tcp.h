/*
 * src/tcp.h
 *
 * Lightweight Bare-Metal TCP State Machine & Connection Engine
 * RFC 793 (Transmission Control Protocol)
 *
 * Implements deterministic static Protocol Control Blocks (PCBs), connection state
 * transitions (LISTEN, SYN_SENT, SYN_RECEIVED, ESTABLISHED, FIN_WAIT, CLOSED),
 * sequence number arithmetic, window management, and zero-allocation socket APIs.
 */

#ifndef IRON_V_TCP_H
#define IRON_V_TCP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "net.h"

/* ========================================================================= */
/* TCP Configuration Constants (Zero Dynamic Heap Allocation)                */
/* ========================================================================= */
#define TCP_MAX_PCBS                     8U

#define TCP_DEFAULT_WINDOW_BYTES         CONFIG_TCP_DEFAULT_WINDOW
#define TCP_DEFAULT_SEGMENT_MSS          CONFIG_TCP_DEFAULT_MSS
#define TCP_HDR_WORD_BYTES               4U            /* data offset / IHL unit */
#define TCP_HDR_NIBBLE_MASK              0x0FU
#define TCP_EPHEMERAL_PORT_MIN           49152U        /* RFC 6335 dynamic range */
#define TCP_EPHEMERAL_PORT_MAX           65535U
#define TCP_EPHEMERAL_PORT_COUNT         (TCP_EPHEMERAL_PORT_MAX - TCP_EPHEMERAL_PORT_MIN + 1U)
#define TCP_ETH_BROADCAST_OCTET          0xFFU         /* no ARP entry for peer or gateway */
#define TCP_DEBUG_NO_PCB_STATE           99U           /* debug log: segment matched no PCB */

/* Send buffer (REV-13): unacknowledged data is kept for retransmission in chunks of a shared pool, so a
 * 4 KB HTTP response and a small persistent backlog fit together without a 4 KB buffer per PCB */
#define TCP_SNDBUF_CHUNK_SIZE            256U
#define TCP_SNDBUF_CHUNKS                24U           /* pool: 6 KB */
#define TCP_SNDBUF_PCB_MAX_BYTES         4096U         /* one PCB's backlog (HTTP_RESPONSE_BUF_SIZE) */
#define TCP_SNDBUF_PCB_MAX_CHUNKS        ((TCP_SNDBUF_PCB_MAX_BYTES / TCP_SNDBUF_CHUNK_SIZE) + 1U)
#define TCP_SNDBUF_NO_CHUNK              0xFFU

/* Retransmission (RFC 6298, Karn's rule) */
#define TCP_RTO_INITIAL_MS               CONFIG_TCP_RETRANSMIT_TIMEOUT_MS
#define TCP_RTO_MIN_MS                   1000U         /* RFC 6298 2.4: fewer spurious resends on air */
#define TCP_RTO_MAX_MS                   60000U
#define TCP_RTO_BACKOFF_FACTOR           2U
#define TCP_RTT_ALPHA_SHIFT              3U            /* SRTT gain 1/8 */
#define TCP_RTT_BETA_SHIFT               2U            /* RTTVAR gain 1/4 */
#define TCP_RTT_VAR_FACTOR               4U            /* RTO = SRTT + K * RTTVAR */
#define TCP_RTT_FIRST_VAR_SHIFT          1U            /* first sample: RTTVAR = R / 2 */
#define TCP_MAX_RETRIES                  CONFIG_TCP_MAX_RETRIES  /* data / FIN */
#define TCP_SYN_MAX_RETRIES              5U            /* client SYN: ~1 min */
#define TCP_SYNACK_MAX_RETRIES           3U            /* server SYN-ACK: ~15 s, then the slot is freed */
/* Congestion window: RFC 3390 initial window; after a timeout one segment, growing by one MSS per ACK */
#define TCP_INITIAL_CWND_BYTES           4380U
#define TCP_LOSS_CWND_BYTES              TCP_DEFAULT_SEGMENT_MSS
#define TCP_CWND_MAX_BYTES               TCP_SNDBUF_PCB_MAX_BYTES  /* never more in flight than buffered */
#define TCP_PERSIST_PROBE_BYTES          1U            /* zero window: one byte per timeout */
#define TCP_TX_BLOCKED_RETRY_MS          20U           /* driver out of TX buffers: retry soon, no backoff */
#define TCP_TX_BLOCKED_MAX               10U           /* then the frame counts as lost (interface down) */

/* Timeouts of connections with nothing in flight */
#define TCP_IDLE_TIMEOUT_DEFAULT_MS      30000U        /* ESTABLISHED without traffic (HTTP) */
#define TCP_IDLE_TIMEOUT_NEVER           0U            /* persistent (MQTT keeps it alive itself) */
#define TCP_TIME_WAIT_MS                 1000U         /* short: PCB slots are scarce; re-ACKs a late FIN */
#define TCP_FIN_WAIT_2_TIMEOUT_MS        5000U         /* peer never sends its FIN */
#define TCP_CLOSE_WAIT_TIMEOUT_MS        5000U         /* application never closes */
#define TCP_STALE_RECYCLE_MS             5000U         /* tcp_new() may take an idle slot after this */

/* ========================================================================= */
/* TCP State & Status Enumerations                                           */
/* ========================================================================= */
typedef enum {
    TCP_STATE_CLOSED = 0,
    TCP_STATE_LISTEN,
    TCP_STATE_SYN_SENT,
    TCP_STATE_SYN_RECEIVED,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT_1,
    TCP_STATE_FIN_WAIT_2,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSING,
    TCP_STATE_LAST_ACK,
    TCP_STATE_TIME_WAIT
} tcp_state_t;

typedef enum {
    TCP_OK = 0,
    TCP_ERR_MEM = -1,
    TCP_ERR_CONN = -2,
    TCP_ERR_STATE = -3,
    TCP_ERR_ARG = -4,
    TCP_ERR_TIMEOUT = -5,
    TCP_ERR_RST = -6
} tcp_status_t;

/* Forward declaration */
struct tcp_pcb;

/* Callback function pointer signatures */
/* Returning NET_ERR_BUSY (no tcp_write/tcp_close in that call) leaves the segment unacknowledged:
 * the peer retransmits it later (backpressure while the send buffer is full) */
typedef net_status_t (*tcp_recv_fn)(void *arg, struct tcp_pcb *pcb, const uint8_t *data, uint16_t len);
typedef net_status_t (*tcp_accept_fn)(void *arg, struct tcp_pcb *newpcb);
typedef void (*tcp_err_fn)(void *arg, tcp_status_t err);

/* ========================================================================= */
/* Protocol Control Block (PCB) Structure                                    */
/* ========================================================================= */
typedef struct tcp_pcb {
    tcp_state_t state;
    bool in_use;

    /* Addressing tuple */
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;

    /* Send sequence space */
    uint32_t snd_una;     /* Oldest unacknowledged sequence number */
    uint32_t snd_nxt;     /* Next sequence number to be sent */
    uint32_t snd_wnd;     /* Send window */
    uint32_t snd_wl1;     /* Sequence number used for last window update */
    uint32_t snd_wl2;     /* Acknowledgment number used for last window update */

    /* Receive sequence space */
    uint32_t rcv_nxt;     /* Next sequence number expected on incoming segments */
    uint32_t rcv_wnd;     /* Receive window advertised to peer */

    uint32_t snd_max;     /* Highest sequence number sent (snd_nxt rewinds on a timeout) */
    uint32_t cwnd;        /* Congestion window (bytes) */

    /* Send buffer: chunks of the shared pool holding [snd_una, snd_una + sndbuf_len) */
    uint8_t  sndbuf_chunk[TCP_SNDBUF_PCB_MAX_CHUNKS];
    uint8_t  sndbuf_chunk_count;
    uint16_t sndbuf_head;     /* offset of snd_una's byte in the first chunk */
    uint16_t sndbuf_len;      /* bytes buffered (sent and unacknowledged, then unsent) */
    bool fin_queued;          /* tcp_close() asked for a FIN after the buffered data */
    bool fin_sent;            /* the FIN occupies snd_nxt - 1 */
    bool counted_active;      /* included in telemetry active_connections */
    bool ack_pending;         /* received data/FIN not yet acknowledged (any outgoing segment carries it) */

    /* Timers & Counters */
    uint32_t last_activity_ms;
    uint32_t idle_timeout_ms;     /* TCP_IDLE_TIMEOUT_NEVER or ms without traffic before the PCB is dropped */
    uint32_t rto_ms;
    uint32_t srtt_ms;             /* 0 until the first RTT sample */
    uint32_t rttvar_ms;
    uint32_t rtx_deadline_ms;
    bool rtx_armed;
    bool tx_blocked;              /* the timer is a short local-TX retry, not an RTO */
    uint8_t tx_blocked_count;     /* consecutive refused sends */
    bool rtt_active;              /* timing one segment (Karn: cancelled by any retransmission) */
    uint32_t rtt_seq;             /* sample taken when this sequence number is acknowledged */
    uint32_t rtt_start_ms;
    uint8_t retries;              /* retransmissions of the oldest unacknowledged segment */

    /* Application callback hooks */
    void *callback_arg;
    tcp_recv_fn recv_cb;
    tcp_accept_fn accept_cb;
    tcp_err_fn err_cb;
} tcp_pcb_t;

/* ========================================================================= */
/* TCP Subsystem Telemetry Structure                                         */
/* ========================================================================= */
typedef struct {
    uint32_t active_connections;
    uint32_t listening_pcbs;
    uint32_t syn_received_count;
    uint32_t established_count;
    uint32_t bytes_tx;
    uint32_t bytes_rx;
    uint32_t retransmit_count;
    uint32_t rst_sent_count;
    uint32_t rto_giveups;          /* connections dropped after the last retransmission */
    uint32_t idle_expired;         /* connections dropped by their idle timeout */
    uint32_t dup_segments;         /* old/duplicate/out-of-order segments re-ACKed, not delivered */
    uint32_t sndbuf_full;          /* tcp_write refused: send buffer pool or PCB cap full */
    uint32_t rtt_samples;
    uint32_t tx_blocked;           /* segments the driver refused (no TX buffer), resent shortly */
    uint32_t rx_deferred;          /* segments the application refused (NET_ERR_BUSY), left unacknowledged */
    uint32_t last_isn;             /* ISN of the newest connection (HW RNG, REV-20) */
} tcp_telemetry_t;

/* ========================================================================= */
/* Public TCP Driver APIs                                                    */
/* ========================================================================= */

/* Core lifecycle initialization */
tcp_status_t tcp_init(void);

/* PCB Allocation & Binding */
tcp_pcb_t *tcp_new(void);
tcp_status_t tcp_bind(tcp_pcb_t *pcb, uint16_t port);
tcp_status_t tcp_listen(tcp_pcb_t *pcb, tcp_accept_fn accept_cb);
tcp_status_t tcp_connect(tcp_pcb_t *pcb, uint32_t remote_ip, uint16_t remote_port);

/* Per-connection policy & callbacks */
void tcp_set_idle_timeout(tcp_pcb_t *pcb, uint32_t idle_timeout_ms);
void tcp_set_arg(tcp_pcb_t *pcb, void *arg);
void tcp_set_recv_cb(tcp_pcb_t *pcb, tcp_recv_fn recv_cb);
/* Called when the stack drops the connection (TCP_ERR_RST, TCP_ERR_TIMEOUT); the PCB is already free */
void tcp_set_err_cb(tcp_pcb_t *pcb, tcp_err_fn err_cb);

/* Data Transfer & Teardown.
 * tcp_write copies all of data into the send buffer (or nothing: TCP_ERR_MEM) and sends what the window
 * allows; the rest goes out as ACKs arrive. Lost segments are retransmitted until acknowledged. */
tcp_status_t tcp_write(tcp_pcb_t *pcb, const void *data, uint16_t len);
/* Bytes tcp_write would accept now */
uint32_t tcp_sndbuf_space(const tcp_pcb_t *pcb);
tcp_status_t tcp_close(tcp_pcb_t *pcb);
tcp_status_t tcp_abort(tcp_pcb_t *pcb);

/* Segment Input Processing & Timer Tick */
tcp_status_t tcp_input(const uint8_t *ip_packet, uint16_t ip_len);
void tcp_tick(void);

/* Status & Telemetry Accessors */
tcp_status_t tcp_get_telemetry(tcp_telemetry_t *out_telem);
const tcp_pcb_t *tcp_get_pcb(uint32_t index);
const char *tcp_state_to_str(tcp_state_t state);
uint32_t tcp_sndbuf_free_chunks(void);
/* REV-32 RAM budget: chunks in use now and at most since boot or the last reset */
void tcp_sndbuf_usage(uint32_t *out_in_use, uint32_t *out_peak);
void tcp_sndbuf_peak_reset(void);

#if !defined(__riscv)
/* Host tests: the TCP clock only moves when told to */
void tcp_host_set_time_ms(uint32_t now_ms);
#endif

#endif /* IRON_V_TCP_H */
