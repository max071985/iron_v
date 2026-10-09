/*
 * Iron V - Developer module (REV-33): tools for the test build only (PROFILE=dev).
 *
 * LAN speed test (speedtest.c, UDP port SPEEDTEST_DEFAULT_PORT), stability soak (soak.c), the
 * diagnostic routes /api/telemetry, /api/health and /api/speedtest, and their `info` lines and
 * commands. do-test and `soak` live in test.c (on-target only).
 */
#include "speedtest.h"
#include "soak.h"
#include "module.h"
#include "shell.h"
#include "http_server.h"
#include "api_v1.h"
#include "net.h"
#include "dpc.h"
#include "section.h"
#include "console.h"
#include "utils.h"
#include "string.h"

/* POST /api/speedtest?run: one synthetic burst */
#define DEV_SPEEDTEST_BURST_PACKETS      100U
#define DEV_SPEEDTEST_BURST_BYTES        1024U

/* GET /api/telemetry -> Returns HTTP & TCP telemetry JSON */
static void dev_http_telemetry(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U)
    {
        return;
    }

    http_telemetry_t ht;
    http_server_get_telemetry(&ht);
    response_body[0] = '\0';
    api_v1_append(response_body, max_len, "{\"http\":{\"requests_total\":");
    api_v1_append_u32(response_body, max_len, ht.requests_total);
    api_v1_append(response_body, max_len, ",\"requests_get\":");
    api_v1_append_u32(response_body, max_len, ht.requests_get);
    api_v1_append(response_body, max_len, ",\"requests_post\":");
    api_v1_append_u32(response_body, max_len, ht.requests_post);
    api_v1_append(response_body, max_len, ",\"responses_200\":");
    api_v1_append_u32(response_body, max_len, ht.responses_200);
    api_v1_append(response_body, max_len, ",\"responses_404\":");
    api_v1_append_u32(response_body, max_len, ht.responses_404);
    api_v1_append(response_body, max_len, ",\"responses_405\":");
    api_v1_append_u32(response_body, max_len, ht.responses_405);
    api_v1_append(response_body, max_len, "}}\r\n");
}

static bool dev_param_contains(const char *buf, const char *key)
{
    if (buf == NULL || key == NULL) return false;
    return strstr(buf, key) != NULL;
}

/* Flash read-only string constants to preserve 32KB DRAM stack headroom */
static const char s_str_close_brace[] FLASH_RODATA_ATTR = "}\r\n";
static const char s_str_health_prefix[] FLASH_RODATA_ATTR = "{\"status\":\"healthy\",\"uptime_seconds\":";
static const char s_str_arena_used[] FLASH_RODATA_ATTR = ",\"arena_bytes_used\":";
static const char s_str_arena_free[] FLASH_RODATA_ATTR = ",\"arena_bytes_free\":";
static const char s_str_dpc_drops[] FLASH_RODATA_ATTR = ",\"dpc_queue_drops\":";
static const char s_str_wdt_feeds[] FLASH_RODATA_ATTR = ",\"wdt_feeds_total\":";
static const char s_str_wifi_rx[] FLASH_RODATA_ATTR = ",\"wifi_packets_rx\":";
static const char s_str_wifi_tx[] FLASH_RODATA_ATTR = ",\"wifi_packets_tx\":";
static const char s_str_uart_active[] FLASH_RODATA_ATTR = ",\"uart_active\":";
static const char s_str_usb_active[] FLASH_RODATA_ATTR = ",\"usb_active\":";

static const char s_str_sp_bursts[] FLASH_RODATA_ATTR = "{\"status\":\"ok\",\"bursts_run\":";
static const char s_str_sp_thru_k[] FLASH_RODATA_ATTR = ",\"last_throughput_kbps\":";
static const char s_str_sp_thru_m[] FLASH_RODATA_ATTR = ",\"last_throughput_mbps\":";
static const char s_str_sp_lat_avg[] FLASH_RODATA_ATTR = ",\"last_latency_avg_us\":";
static const char s_str_sp_pkt_loss[] FLASH_RODATA_ATTR = ",\"last_packet_loss\":";
static const char s_str_sp_run_thru_k[] FLASH_RODATA_ATTR = "{\"status\":\"ok\",\"throughput_kbps\":";
static const char s_str_sp_run_thru_m[] FLASH_RODATA_ATTR = ",\"throughput_mbps\":";
static const char s_str_sp_run_lat_min[] FLASH_RODATA_ATTR = ",\"latency_min_us\":";
static const char s_str_sp_run_lat_max[] FLASH_RODATA_ATTR = ",\"latency_max_us\":";
static const char s_str_sp_run_pkt_loss[] FLASH_RODATA_ATTR = ",\"packet_loss_count\":";

