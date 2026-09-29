/*
 * src/speedtest.c
 *
 * Bare-Metal LAN Network Diagnostic & Wi-Fi Speed-Test Benchmark Engine
 * TRM Chapter 13 (SYSTIMER) & RFC 768 (UDP) Transport Benchmarking
 *
 * Implements deterministic synthetic throughput bursts, microsecond-accurate
 * SYSTIMER latency tracking, UDP packet injection/reflection, and bandwidth
 * calculation routines (kbps/Mbps) with zero dynamic memory allocation.
 */

#include "speedtest.h"
#include "net.h"
#include "string.h"

#if defined(__riscv)
#include "systimer.h"
#include "wdt.h"
static inline uint64_t speedtest_get_time_us(void)
{
    return systimer_get_us();
}
static inline void speedtest_feed_wdt(void)
{
    wdt_feed();
}
#else
static inline uint64_t speedtest_get_time_us(void)
{
    static uint64_t s_mock_us = 1000000ULL;
    return s_mock_us += 100ULL;
}
static inline void speedtest_feed_wdt(void)
{
}
#endif

/* ========================================================================= */
/* Static Storage & Benchmark State (Zero Dynamic Heap Allocation)           */
/* ========================================================================= */
static speedtest_result_t s_last_result;
static speedtest_telemetry_t s_speedtest_telemetry;
static bool s_speedtest_initialized = false;

/* Static reusable packet buffer to prevent runtime stack exhaustion */
static uint8_t s_speedtest_packet_buf[SPEEDTEST_MAX_PACKET_SIZE];

/* ========================================================================= */
/* Subsystem Lifecycle Implementation                                        */
/* ========================================================================= */

speedtest_status_t speedtest_init(void)
{
    memset(&s_last_result, 0, sizeof(s_last_result));
    memset(&s_speedtest_telemetry, 0, sizeof(s_speedtest_telemetry));
    memset(s_speedtest_packet_buf, 0, sizeof(s_speedtest_packet_buf));

    s_speedtest_initialized = true;
    return SPEEDTEST_OK;
}

void speedtest_reset(void)
{
    memset(&s_last_result, 0, sizeof(s_last_result));
    memset(&s_speedtest_telemetry, 0, sizeof(s_speedtest_telemetry));
}

/* ========================================================================= */
/* Throughput & Latency Conversion Engines (Zero Magic Numbers)              */
/* ========================================================================= */

uint32_t speedtest_calculate_throughput_kbps(uint32_t bytes, uint32_t elapsed_us)
{
    if (elapsed_us == 0U || bytes == 0U)
    {
        return 0U;
    }

    /* Throughput in kbps = (bytes * 8 * 1,000,000 / elapsed_us) / 1,000
     *                    = (bytes * 8 * 1,000) / elapsed_us */
    uint64_t numerator = (uint64_t)bytes * SPEEDTEST_BITS_PER_BYTE * SPEEDTEST_KBPS_FACTOR;
    return (uint32_t)(numerator / (uint64_t)elapsed_us);
}

uint32_t speedtest_calculate_throughput_mbps(uint32_t bytes, uint32_t elapsed_us)
{
    if (elapsed_us == 0U || bytes == 0U)
    {
        return 0U;
    }

    /* Throughput in Mbps = (bytes * 8 * 1,000,000 / elapsed_us) / 1,000,000
     *                    = (bytes * 8) / elapsed_us */
    uint64_t numerator = (uint64_t)bytes * SPEEDTEST_BITS_PER_BYTE;
    return (uint32_t)(numerator / (uint64_t)elapsed_us);
}

uint32_t speedtest_kbps_to_mbps(uint32_t kbps)
{
    return kbps / SPEEDTEST_KBPS_PER_MBPS;
}

/* ========================================================================= */
/* Benchmark Execution Engines                                               */
/* ========================================================================= */

