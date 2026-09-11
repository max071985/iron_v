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
#define TCP_MAX_PCBS                     4U
#define TCP_RX_BUF_SIZE                  512U
#define TCP_TX_BUF_SIZE                  512U

#define TCP_DEFAULT_WINDOW_BYTES         CONFIG_TCP_DEFAULT_WINDOW
#define TCP_DEFAULT_SEGMENT_MSS          CONFIG_TCP_DEFAULT_MSS
#define TCP_RETRANSMIT_TIMEOUT_MS        CONFIG_TCP_RETRANSMIT_TIMEOUT_MS
#define TCP_MAX_RETRIES                  CONFIG_TCP_MAX_RETRIES
#define TCP_INITIAL_SEQ_NUM              0x10000000U

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

    /* Static ring buffers (Zero Heap Allocation) */
    uint8_t rx_buf[TCP_RX_BUF_SIZE];
    uint16_t rx_len;
    uint8_t tx_buf[TCP_TX_BUF_SIZE];
    uint16_t tx_len;

    /* Timers & Counters */
    uint32_t last_activity_ms;
    uint32_t retransmit_ms;
    uint8_t retries;

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

/* Data Transfer & Teardown */
tcp_status_t tcp_write(tcp_pcb_t *pcb, const void *data, uint16_t len);
tcp_status_t tcp_close(tcp_pcb_t *pcb);
tcp_status_t tcp_abort(tcp_pcb_t *pcb);

/* Segment Input Processing & Timer Tick */
tcp_status_t tcp_input(const uint8_t *ip_packet, uint16_t ip_len);
void tcp_tick(void);

/* Status & Telemetry Accessors */
tcp_status_t tcp_get_telemetry(tcp_telemetry_t *out_telem);
const tcp_pcb_t *tcp_get_pcb(uint32_t index);
const char *tcp_state_to_str(tcp_state_t state);

#endif /* IRON_V_TCP_H */