/* GET /api/health -> Aggregated 24/7 system health telemetry */
static void dev_http_health(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U) return;

    system_health_telemetry_t h;
    shell_get_health_telemetry(&h);

    response_body[0] = '\0';
    api_v1_append(response_body, max_len, s_str_health_prefix);
    api_v1_append_u32(response_body, max_len, h.uptime_seconds);
    api_v1_append(response_body, max_len, s_str_arena_used);
    api_v1_append_u32(response_body, max_len, h.arena_bytes_used);
    api_v1_append(response_body, max_len, s_str_arena_free);
    api_v1_append_u32(response_body, max_len, h.arena_bytes_free);
    api_v1_append(response_body, max_len, s_str_dpc_drops);
    api_v1_append_u32(response_body, max_len, h.dpc_queue_drops);
    api_v1_append(response_body, max_len, s_str_wdt_feeds);
    api_v1_append_u32(response_body, max_len, h.wdt_feeds_total);
    api_v1_append(response_body, max_len, s_str_wifi_rx);
    api_v1_append_u32(response_body, max_len, h.wifi_packets_rx);
    api_v1_append(response_body, max_len, s_str_wifi_tx);
    api_v1_append_u32(response_body, max_len, h.wifi_packets_tx);
    api_v1_append(response_body, max_len, s_str_uart_active);
    api_v1_append_u32(response_body, max_len, h.uart_active);
    api_v1_append(response_body, max_len, s_str_usb_active);
    api_v1_append_u32(response_body, max_len, h.usb_active);
    api_v1_append(response_body, max_len, s_str_close_brace);
}

/* GET & POST /api/speedtest -> Diagnostics & throughput benchmark */
static void dev_http_speedtest(const char *query_params, char *response_body, size_t max_len)
{
    if (response_body == NULL || max_len == 0U) return;

    response_body[0] = '\0';

    if (query_params != NULL && (dev_param_contains(query_params, "run") || dev_param_contains(query_params, "burst")))
    {
        speedtest_result_t res;
        speedtest_run_synthetic_burst(DEV_SPEEDTEST_BURST_PACKETS, DEV_SPEEDTEST_BURST_BYTES, &res);
        api_v1_append(response_body, max_len, s_str_sp_run_thru_k);
        api_v1_append_u32(response_body, max_len, res.throughput_kbps);
        api_v1_append(response_body, max_len, s_str_sp_run_thru_m);
        api_v1_append_u32(response_body, max_len, speedtest_kbps_to_mbps(res.throughput_kbps));
        api_v1_append(response_body, max_len, s_str_sp_run_lat_min);
        api_v1_append_u32(response_body, max_len, res.latency_min_us);
        api_v1_append(response_body, max_len, s_str_sp_run_lat_max);
        api_v1_append_u32(response_body, max_len, res.latency_max_us);
        api_v1_append(response_body, max_len, s_str_sp_run_pkt_loss);
        api_v1_append_u32(response_body, max_len, res.packet_loss_count);
        api_v1_append(response_body, max_len, s_str_close_brace);
    }
    else
    {
        speedtest_telemetry_t telem;
        speedtest_get_telemetry(&telem);
        api_v1_append(response_body, max_len, s_str_sp_bursts);
        api_v1_append_u32(response_body, max_len, telem.bursts_run);
        api_v1_append(response_body, max_len, s_str_sp_thru_k);
        api_v1_append_u32(response_body, max_len, telem.last_throughput_kbps);
        api_v1_append(response_body, max_len, s_str_sp_thru_m);
        api_v1_append_u32(response_body, max_len, telem.last_throughput_mbps);
        api_v1_append(response_body, max_len, s_str_sp_lat_avg);
        api_v1_append_u32(response_body, max_len, telem.last_latency_avg_us);
        api_v1_append(response_body, max_len, s_str_sp_pkt_loss);
        api_v1_append_u32(response_body, max_len, telem.last_packet_loss);
        api_v1_append(response_body, max_len, s_str_close_brace);
    }
}


static void dev_speedtest_udp(const uint8_t *frame, const uint8_t *payload, uint16_t payload_len)
{
    (void)speedtest_process_udp_packet(frame, payload, payload_len);
}

static void dev_module_init(uint64_t now_us)
{
    (void)now_us;
    (void)soak_init();
    (void)speedtest_init();
    (void)net_udp_listen(SPEEDTEST_DEFAULT_PORT, dev_speedtest_udp);
}