speedtest_status_t speedtest_run_synthetic_burst(uint32_t packet_count,
                                                 uint32_t packet_size,
                                                 speedtest_result_t *out_result)
{
    if (!s_speedtest_initialized)
    {
        speedtest_init();
    }

    if (packet_count == 0U)
    {
        packet_count = SPEEDTEST_DEFAULT_BURST_COUNT;
    }
    if (packet_size == 0U)
    {
        packet_size = SPEEDTEST_DEFAULT_PACKET_SIZE;
    }

    if (packet_count < SPEEDTEST_MIN_BURST_COUNT || packet_count > SPEEDTEST_MAX_BURST_COUNT ||
        packet_size < SPEEDTEST_MIN_PACKET_SIZE || packet_size > SPEEDTEST_MAX_PACKET_SIZE)
    {
        return SPEEDTEST_ERR_INVALID_PARAM;
    }

    speedtest_packet_header_t *hdr = (speedtest_packet_header_t *)s_speedtest_packet_buf;

    uint32_t latency_min = 0xFFFFFFFFU;
    uint32_t latency_max = 0U;
    uint64_t latency_sum = 0ULL;

    uint32_t start_time = (uint32_t)speedtest_get_time_us();

    for (uint32_t i = 0U; i < packet_count; i++)
    {
        uint32_t p_start = (uint32_t)speedtest_get_time_us();

        hdr->magic        = SPEEDTEST_MAGIC_HEADER;
        hdr->sequence     = i;
        hdr->timestamp_us = p_start;
        hdr->payload_len  = (uint16_t)packet_size;
        hdr->flags        = SPEEDTEST_FLAG_BURST;

        /* Deterministic payload pattern */
        uint8_t pat = (uint8_t)(i ^ SPEEDTEST_PAYLOAD_PATTERN);
        for (uint32_t b = sizeof(speedtest_packet_header_t); b < packet_size; b++)
        {
            s_speedtest_packet_buf[b] = pat;
        }

        /* Checksum / integrity processing simulation */
        volatile uint16_t dummy_chk = net_checksum(s_speedtest_packet_buf, packet_size);
        (void)dummy_chk;

        uint32_t p_end = (uint32_t)speedtest_get_time_us();
        uint32_t p_lat = (p_end >= p_start) ? (p_end - p_start) : 0U;

        if (p_lat < latency_min)
        {
            latency_min = p_lat;
        }
        if (p_lat > latency_max)
        {
            latency_max = p_lat;
        }
        latency_sum += p_lat;

        if ((i % SPEEDTEST_WDT_FEED_INTERVAL) == 0U)
        {
            speedtest_feed_wdt();
        }
    }

    uint32_t end_time = (uint32_t)speedtest_get_time_us();
    uint32_t elapsed_us = (end_time > start_time) ? (end_time - start_time) : 1U;
    uint32_t total_bytes = packet_count * packet_size;
    uint32_t throughput_kbps = speedtest_calculate_throughput_kbps(total_bytes, elapsed_us);
    uint32_t throughput_mbps = speedtest_calculate_throughput_mbps(total_bytes, elapsed_us);

    speedtest_result_t res;
    res.total_bytes_transferred = total_bytes;
    res.start_time_us           = start_time;
    res.end_time_us             = end_time;
    res.throughput_kbps         = throughput_kbps;
    res.latency_min_us          = (latency_min == 0xFFFFFFFFU) ? 0U : latency_min;
    res.latency_max_us          = latency_max;
    res.packet_loss_count       = 0U;

    /* Update internal telemetry records */
    s_last_result = res;
    s_speedtest_telemetry.bursts_run++;
    s_speedtest_telemetry.total_packets_tx += packet_count;
    s_speedtest_telemetry.total_bytes_tx += total_bytes;
    s_speedtest_telemetry.last_throughput_kbps = throughput_kbps;
    s_speedtest_telemetry.last_throughput_mbps = throughput_mbps;
    s_speedtest_telemetry.last_duration_us = elapsed_us;
    s_speedtest_telemetry.last_latency_min_us = res.latency_min_us;
    s_speedtest_telemetry.last_latency_max_us = res.latency_max_us;
    s_speedtest_telemetry.last_latency_avg_us = (uint32_t)(latency_sum / packet_count);
    s_speedtest_telemetry.last_packet_loss = 0U;

    if (out_result != NULL)
    {
        *out_result = res;
    }

    return SPEEDTEST_OK;
}

