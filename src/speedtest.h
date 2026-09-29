/*
 * src/speedtest.h
 *
 * Bare-Metal LAN Network Diagnostic & Wi-Fi Speed-Test Benchmark Engine
 * TRM Chapter 13 (SYSTIMER) & RFC 768 (UDP) Transport Benchmarking
 *
 * Implements deterministic synthetic throughput bursts, microsecond-accurate
 * SYSTIMER latency tracking, UDP packet injection/reflection, and bandwidth
 * calculation routines (kbps/Mbps) with zero dynamic memory allocation.
 */

#ifndef IRON_V_SPEEDTEST_H
#define IRON_V_SPEEDTEST_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"

/* ========================================================================= */
/* Protocol & Sizing Constants (Zero Magic Numbers)                          */
/* ========================================================================= */
#define SPEEDTEST_DEFAULT_PORT              5001U
#define SPEEDTEST_MAGIC_HEADER              0x53504544U /* 'SPED' */

#define SPEEDTEST_DEFAULT_BURST_COUNT       100U
#define SPEEDTEST_MIN_BURST_COUNT           1U
#define SPEEDTEST_MAX_BURST_COUNT           10000U

#define SPEEDTEST_DEFAULT_PACKET_SIZE       1024U
#define SPEEDTEST_MIN_PACKET_SIZE           64U
#define SPEEDTEST_MAX_PACKET_SIZE           1472U /* Max standard Ethernet UDP payload */

#define SPEEDTEST_PAYLOAD_PATTERN           0xAAU
#define SPEEDTEST_WDT_FEED_INTERVAL         20U

/* Timing & Conversion Arithmetic Factors */
#define SPEEDTEST_BITS_PER_BYTE             8ULL
#define SPEEDTEST_US_PER_SECOND             1000000ULL
#define SPEEDTEST_KBPS_FACTOR               1000ULL
#define SPEEDTEST_MBPS_FACTOR               1000000ULL
#define SPEEDTEST_KBPS_PER_MBPS             1000U

/* Wire Protocol Flag Bitfields */
#define SPEEDTEST_FLAG_BURST                (1U << 0)
#define SPEEDTEST_FLAG_ECHO_REQ             (1U << 1)
#define SPEEDTEST_FLAG_ECHO_RESP            (1U << 2)
#define SPEEDTEST_FLAG_END_STREAM           (1U << 3)

/* ========================================================================= */
/* Status & Error Codes                                                      */
/* ========================================================================= */
typedef enum {
    SPEEDTEST_OK                    = 0,
    SPEEDTEST_ERR_INVALID_PARAM     = -1,
    SPEEDTEST_ERR_BUFFER_TOO_SMALL  = -2,
    SPEEDTEST_ERR_NETWORK           = -3,
    SPEEDTEST_ERR_BUSY              = -4,
    SPEEDTEST_ERR_TIMEOUT           = -5
} speedtest_status_t;

/* ========================================================================= */
/* Wire Protocol & Data Structures                                           */
/* ========================================================================= */

/**
 * @brief Standard 16-Byte Wire Protocol Header for Benchmark Packets
 */
typedef struct {
    uint32_t magic;         /* SPEEDTEST_MAGIC_HEADER */
    uint32_t sequence;      /* Packet sequence number (0, 1, 2, ...) */
    uint32_t timestamp_us;  /* Hardware transmit timestamp in microseconds */
    uint16_t payload_len;   /* Total UDP packet payload length */
    uint16_t flags;         /* Control flags (BURST, ECHO_REQ, ECHO_RESP) */
} __attribute__((packed)) speedtest_packet_header_t;

/**
 * @brief Discrete Speed-Test Benchmark Result (per Roadmap §6.2)
 */
typedef struct {
    uint32_t total_bytes_transferred;
    uint32_t start_time_us;
    uint32_t end_time_us;
    uint32_t throughput_kbps;
    uint32_t latency_min_us;
    uint32_t latency_max_us;
    uint32_t packet_loss_count;
} speedtest_result_t;

/**
 * @brief Cumulative Speed-Test Engine Telemetry
 */
typedef struct {
    uint32_t bursts_run;
    uint32_t total_packets_tx;
    uint32_t total_packets_rx;
    uint32_t total_bytes_tx;
    uint32_t total_bytes_rx;
    uint32_t last_throughput_kbps;
    uint32_t last_throughput_mbps;
    uint32_t last_duration_us;
    uint32_t last_latency_min_us;
    uint32_t last_latency_max_us;
    uint32_t last_latency_avg_us;
    uint32_t last_packet_loss;
} speedtest_telemetry_t;

/* ========================================================================= */
/* Public API Function Prototypes                                            */
/* ========================================================================= */

/* Core Lifecycle */
speedtest_status_t speedtest_init(void);
void speedtest_reset(void);

/* Conversion & Calculation Engines */
uint32_t speedtest_calculate_throughput_kbps(uint32_t bytes, uint32_t elapsed_us);
uint32_t speedtest_calculate_throughput_mbps(uint32_t bytes, uint32_t elapsed_us);
uint32_t speedtest_kbps_to_mbps(uint32_t kbps);

/* Benchmark Execution */
speedtest_status_t speedtest_run_synthetic_burst(uint32_t packet_count,
                                                 uint32_t packet_size,
                                                 speedtest_result_t *out_result);

speedtest_status_t speedtest_run_udp_tx(uint32_t target_ip,
                                        uint16_t target_port,
                                        uint32_t packet_count,
                                        uint32_t packet_size,
                                        speedtest_result_t *out_result);

/* Inbound Packet Processing */
speedtest_status_t speedtest_process_udp_packet(const uint8_t *frame,
                                                const uint8_t *payload,
                                                uint16_t payload_len);

/* Telemetry & Results */
speedtest_status_t speedtest_get_last_result(speedtest_result_t *out_result);
speedtest_status_t speedtest_get_telemetry(speedtest_telemetry_t *out_telemetry);

#endif /* IRON_V_SPEEDTEST_H */