static void dev_module_routes(void)
{
    (void)http_route_register("/api/telemetry", HTTP_METHOD_GET, dev_http_telemetry);
    (void)http_route_register("/api/health", HTTP_METHOD_GET, dev_http_health);
    (void)http_route_register("/api/speedtest", HTTP_METHOD_GET, dev_http_speedtest);
    (void)http_route_register("/api/speedtest", HTTP_METHOD_POST, dev_http_speedtest);
}

static void dev_module_info(void)
{
    speedtest_telemetry_t sptel;
    speedtest_get_telemetry(&sptel);
    console_puts(" Diag:    Port: ");
    put_dec(SPEEDTEST_DEFAULT_PORT);
    console_puts(", Bursts: ");
    put_dec(sptel.bursts_run);
    console_puts(", Last: ");
    put_dec(sptel.last_throughput_mbps);
    console_puts(" Mbps\r\n");

    soak_telemetry_t soak_tel;
    soak_get_telemetry(&soak_tel);
    console_puts(" Soak:    Cycles: ");
    put_dec(soak_tel.completed_cycles);
    console_puts(", Streak: ");
    put_dec(soak_tel.clean_streak);
    console_puts(", LeakFree: ");
    console_puts(!soak_tel.mem_leak_detected ? "YES" : "NO");
    console_puts(", Drops: ");
    put_dec(dpc_get_drop_count());
    console_puts("\r\n");
}