speedtest_status_t speedtest_run_udp_tx(uint32_t target_ip,
                                        uint16_t target_port,
                                        uint32_t packet_count,
                                        uint32_t packet_size,
                                        speedtest_result_t *out_result)
{
    if (!s_speedtest_initialized)
    {
        speedtest_init();
    }

    if (target_ip == 0U)
    {
        net_config_t cfg;
        net_get_config(&cfg);
        target_ip = cfg.gateway;
        if (target_ip == 0U)
        {
            return SPEEDTEST_ERR_INVALID_PARAM;
        }
    }
    if (target_port == 0U)
    {
        target_port = SPEEDTEST_DEFAULT_PORT;
    }
    if (packet_count == 0U)
    {
        packet_count = SPEEDTEST_DEFAULT_BURST_COUNT;
    }
    if (packet_size == 0U)
    {
        packet_size = SPEEDTEST_DEFAULT_PACKET_SIZE;
    }

    if (packet_count < SPEEDTEST_MIN_BURST_COUNT || packet_count > SPEEDTEST_MAX_BURST_COUNT ||
        packet_size < SPEEDTEST_MIN_PACKET_SIZE || packet_size > SPEEDTEST_MAX_PACKET_SIZE)
    {
        return SPEEDTEST_ERR_INVALID_PARAM;
    }

    speedtest_packet_header_t *hdr = (speedtest_packet_header_t *)s_speedtest_packet_buf;

    uint32_t packets_sent = 0U;
    uint32_t packet_loss = 0U;
    uint32_t bytes_sent = 0U;

    uint32_t latency_min = 0xFFFFFFFFU;
    uint32_t latency_max = 0U;
    uint64_t latency_sum = 0ULL;

    uint32_t start_time = (uint32_t)speedtest_get_time_us();

    for (uint32_t i = 0U; i < packet_count; i++)
    {
        uint32_t p_start = (uint32_t)speedtest_get_time_us();

        hdr->magic        = SPEEDTEST_MAGIC_HEADER;
        hdr->sequence     = i;
        hdr->timestamp_us = p_start;
        hdr->payload_len  = (uint16_t)packet_size;
        hdr->flags        = SPEEDTEST_FLAG_BURST;

        uint8_t pat = (uint8_t)(i ^ SPEEDTEST_PAYLOAD_PATTERN);
        for (uint32_t b = sizeof(speedtest_packet_header_t); b < packet_size; b++)
        {
            s_speedtest_packet_buf[b] = pat;
        }

        net_status_t st = net_send_udp(target_ip, SPEEDTEST_DEFAULT_PORT, target_port,
                                       s_speedtest_packet_buf, (uint16_t)packet_size);

        uint32_t p_end = (uint32_t)speedtest_get_time_us();
        uint32_t p_lat = (p_end >= p_start) ? (p_end - p_start) : 0U;

        if (st == NET_OK)
        {
            packets_sent++;
            bytes_sent += packet_size;

            if (p_lat < latency_min)
            {
                latency_min = p_lat;
            }
            if (p_lat > latency_max)
            {
                latency_max = p_lat;
            }
            latency_sum += p_lat;
        }
        else
        {
            packet_loss++;
        }

        if ((i % SPEEDTEST_WDT_FEED_INTERVAL) == 0U)
        {
            speedtest_feed_wdt();
        }
    }

    uint32_t end_time = (uint32_t)speedtest_get_time_us();
    uint32_t elapsed_us = (end_time > start_time) ? (end_time - start_time) : 1U;
    uint32_t throughput_kbps = speedtest_calculate_throughput_kbps(bytes_sent, elapsed_us);
    uint32_t throughput_mbps = speedtest_calculate_throughput_mbps(bytes_sent, elapsed_us);

    speedtest_result_t res;
    res.total_bytes_transferred = bytes_sent;
    res.start_time_us           = start_time;
    res.end_time_us             = end_time;
    res.throughput_kbps         = throughput_kbps;
    res.latency_min_us          = (latency_min == 0xFFFFFFFFU) ? 0U : latency_min;
    res.latency_max_us          = latency_max;
    res.packet_loss_count       = packet_loss;

    s_last_result = res;
    s_speedtest_telemetry.bursts_run++;
    s_speedtest_telemetry.total_packets_tx += packets_sent;
    s_speedtest_telemetry.total_bytes_tx += bytes_sent;
    s_speedtest_telemetry.last_throughput_kbps = throughput_kbps;
    s_speedtest_telemetry.last_throughput_mbps = throughput_mbps;
    s_speedtest_telemetry.last_duration_us = elapsed_us;
    s_speedtest_telemetry.last_latency_min_us = res.latency_min_us;
    s_speedtest_telemetry.last_latency_max_us = res.latency_max_us;
    s_speedtest_telemetry.last_latency_avg_us = (packets_sent > 0U) ? (uint32_t)(latency_sum / packets_sent) : 0U;
    s_speedtest_telemetry.last_packet_loss = packet_loss;

    if (out_result != NULL)
    {
        *out_result = res;
    }

    return SPEEDTEST_OK;
}