static void dev_speedtest_shell(char *args)
{
    char *subcmd = args;

    if (strncmp(subcmd, "reset", 5) == 0)
    {
        speedtest_reset();
        console_puts("Speed-Test engine statistics reset.\r\n");
    }
    else if (strncmp(subcmd, "udp", 3) == 0)
    {
        char *arg = subcmd + 3;
        while (*arg == ' ') arg++;

        uint32_t target_ip = 0U;
        uint32_t count = SPEEDTEST_DEFAULT_BURST_COUNT;
        uint32_t size = SPEEDTEST_DEFAULT_PACKET_SIZE;

        if (*arg != '\0')
        {
            char ip_tok[32];
            size_t tok_len = 0U;
            while (*arg != ' ' && *arg != '\0' && tok_len < (sizeof(ip_tok) - 1U))
            {
                ip_tok[tok_len++] = *arg++;
            }
            ip_tok[tok_len] = '\0';
            target_ip = net_str_to_ip(ip_tok);
            while (*arg == ' ') arg++;
            if (*arg != '\0')
            {
                uint32_t c_in = 0U;
                if (shell_parse_uint(&arg, &c_in)) count = c_in;
                while (*arg == ' ') arg++;
                if (*arg != '\0')
                {
                    uint32_t s_in = 0U;
                    if (shell_parse_uint(&arg, &s_in)) size = s_in;
                }
            }
        }

        if (target_ip == 0U)
        {
            net_config_t cfg;
            net_get_config(&cfg);
            target_ip = cfg.gateway;
        }

        char ip_s[NET_IP_STR_BUF_LEN];
        net_ip_to_str(target_ip, ip_s, sizeof(ip_s));
        console_puts("Executing UDP Speed-Test Benchmark Burst to ");
        console_puts(ip_s);
        console_puts(" (");
        put_dec(count);
        console_puts(" packets, ");
        put_dec(size);
        console_puts(" B each)...\r\n");

        speedtest_result_t res;
        speedtest_status_t st = speedtest_run_udp_tx(target_ip, SPEEDTEST_DEFAULT_PORT, count, size, &res);
        if (st == SPEEDTEST_OK)
        {
            uint32_t dur_us = (res.end_time_us > res.start_time_us) ? (res.end_time_us - res.start_time_us) : 1U;
            uint32_t mbps = speedtest_calculate_throughput_mbps(res.total_bytes_transferred, dur_us);
            console_puts("UDP Benchmark Completed:\r\n");
            console_puts("  Bytes Transferred: "); put_dec(res.total_bytes_transferred); console_puts(" B\r\n");
            console_puts("  Duration:          "); put_dec(dur_us / 1000U); console_puts(" ms ("); put_dec(dur_us); console_puts(" us)\r\n");
            console_puts("  Throughput:        "); put_dec(res.throughput_kbps); console_puts(" kbps ("); put_dec(mbps); console_puts(" Mbps)\r\n");
            console_puts("  Latency Min/Max:   "); put_dec(res.latency_min_us); console_puts(" us / "); put_dec(res.latency_max_us); console_puts(" us\r\n");
            console_puts("  Packet Loss:       "); put_dec(res.packet_loss_count); console_puts("\r\n");
        }
        else
        {
            console_puts("UDP Benchmark Failed (error code: ");
            put_dec((uint32_t)st);
            console_puts(")\r\n");
        }
    }
    else if (strncmp(subcmd, "status", 6) == 0)
    {
        speedtest_telemetry_t st;
        speedtest_get_telemetry(&st);
        console_puts("Speed-Test Benchmark Engine Telemetry:\r\n");
        console_puts("  Bursts Executed:   "); put_dec(st.bursts_run); console_puts("\r\n");
        console_puts("  Packets TX / RX:   "); put_dec(st.total_packets_tx); console_puts(" / "); put_dec(st.total_packets_rx); console_puts("\r\n");
        console_puts("  Bytes TX / RX:     "); put_dec(st.total_bytes_tx); console_puts(" / "); put_dec(st.total_bytes_rx); console_puts("\r\n");
        console_puts("  Last Throughput:   "); put_dec(st.last_throughput_kbps); console_puts(" kbps ("); put_dec(st.last_throughput_mbps); console_puts(" Mbps)\r\n");
        console_puts("  Last Latency:      Min="); put_dec(st.last_latency_min_us); console_puts(" us, Max="); put_dec(st.last_latency_max_us);
        console_puts(" us, Avg="); put_dec(st.last_latency_avg_us); console_puts(" us\r\n");
        console_puts("  Last Packet Loss:  "); put_dec(st.last_packet_loss); console_puts("\r\n");
    }
    else
    {
        uint32_t count = SPEEDTEST_DEFAULT_BURST_COUNT;
        uint32_t size = SPEEDTEST_DEFAULT_PACKET_SIZE;

        if (strncmp(subcmd, "run", 3) == 0 || strncmp(subcmd, "burst", 5) == 0)
        {
            if (strncmp(subcmd, "run", 3) == 0) subcmd += 3;
            else subcmd += 5;
            while (*subcmd == ' ') subcmd++;
            if (*subcmd != '\0')
            {
                uint32_t c_in = 0U;
                if (shell_parse_uint(&subcmd, &c_in)) count = c_in;
                while (*subcmd == ' ') subcmd++;
                if (*subcmd != '\0')
                {
                    uint32_t s_in = 0U;
                    if (shell_parse_uint(&subcmd, &s_in)) size = s_in;
                }
            }
        }

        console_puts("Running Synthetic Speed-Test Burst Benchmark (");
        put_dec(count);
        console_puts(" packets, ");
        put_dec(size);
        console_puts(" B each)...\r\n");

        speedtest_result_t res;
        speedtest_status_t st = speedtest_run_synthetic_burst(count, size, &res);
        if (st == SPEEDTEST_OK)
        {
            uint32_t dur_us = (res.end_time_us > res.start_time_us) ? (res.end_time_us - res.start_time_us) : 1U;
            uint32_t mbps = speedtest_calculate_throughput_mbps(res.total_bytes_transferred, dur_us);
            console_puts("Synthetic Benchmark Result:\r\n");
            console_puts("  Total Transferred: "); put_dec(res.total_bytes_transferred); console_puts(" B ("); put_dec(res.total_bytes_transferred / 1024U); console_puts(" KB)\r\n");
            console_puts("  Duration:          "); put_dec(dur_us / 1000U); console_puts(" ms ("); put_dec(dur_us); console_puts(" us)\r\n");
            console_puts("  Throughput:        "); put_dec(res.throughput_kbps); console_puts(" kbps ("); put_dec(mbps); console_puts(" Mbps)\r\n");
            console_puts("  Latency Min/Max:   "); put_dec(res.latency_min_us); console_puts(" us / "); put_dec(res.latency_max_us); console_puts(" us\r\n");
            console_puts("  Packet Loss:       0\r\n");
        }
        else
        {
            console_puts("Synthetic Benchmark Failed (error code: ");
            put_dec((uint32_t)st);
            console_puts(")\r\n");
        }
    }
}

MODULE_DEFINE(s_dev_module, {
    .name       = "dev",
    .order      = MODULE_ORDER_DEV,
    .init       = dev_module_init,
    .routes     = dev_module_routes,
    .print_info = dev_module_info,
});

SHELL_COMMAND_DEFINE(s_speedtest_cmd, {
    .name = "speedtest",
    .help = "speedtest [run|burst|udp|status|reset] - Run or inspect LAN network & Wi-Fi throughput benchmark",
    .run  = dev_speedtest_shell,
});