/* ========================================================================= */
/* Inbound UDP Packet Processing & Echo Reflection                           */
/* ========================================================================= */

speedtest_status_t speedtest_process_udp_packet(const uint8_t *frame,
                                                const uint8_t *payload,
                                                uint16_t payload_len)
{
    if (frame == NULL || payload == NULL || payload_len < sizeof(speedtest_packet_header_t))
    {
        return SPEEDTEST_ERR_INVALID_PARAM;
    }

    const speedtest_packet_header_t *hdr = (const speedtest_packet_header_t *)payload;
    if (hdr->magic != SPEEDTEST_MAGIC_HEADER)
    {
        return SPEEDTEST_ERR_INVALID_PARAM;
    }

    if (!s_speedtest_initialized)
    {
        speedtest_init();
    }

    s_speedtest_telemetry.total_packets_rx++;
    s_speedtest_telemetry.total_bytes_rx += payload_len;

    /* Handle Echo Request */
    if ((hdr->flags & SPEEDTEST_FLAG_ECHO_REQ) != 0U)
    {
        const ipv4_header_t *ip = (const ipv4_header_t *)(frame + ETH_HDR_LEN);
        uint8_t ihl = (ip->ver_ihl & 0x0FU) * 4U;
        const udp_header_t *udp = (const udp_header_t *)(frame + ETH_HDR_LEN + ihl);

        uint32_t sender_ip = NET_NTOHL(ip->src_ip);
        uint16_t sender_port = NET_NTOHS(udp->src_port);

        if (payload_len <= sizeof(s_speedtest_packet_buf))
        {
            memcpy(s_speedtest_packet_buf, payload, payload_len);
            speedtest_packet_header_t *resp_hdr = (speedtest_packet_header_t *)s_speedtest_packet_buf;
            resp_hdr->flags = SPEEDTEST_FLAG_ECHO_RESP;

            net_send_udp(sender_ip, SPEEDTEST_DEFAULT_PORT, sender_port,
                         s_speedtest_packet_buf, payload_len);
        }
    }

    return SPEEDTEST_OK;
}

/* ========================================================================= */
/* Telemetry & State Queries                                                 */
/* ========================================================================= */

speedtest_status_t speedtest_get_last_result(speedtest_result_t *out_result)
{
    if (out_result == NULL)
    {
        return SPEEDTEST_ERR_INVALID_PARAM;
    }

    *out_result = s_last_result;
    return SPEEDTEST_OK;
}

speedtest_status_t speedtest_get_telemetry(speedtest_telemetry_t *out_telemetry)
{
    if (out_telemetry == NULL)
    {
        return SPEEDTEST_ERR_INVALID_PARAM;
    }

    *out_telemetry = s_speedtest_telemetry;
    return SPEEDTEST_OK;
}
