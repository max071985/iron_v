/*
 * src/http_server.c
 *
 * Zero-Allocation Local REST/HTTP Engine & Embedded Web Server
 * RFC 2616 (HTTP/1.1) & RFC 7230 / RFC 7231 (HTTP Semantics)
 *
 * Implements deterministic static route tables, zero-copy HTTP/1.1 request
 * parser, JSON REST endpoints, and static HTML asset delivery over bare-metal TCP.
 */

#include "http_server.h"
#include "section.h"
#include "config.h"
#include "web_assets.h"
#include "arena.h"
#include "string.h"
#include "wdt.h"
#include "clock.h"
#include "shell.h"
#include "speedtest.h"
#include "gpio.h"

#if defined(__riscv)
#include "systimer.h"
#include "console.h"
#include "utils.h"
static inline uint64_t http_get_uptime_ms(void)
{
    return systimer_get_ms();
}
#else
static inline uint64_t http_get_uptime_ms(void)
{
    static uint64_t s_mock_ms = 1000ULL;
    return s_mock_ms += 100ULL;
}
#endif

/* ========================================================================= */
/* Static Storage & Server State (Zero Dynamic Heap Allocation)              */
/* ========================================================================= */
static http_route_t s_http_routes[HTTP_MAX_ROUTES];
static uint16_t s_http_route_count = 0U;
static http_telemetry_t s_http_telemetry;
static tcp_pcb_t *s_http_listener_pcb = NULL;
static bool s_http_initialized = false;

/* ========================================================================= */
/* Internal String Formatting Utilities (Zero C-Library Dependency)          */
/* ========================================================================= */

static size_t http_u32_to_dec(uint32_t val, char *buf, size_t max_len)
{
    if (buf == NULL || max_len < 2U)
    {
        return 0U;
    }

    if (val == 0U)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1U;
    }

    char temp[12];
    size_t digits = 0U;
    while (val > 0U && digits < sizeof(temp))
    {
        temp[digits++] = (char)('0' + (val % 10U));
        val /= 10U;
    }

    if (digits >= max_len)
    {
        digits = max_len - 1U;
    }

    for (size_t i = 0U; i < digits; i++)
    {
        buf[i] = temp[digits - 1U - i];
    }
    buf[digits] = '\0';
    return digits;
}

static size_t http_u64_to_dec(uint64_t val, char *buf, size_t max_len)
{
    if (buf == NULL || max_len < 2U)
    {
        return 0U;
    }

    if (val == 0ULL)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1U;
    }

    char temp[24];
    size_t digits = 0U;
    while (val > 0ULL && digits < sizeof(temp))
    {
        temp[digits++] = (char)('0' + (val % 10ULL));
        val /= 10ULL;
    }

    if (digits >= max_len)
    {
        digits = max_len - 1U;
    }

    for (size_t i = 0U; i < digits; i++)
    {
        buf[i] = temp[digits - 1U - i];
    }
    buf[digits] = '\0';
    return digits;
}

static size_t http_str_append(char *dest, size_t dest_max, const char *src)
{
    if (dest == NULL || src == NULL || dest_max == 0U)
    {
        return 0U;
    }

    size_t dlen = strlen(dest);
    if (dlen >= dest_max - 1U)
    {
        return dlen;
    }

    size_t avail = dest_max - 1U - dlen;
    size_t slen  = strlen(src);
    size_t to_copy = (slen < avail) ? slen : avail;

    memcpy(dest + dlen, src, to_copy);
    dest[dlen + to_copy] = '\0';
    return dlen + to_copy;
}

/* ========================================================================= */
/* Default Built-In REST Handlers                                            */
/* ========================================================================= */

/* GET / and GET /index.html -> Serves embedded HTML dashboard */
static void http_handler_root(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U)
    {
        return;
    }

    size_t asset_len = strlen(g_index_html);
    if (asset_len >= max_len)
    {
        asset_len = max_len - 1U;
    }
    memcpy(response_body, g_index_html, asset_len);
    response_body[asset_len] = '\0';
}

/* GET /api/status -> Returns JSON with uptime_ms, cpu_mhz, hostname */
static void http_handler_status(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U)
    {
        return;
    }

    uint64_t uptime_ms = http_get_uptime_ms();
    clock_config_t clk;
    clock_get_config(&clk);

    char num_buf[24];

    response_body[0] = '\0';
    http_str_append(response_body, max_len, "{\"status\":\"online\",\"hostname\":\"");
    http_str_append(response_body, max_len, CONFIG_DEVICE_HOSTNAME);
    http_str_append(response_body, max_len, "\",\"uptime_ms\":");
    http_u64_to_dec(uptime_ms, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, ",\"cpu_mhz\":");
    http_u32_to_dec(clk.cpu_mhz, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, ",\"firmware\":{\"name\":\"Iron V\",\"version\":\"");
    http_str_append(response_body, max_len, CONFIG_FIRMWARE_REVISION);
    http_str_append(response_body, max_len, "\"}}\r\n");
}

/* GET /api/info -> Returns device identification JSON */
static void http_handler_info(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U)
    {
        return;
    }

    response_body[0] = '\0';
    http_str_append(response_body, max_len, "{\"hostname\":\"");
    http_str_append(response_body, max_len, CONFIG_DEVICE_HOSTNAME);
    http_str_append(response_body, max_len, "\",\"model\":\"");
    http_str_append(response_body, max_len, CONFIG_DEVICE_MODEL_NUMBER);
    http_str_append(response_body, max_len, "\",\"manufacturer\":\"");
    http_str_append(response_body, max_len, CONFIG_DEVICE_MANUFACTURER);
    http_str_append(response_body, max_len, "\",\"firmware_rev\":\"");
    http_str_append(response_body, max_len, CONFIG_FIRMWARE_REVISION);
    http_str_append(response_body, max_len, "\",\"hardware_rev\":\"");
    http_str_append(response_body, max_len, CONFIG_HARDWARE_REVISION);
    http_str_append(response_body, max_len, "\"}\r\n");
}

/* GET /api/telemetry -> Returns HTTP & TCP telemetry JSON */
static void http_handler_telemetry(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U)
    {
        return;
    }

    char num_buf[16];
    response_body[0] = '\0';
    http_str_append(response_body, max_len, "{\"http\":{\"requests_total\":");
    http_u32_to_dec(s_http_telemetry.requests_total, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, ",\"requests_get\":");
    http_u32_to_dec(s_http_telemetry.requests_get, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, ",\"requests_post\":");
    http_u32_to_dec(s_http_telemetry.requests_post, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, ",\"responses_200\":");
    http_u32_to_dec(s_http_telemetry.responses_200, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, ",\"responses_404\":");
    http_u32_to_dec(s_http_telemetry.responses_404, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, ",\"responses_405\":");
    http_u32_to_dec(s_http_telemetry.responses_405, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, "}}\r\n");
}

/* POST /api/wdt/feed -> Feeds watchdog supervisor via REST API */
static void http_handler_wdt_feed(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U)
    {
        return;
    }

    wdt_feed();
    response_body[0] = '\0';
    http_str_append(response_body, max_len, "{\"status\":\"ok\",\"fed\":true,\"message\":\"watchdog fed\"}\r\n");
}

/* GET /favicon.ico -> Returns 200 OK empty response */
static void http_handler_favicon(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    (void)max_len;
    if (response_body != NULL)
    {
        response_body[0] = '\0';
    }
}

/* Connectivity probes (Android, Apple, Windows): empty body, answered with a 302 to the portal */
static void http_handler_connectivity_probe(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    (void)max_len;
    if (response_body != NULL)
    {
        response_body[0] = '\0';
    }
}

/* Android, Apple and Windows connectivity-check paths */
static bool http_is_connectivity_probe(const char *path)
{
    return strcmp(path, "/generate_204") == 0 || strcmp(path, "/gen_204") == 0 ||
           strcmp(path, "/hotspot-detect.html") == 0 || strcmp(path, "/canonical.html") == 0 ||
           strcmp(path, "/ncsi.txt") == 0 || strcmp(path, "/connecttest.txt") == 0;
}

/* Appends "Location: http://<board IP>/setup" for the current interface address */
static void http_append_portal_location(char *out, size_t max_len)
{
    net_config_t ncfg;
    net_get_config(&ncfg);
    char octet[4];
    http_str_append(out, max_len, "Location: http://");
    for (uint32_t i = 0U; i < 4U; i++)
    {
        uint32_t shift = 24U - (i * 8U);
        http_u32_to_dec((ncfg.ip >> shift) & 0xFFU, octet, sizeof(octet));
        http_str_append(out, max_len, octet);
        if (i < 3U)
        {
            http_str_append(out, max_len, ".");
        }
    }
    http_str_append(out, max_len, HTTP_CAPTIVE_PORTAL_PATH "\r\n");
}

static bool http_find_int_param(const char *buf, const char *key, int32_t *out_val)
{
    if (buf == NULL || key == NULL || out_val == NULL) return false;
    const char *p = strstr(buf, key);
    if (p == NULL) return false;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == ':' || *p == '=' || *p == '"') p++;
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    if (*p < '0' || *p > '9') return false;
    int32_t val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    *out_val = neg ? -val : val;
    return true;
}

static bool http_param_contains(const char *buf, const char *key)
{
    if (buf == NULL || key == NULL) return false;
    return strstr(buf, key) != NULL;
}

/* Flash read-only string constants to preserve 32KB DRAM stack headroom */
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


static const char s_str_gpio_ok_pin[] FLASH_RODATA_ATTR = "{\"status\":\"ok\",\"pin\":";
static const char s_str_gpio_level[] FLASH_RODATA_ATTR = ",\"level\":";
static const char s_str_gpio_pins_pre[] FLASH_RODATA_ATTR = "{\"pins\":[";
static const char s_str_gpio_p15[] FLASH_RODATA_ATTR = "{\"pin\":15,\"level\":";
static const char s_str_gpio_n15[] FLASH_RODATA_ATTR = ",\"name\":\"Status LED\"},";
static const char s_str_gpio_p2[] FLASH_RODATA_ATTR = "{\"pin\":2,\"level\":";
static const char s_str_gpio_n2[] FLASH_RODATA_ATTR = ",\"name\":\"Relay 1\"},";
static const char s_str_gpio_p3[] FLASH_RODATA_ATTR = "{\"pin\":3,\"level\":";
static const char s_str_gpio_n3[] FLASH_RODATA_ATTR = ",\"name\":\"Relay 2\"},";
static const char s_str_gpio_p8[] FLASH_RODATA_ATTR = "{\"pin\":8,\"level\":";
static const char s_str_gpio_n8[] FLASH_RODATA_ATTR = ",\"name\":\"Output Pin 8\"}";
static const char s_str_close_bracket[] FLASH_RODATA_ATTR = "]}\r\n";
static const char s_str_close_brace[] FLASH_RODATA_ATTR = "}\r\n";
static const char s_cors_methods_hdr[] FLASH_RODATA_ATTR = "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
static const char s_cors_headers_hdr[] FLASH_RODATA_ATTR = "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
static const char s_cors_preflight_resp[] FLASH_RODATA_ATTR =
    "HTTP/1.1 204 No Content\r\n"
    HTTP_SERVER_HEADER
    HTTP_CONN_CLOSE_HEADER
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
    "Access-Control-Max-Age: 86400\r\n"
    "Content-Length: 0\r\n\r\n";

/* GET /api/health -> Aggregated 24/7 system health telemetry */
static void http_handler_health(const char *query_params, char *response_body, size_t max_len)
{
    (void)query_params;
    if (response_body == NULL || max_len == 0U) return;

    system_health_telemetry_t h;
    shell_get_health_telemetry(&h);

    char num_buf[24];
    response_body[0] = '\0';
    http_str_append(response_body, max_len, s_str_health_prefix);
    http_u32_to_dec(h.uptime_seconds, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, s_str_arena_used);
    http_u32_to_dec(h.arena_bytes_used, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, s_str_arena_free);
    http_u32_to_dec(h.arena_bytes_free, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, s_str_dpc_drops);
    http_u32_to_dec(h.dpc_queue_drops, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, s_str_wdt_feeds);
    http_u32_to_dec(h.wdt_feeds_total, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, s_str_wifi_rx);
    http_u32_to_dec(h.wifi_packets_rx, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, s_str_wifi_tx);
    http_u32_to_dec(h.wifi_packets_tx, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, s_str_uart_active);
    http_u32_to_dec(h.uart_active, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, s_str_usb_active);
    http_u32_to_dec(h.usb_active, num_buf, sizeof(num_buf));
    http_str_append(response_body, max_len, num_buf);
    http_str_append(response_body, max_len, s_str_close_brace);
}

/* GET & POST /api/speedtest -> Diagnostics & throughput benchmark */
static void http_handler_speedtest(const char *query_params, char *response_body, size_t max_len)
{
    if (response_body == NULL || max_len == 0U) return;

    char num_buf[24];
    response_body[0] = '\0';

    if (query_params != NULL && (http_param_contains(query_params, "run") || http_param_contains(query_params, "burst")))
    {
        speedtest_result_t res;
        speedtest_run_synthetic_burst(100U, 1024U, &res);
        http_str_append(response_body, max_len, s_str_sp_run_thru_k);
        http_u32_to_dec(res.throughput_kbps, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_sp_run_thru_m);
        http_u32_to_dec(speedtest_kbps_to_mbps(res.throughput_kbps), num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_sp_run_lat_min);
        http_u32_to_dec(res.latency_min_us, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_sp_run_lat_max);
        http_u32_to_dec(res.latency_max_us, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_sp_run_pkt_loss);
        http_u32_to_dec(res.packet_loss_count, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_close_brace);
    }
    else
    {
        speedtest_telemetry_t telem;
        speedtest_get_telemetry(&telem);
        http_str_append(response_body, max_len, s_str_sp_bursts);
        http_u32_to_dec(telem.bursts_run, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_sp_thru_k);
        http_u32_to_dec(telem.last_throughput_kbps, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_sp_thru_m);
        http_u32_to_dec(telem.last_throughput_mbps, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_sp_lat_avg);
        http_u32_to_dec(telem.last_latency_avg_us, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_sp_pkt_loss);
        http_u32_to_dec(telem.last_packet_loss, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_close_brace);
    }
}

/* GET & POST /api/gpio -> Interactive GPIO pin & relay control */
static void http_handler_gpio(const char *query_params, char *response_body, size_t max_len)
{
    if (response_body == NULL || max_len == 0U) return;

    char num_buf[24];
    response_body[0] = '\0';

    int32_t pin = -1;
    if (http_find_int_param(query_params, "pin", &pin) && pin >= 0 && pin <= (int32_t)GPIO_PIN_MAX)
    {
        uint32_t u_pin = (uint32_t)pin;
        if (http_param_contains(query_params, "toggle"))
        {
            gpio_toggle_level(u_pin);
        }
        else
        {
            int32_t val = 0;
            if (http_find_int_param(query_params, "value", &val) || http_find_int_param(query_params, "level", &val))
            {
                gpio_set_level(u_pin, (val != 0) ? 1U : 0U);
            }
        }

        int curr_lvl = gpio_get_output_level(u_pin);
        if (curr_lvl < 0) curr_lvl = 0;

        http_str_append(response_body, max_len, s_str_gpio_ok_pin);
        http_u32_to_dec(u_pin, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_gpio_level);
        http_u32_to_dec((uint32_t)curr_lvl, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_close_brace);
    }
    else
    {
        int l15 = gpio_get_output_level(15U); if (l15 < 0) l15 = 0;
        int l2  = gpio_get_output_level(2U);  if (l2 < 0)  l2 = 0;
        int l3  = gpio_get_output_level(3U);  if (l3 < 0)  l3 = 0;
        int l8  = gpio_get_output_level(8U);  if (l8 < 0)  l8 = 0;

        http_str_append(response_body, max_len, s_str_gpio_pins_pre);
        http_str_append(response_body, max_len, s_str_gpio_p15);
        http_u32_to_dec((uint32_t)l15, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_gpio_n15);

        http_str_append(response_body, max_len, s_str_gpio_p2);
        http_u32_to_dec((uint32_t)l2, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_gpio_n2);

        http_str_append(response_body, max_len, s_str_gpio_p3);
        http_u32_to_dec((uint32_t)l3, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_gpio_n3);

        http_str_append(response_body, max_len, s_str_gpio_p8);
        http_u32_to_dec((uint32_t)l8, num_buf, sizeof(num_buf));
        http_str_append(response_body, max_len, num_buf);
        http_str_append(response_body, max_len, s_str_gpio_n8);
        http_str_append(response_body, max_len, s_str_close_bracket);
    }
}

static __attribute__((noinline)) void http_register_default_routes(void)
{
    http_route_register("/", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/index.html", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/favicon.ico", HTTP_METHOD_GET, http_handler_favicon);
    http_route_register("/generate_204", HTTP_METHOD_GET, http_handler_connectivity_probe);
    http_route_register("/gen_204", HTTP_METHOD_GET, http_handler_connectivity_probe);
    http_route_register("/hotspot-detect.html", HTTP_METHOD_GET, http_handler_connectivity_probe);
    http_route_register("/ncsi.txt", HTTP_METHOD_GET, http_handler_connectivity_probe);
    http_route_register("/connecttest.txt", HTTP_METHOD_GET, http_handler_connectivity_probe);
    http_route_register("/canonical.html", HTTP_METHOD_GET, http_handler_connectivity_probe);
    http_route_register("/api/status", HTTP_METHOD_GET, http_handler_status);
    http_route_register("/api/info", HTTP_METHOD_GET, http_handler_info);
    http_route_register("/api/telemetry", HTTP_METHOD_GET, http_handler_telemetry);
    http_route_register("/api/wdt/feed", HTTP_METHOD_POST, http_handler_wdt_feed);
    http_route_register("/api/health", HTTP_METHOD_GET, http_handler_health);
    http_route_register("/api/speedtest", HTTP_METHOD_GET, http_handler_speedtest);
    http_route_register("/api/speedtest", HTTP_METHOD_POST, http_handler_speedtest);
    http_route_register("/api/gpio", HTTP_METHOD_GET, http_handler_gpio);
    http_route_register("/api/gpio", HTTP_METHOD_POST, http_handler_gpio);
}

/* ========================================================================= */
/* Core Lifecycle & Routing Implementations                                  */
/* ========================================================================= */

http_status_t http_server_init(void)
{
    s_http_route_count = 0U;
    memset(s_http_routes, 0, sizeof(s_http_routes));
    memset(&s_http_telemetry, 0, sizeof(s_http_telemetry));
    s_http_listener_pcb = NULL;

    http_register_default_routes();

    s_http_telemetry.active_routes = s_http_route_count;
    s_http_telemetry.server_running = false;
    s_http_initialized = true;

    return HTTP_OK;
}

http_status_t http_route_register(const char *path, http_method_t method, http_route_handler_t handler)
{
    if (path == NULL || handler == NULL)
    {
        return HTTP_ERR_INVALID_ARG;
    }

    /* Re-registering a path/method replaces its handler (re-init must not leak slots) */
    for (uint32_t i = 0U; i < s_http_route_count; i++)
    {
        if (s_http_routes[i].method == method && strcmp(s_http_routes[i].path, path) == 0)
        {
            s_http_routes[i].handler = handler;
            return HTTP_OK;
        }
    }

    if (s_http_route_count >= HTTP_MAX_ROUTES)
    {
        return HTTP_ERR_ROUTE_TABLE_FULL;
    }

    s_http_routes[s_http_route_count].path    = path;
    s_http_routes[s_http_route_count].method  = method;
    s_http_routes[s_http_route_count].handler = handler;
    s_http_route_count++;

    s_http_telemetry.active_routes = s_http_route_count;
    return HTTP_OK;
}

const http_route_t *http_route_find(const char *path, http_method_t method, bool *out_path_matched)
{
    if (out_path_matched != NULL)
    {
        *out_path_matched = false;
    }

    if (path == NULL)
    {
        return NULL;
    }

    for (uint16_t i = 0U; i < s_http_route_count; i++)
    {
        if (strcmp(s_http_routes[i].path, path) == 0)
        {
            if (out_path_matched != NULL)
            {
                *out_path_matched = true;
            }
            if (s_http_routes[i].method == method)
            {
                return &s_http_routes[i];
            }
        }
    }

    return NULL;
}

/* ========================================================================= */
/* Request & Response Processing Engine                                      */
/* ========================================================================= */

http_status_t http_process_request(const char *raw_request, size_t req_len,
                                   char *out_response, size_t max_resp_len,
                                   size_t *out_resp_len)
{
    if (raw_request == NULL || req_len == 0U || out_response == NULL ||
        max_resp_len == 0U || out_resp_len == NULL)
    {
        return HTTP_ERR_INVALID_ARG;
    }

    if (!s_http_initialized)
    {
        http_server_init();
    }

    s_http_telemetry.requests_total++;
    s_http_telemetry.bytes_rx += (uint32_t)req_len;

    /* 1. Parse Request Method */
    http_method_t method = HTTP_METHOD_UNKNOWN;
    size_t pos = 0U;

    if (req_len >= 4U && strncmp(raw_request, "GET ", 4) == 0)
    {
        method = HTTP_METHOD_GET;
        pos = 4U;
        s_http_telemetry.requests_get++;
    }
    else if (req_len >= 5U && strncmp(raw_request, "POST ", 5) == 0)
    {
        method = HTTP_METHOD_POST;
        pos = 5U;
        s_http_telemetry.requests_post++;
    }
    else if (req_len >= 5U && strncmp(raw_request, "HEAD ", 5) == 0)
    {
        method = HTTP_METHOD_HEAD;
        pos = 5U;
    }
    else if (req_len >= 8U && strncmp(raw_request, "OPTIONS ", 8) == 0)
    {
        method = HTTP_METHOD_OPTIONS;
        pos = 8U;
    }
    else
    {
        /* Find space to see if it was an unknown method or malformed line */
        size_t sp = 0U;
        while (sp < req_len && raw_request[sp] != ' ' && raw_request[sp] != '\r' && raw_request[sp] != '\n')
        {
            sp++;
        }
        if (sp < req_len && raw_request[sp] == ' ')
        {
            method = HTTP_METHOD_UNKNOWN;
            pos = sp + 1U;
        }
        else
        {
            /* Totally malformed request line -> 400 Bad Request */
            s_http_telemetry.responses_err++;
            const char bad_resp[] =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: 25\r\n"
                "Connection: close\r\n"
                HTTP_SERVER_HEADER
                "\r\n"
                "{\"error\":\"bad_request\"}\r\n";
            size_t blen = strlen(bad_resp);
            if (blen >= max_resp_len)
            {
                blen = max_resp_len - 1U;
            }
            memcpy(out_response, bad_resp, blen);
            out_response[blen] = '\0';
            *out_resp_len = blen;
            s_http_telemetry.bytes_tx += (uint32_t)blen;
            return HTTP_ERR_MALFORMED;
        }
    }

    while (pos < req_len && raw_request[pos] == ' ')
    {
        pos++;
    }

    /* 2. Parse Path and Query Parameters */
    char path_buf[HTTP_MAX_PATH_LEN];
    char query_buf[HTTP_MAX_QUERY_LEN];
    size_t path_len  = 0U;
    size_t query_len = 0U;
    bool in_query    = false;

    while (pos < req_len && raw_request[pos] != ' ' &&
           raw_request[pos] != '\r' && raw_request[pos] != '\n')
    {
        char c = raw_request[pos++];
        if (c == '?' && !in_query)
        {
            in_query = true;
            continue;
        }

        if (!in_query)
        {
            if (path_len < sizeof(path_buf) - 1U)
            {
                path_buf[path_len++] = c;
            }
        }
        else
        {
            if (query_len < sizeof(query_buf) - 1U)
            {
                query_buf[query_len++] = c;
            }
        }
    }

    path_buf[path_len]   = '\0';
    query_buf[query_len] = '\0';

    /* For POST requests without query string in URL, extract payload body */
    if (method == HTTP_METHOD_POST && query_len == 0U)
    {
        const char *body_marker = strstr(raw_request, "\r\n\r\n");
        if (body_marker != NULL)
        {
            body_marker += 4;
        }
        else
        {
            body_marker = strstr(raw_request, "\n\n");
            if (body_marker != NULL)
            {
                body_marker += 2;
            }
        }

        if (body_marker != NULL)
        {
            size_t blen = strlen(body_marker);
            if (blen >= sizeof(query_buf))
            {
                blen = sizeof(query_buf) - 1U;
            }
            memcpy(query_buf, body_marker, blen);
            query_buf[blen] = '\0';
        }
    }

    if (path_len == 0U)
    {
        /* Malformed path */
        s_http_telemetry.responses_err++;
        const char bad_resp[] =
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 25\r\n"
            "Connection: close\r\n"
            HTTP_SERVER_HEADER
            "\r\n"
            "{\"error\":\"bad_request\"}\r\n";
        size_t blen = strlen(bad_resp);
        if (blen >= max_resp_len)
        {
            blen = max_resp_len - 1U;
        }
        memcpy(out_response, bad_resp, blen);
        out_response[blen] = '\0';
        *out_resp_len = blen;
        s_http_telemetry.bytes_tx += (uint32_t)blen;
        return HTTP_ERR_MALFORMED;
    }

    /* 3. Handle OPTIONS CORS Preflight Requests */
    if (method == HTTP_METHOD_OPTIONS)
    {
        s_http_telemetry.responses_200++;
        size_t clen = strlen(s_cors_preflight_resp);
        if (clen >= max_resp_len)
        {
            clen = max_resp_len - 1U;
        }
        memcpy(out_response, s_cors_preflight_resp, clen);
        out_response[clen] = '\0';
        *out_resp_len = clen;
        s_http_telemetry.bytes_tx += (uint32_t)clen;
        return HTTP_OK;
    }

    /* 4. Match Route */
    bool path_matched = false;
    const http_route_t *route = http_route_find(path_buf, method, &path_matched);

    char body_buf[HTTP_BODY_MAX_LEN];
    body_buf[0] = '\0';
    const char *status_line = "HTTP/1.1 200 OK\r\n";
    bool captive_redirect = false;
    const char *content_type = HTTP_MIME_JSON;
    http_status_t ret_status = HTTP_OK;

    if (route != NULL)
    {
        /* Matched route and method -> execute handler */
        route->handler(query_buf, body_buf, sizeof(body_buf));
        s_http_telemetry.responses_200++;

        if (http_is_connectivity_probe(path_buf))
        {
            /* The SoftAP has no internet: never answer "connected" */
#if CONFIG_SOFTAP_CAPTIVE_PORTAL
            status_line = HTTP_STATUS_LINE_302;   /* send the OS to the portal */
            captive_redirect = true;
#else
            status_line = HTTP_STATUS_LINE_404;   /* plain "no internet" */
#endif
            content_type = HTTP_MIME_TEXT;
            body_buf[0] = '\0';
        }
        else if (strcmp(path_buf, "/") == 0 || strcmp(path_buf, "/index.html") == 0 ||
                 strcmp(path_buf, "/setup") == 0)
        {
            content_type = HTTP_MIME_HTML;
        }
    }
    else if (path_matched)
    {
        /* Path exists but method is not allowed -> 405 */
        status_line = "HTTP/1.1 405 Method Not Allowed\r\n";
        http_str_append(body_buf, sizeof(body_buf), "{\"error\":\"method_not_allowed\"}\r\n");
        s_http_telemetry.responses_405++;
        ret_status = HTTP_ERR_METHOD_NOT_ALLOWED;
    }
    else
    {
        /* Path not found -> 404 */
        status_line = "HTTP/1.1 404 Not Found\r\n";
        http_str_append(body_buf, sizeof(body_buf), "{\"error\":\"not_found\"}\r\n");
        s_http_telemetry.responses_404++;
        ret_status = HTTP_ERR_NOT_FOUND;
    }

    /* 5. Format HTTP/1.1 Response */
    size_t body_len = strlen(body_buf);
    char len_buf[16];
    http_u32_to_dec((uint32_t)body_len, len_buf, sizeof(len_buf));

    out_response[0] = '\0';
    http_str_append(out_response, max_resp_len, status_line);
    if (captive_redirect)
    {
        http_append_portal_location(out_response, max_resp_len);
    }
    http_str_append(out_response, max_resp_len, HTTP_SERVER_HEADER);
    http_str_append(out_response, max_resp_len, HTTP_CONN_CLOSE_HEADER);
    http_str_append(out_response, max_resp_len, "Access-Control-Allow-Origin: *\r\n");
    http_str_append(out_response, max_resp_len, s_cors_methods_hdr);
    http_str_append(out_response, max_resp_len, s_cors_headers_hdr);
    http_str_append(out_response, max_resp_len, "Content-Type: ");
    http_str_append(out_response, max_resp_len, content_type);
    http_str_append(out_response, max_resp_len, "\r\nContent-Length: ");
    http_str_append(out_response, max_resp_len, len_buf);
    http_str_append(out_response, max_resp_len, "\r\n\r\n");

    if (method != HTTP_METHOD_HEAD)
    {
        http_str_append(out_response, max_resp_len, body_buf);
    }

    size_t resp_total_len = strlen(out_response);
    *out_resp_len = resp_total_len;
    s_http_telemetry.bytes_tx += (uint32_t)resp_total_len;

    return ret_status;
}

/* ========================================================================= */
/* TCP Socket Integration (RFC 793 Callbacks)                                */
/* ========================================================================= */

/* A request being assembled from TCP segments; one per connection */
typedef struct {
    tcp_pcb_t *pcb;            /* NULL: free */
    size_t     len;
    char       buf[HTTP_REQUEST_BUF_SIZE];
} http_req_slot_t;

static http_req_slot_t s_http_req_slots[HTTP_REQUEST_SLOTS];

static bool http_ascii_prefix_nocase(const char *s, const char *prefix)
{
    for (; *prefix != '\0'; s++, prefix++)
    {
        char c = *s;
        if (c >= 'A' && c <= 'Z')
        {
            c = (char)(c - 'A' + 'a');
        }
        if (c != *prefix)
        {
            return false;
        }
    }
    return true;
}

/* Complete once the blank line after the headers and Content-Length body bytes are in */
http_req_state_t http_request_state(const char *buf, size_t len)
{
    if (buf == NULL)
    {
        return HTTP_REQ_INCOMPLETE;
    }
    const char *hdr_end = strstr(buf, HTTP_HEADER_END);
    if (hdr_end == NULL)
    {
        return (len >= HTTP_REQUEST_BUF_SIZE - 1U) ? HTTP_REQ_TOO_LARGE : HTTP_REQ_INCOMPLETE;
    }
    size_t body_start = (size_t)(hdr_end - buf) + strlen(HTTP_HEADER_END);
    size_t content_len = 0U;
    for (const char *line = buf; line != NULL && line < hdr_end; )
    {
        if (http_ascii_prefix_nocase(line, HTTP_CONTENT_LENGTH_HEADER))
        {
            const char *v = line + strlen(HTTP_CONTENT_LENGTH_HEADER);
            while (*v == ' ' || *v == '\t') v++;
            while (*v >= '0' && *v <= '9' && content_len < HTTP_REQUEST_BUF_SIZE)
            {
                content_len = content_len * 10U + (size_t)(*v - '0');
                v++;
            }
        }
        line = strstr(line, "\r\n");
        if (line != NULL)
        {
            line += 2;
        }
    }
    if (body_start + content_len >= HTTP_REQUEST_BUF_SIZE)
    {
        return HTTP_REQ_TOO_LARGE;
    }
    return (len >= body_start + content_len) ? HTTP_REQ_COMPLETE : HTTP_REQ_INCOMPLETE;
}

/* Slot of this connection, or a free one (slots of closed connections are reclaimed) */
static http_req_slot_t *http_req_slot_get(tcp_pcb_t *pcb)
{
    http_req_slot_t *free_slot = NULL;
    for (uint32_t i = 0U; i < HTTP_REQUEST_SLOTS; i++)
    {
        http_req_slot_t *slot = &s_http_req_slots[i];
        if (slot->pcb == pcb)
        {
            return slot;
        }
        if (slot->pcb != NULL && (!slot->pcb->in_use || slot->pcb->state != TCP_STATE_ESTABLISHED))
        {
            slot->pcb = NULL;
        }
        if (slot->pcb == NULL && free_slot == NULL)
        {
            free_slot = slot;
        }
    }
    if (free_slot != NULL)
    {
        free_slot->pcb = pcb;
        free_slot->len = 0U;
        free_slot->buf[0] = '\0';
    }
    return free_slot;
}

static void http_req_slot_release(tcp_pcb_t *pcb)
{
    for (uint32_t i = 0U; i < HTTP_REQUEST_SLOTS; i++)
    {
        if (s_http_req_slots[i].pcb == pcb)
        {
            s_http_req_slots[i].pcb = NULL;
            s_http_req_slots[i].len = 0U;
        }
    }
}

static net_status_t http_tcp_recv_cb(void *arg, tcp_pcb_t *pcb, const uint8_t *data, uint16_t len)
{
    (void)arg;
    if (pcb == NULL || data == NULL || len == 0U)
    {
        return NET_ERR_INVALID_ARG;
    }

    /* Collect segments until the request is complete; parse a NUL-terminated copy */
    http_req_slot_t *slot = http_req_slot_get(pcb);
    if (slot == NULL)
    {
        tcp_close(pcb);
        return NET_ERR_BUFFER_TOO_SMALL;
    }
    size_t room = HTTP_REQUEST_BUF_SIZE - 1U - slot->len;
    size_t take = ((size_t)len < room) ? (size_t)len : room;
    memcpy(&slot->buf[slot->len], data, take);
    slot->len += take;
    slot->buf[slot->len] = '\0';

    http_req_state_t rst = http_request_state(slot->buf, slot->len);
    if (rst == HTTP_REQ_INCOMPLETE && take == (size_t)len)
    {
        return NET_OK;
    }

#if defined(__riscv)
    console_puts("[HTTP] ");
    for (size_t i = 0U; i < slot->len && slot->buf[i] != '\r' && slot->buf[i] != '\n' && i < 64U; i++) {
        console_putc(slot->buf[i]);
    }
    console_puts("\r\n");
#endif

    arena_scratch_mark_t mark = arena_scratch_mark();
    char *tx_buf = (char *)arena_scratch_alloc(HTTP_RESPONSE_BUF_SIZE);
    if (tx_buf == NULL)
    {
        http_req_slot_release(pcb);
        return NET_ERR_BUFFER_TOO_SMALL;
    }

    size_t out_len = 0U;
    if (rst == HTTP_REQ_COMPLETE)
    {
        http_process_request(slot->buf, slot->len, tx_buf, HTTP_RESPONSE_BUF_SIZE, &out_len);
    }
    else
    {
        const char too_large[] =
            "HTTP/1.1 413 Payload Too Large\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            HTTP_SERVER_HEADER
            "\r\n";
        out_len = strlen(too_large);
        memcpy(tx_buf, too_large, out_len);
    }

    if (out_len > 0U)
    {
        tcp_status_t wst = tcp_write(pcb, tx_buf, (uint16_t)out_len);
        const char get_prefix[] = "GET ";
        bool idempotent = rst == HTTP_REQ_COMPLETE && slot->len >= (sizeof(get_prefix) - 1U) &&
                          strncmp(slot->buf, get_prefix, sizeof(get_prefix) - 1U) == 0;
        if (wst == TCP_ERR_MEM && idempotent)
        {
            /* TCP send buffer busy with other responses (REV-13): leave this segment unacknowledged;
             * the client resends it once earlier responses are acknowledged */
            slot->len -= take;
            slot->buf[slot->len] = '\0';
            s_http_telemetry.requests_deferred++;
            arena_scratch_reset(mark);
            return NET_ERR_BUSY;
        }
        http_req_slot_release(pcb);
        if (wst == TCP_ERR_MEM)
        {
            /* Not repeatable (POST already applied): a short 503 instead of nothing */
            const char busy[] =
                "HTTP/1.1 503 Service Unavailable\r\n"
                "Retry-After: 1\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                HTTP_SERVER_HEADER
                "\r\n";
            s_http_telemetry.responses_err++;
            if (tcp_write(pcb, busy, (uint16_t)strlen(busy)) != TCP_OK)
            {
                arena_scratch_reset(mark);
                tcp_abort(pcb);
                return NET_OK;
            }
        }
#if defined(__riscv)
        console_puts((wst == TCP_OK) ? "[HTTP] Sent response: " : "[HTTP] Send buffer full, 503 for: ");
        put_dec((uint32_t)out_len);
        console_puts(" bytes\r\n");
#endif
    }

    else
    {
        http_req_slot_release(pcb);
    }

    arena_scratch_reset(mark);

    /* HTTP/1.1 Connection: close */
    tcp_close(pcb);
    return NET_OK;
}

static net_status_t http_tcp_accept_cb(void *arg, tcp_pcb_t *newpcb)
{
    (void)arg;
    if (newpcb == NULL)
    {
        return NET_ERR_INVALID_ARG;
    }

#if defined(__riscv)
    console_puts("[HTTP] Client connected\r\n");
#endif

    http_req_slot_release(newpcb);   /* pcb reused for a new connection */
    newpcb->recv_cb = http_tcp_recv_cb;
    return NET_OK;
}

http_status_t http_server_start(uint16_t port)
{
    if (!s_http_initialized)
    {
        http_server_init();
    }

    if (s_http_telemetry.server_running)
    {
        return HTTP_OK;
    }

    if (port == 0U)
    {
        port = HTTP_SERVER_DEFAULT_PORT;
    }

    s_http_listener_pcb = tcp_new();
    if (s_http_listener_pcb == NULL)
    {
        return HTTP_ERR_SOCKET;
    }

    tcp_status_t bst = tcp_bind(s_http_listener_pcb, port);
    if (bst != TCP_OK)
    {
        tcp_close(s_http_listener_pcb);
        s_http_listener_pcb = NULL;
        return HTTP_ERR_SOCKET;
    }

    tcp_status_t lst = tcp_listen(s_http_listener_pcb, http_tcp_accept_cb);
    if (lst != TCP_OK)
    {
        tcp_close(s_http_listener_pcb);
        s_http_listener_pcb = NULL;
        return HTTP_ERR_SOCKET;
    }

    /* Attach recv callback so incoming connections inherit it */
    s_http_listener_pcb->recv_cb = http_tcp_recv_cb;
    s_http_telemetry.server_running = true;
    return HTTP_OK;
}

http_status_t http_server_stop(void)
{
    if (!s_http_telemetry.server_running)
    {
        return HTTP_OK;
    }

    if (s_http_listener_pcb != NULL)
    {
        tcp_close(s_http_listener_pcb);
        s_http_listener_pcb = NULL;
    }

    s_http_telemetry.server_running = false;
    return HTTP_OK;
}

bool http_server_is_running(void)
{
    return s_http_telemetry.server_running;
}

/* ========================================================================= */
/* Status & Telemetry Accessors                                              */
/* ========================================================================= */

http_status_t http_server_get_telemetry(http_telemetry_t *out_telemetry)
{
    if (out_telemetry == NULL)
    {
        return HTTP_ERR_INVALID_ARG;
    }

    memcpy(out_telemetry, &s_http_telemetry, sizeof(http_telemetry_t));
    return HTTP_OK;
}

uint16_t http_server_get_route_count(void)
{
    return s_http_route_count;
}

const char *http_method_to_str(http_method_t method)
{
    switch (method)
    {
        case HTTP_METHOD_GET:     return "GET";
        case HTTP_METHOD_POST:    return "POST";
        case HTTP_METHOD_HEAD:    return "HEAD";
        case HTTP_METHOD_OPTIONS: return "OPTIONS";
        case HTTP_METHOD_UNKNOWN: return "UNKNOWN";
        default:                  return "INVALID";
    }
}

const char *http_status_to_str(http_status_code_t code)
{
    switch (code)
    {
        case HTTP_STATUS_200_OK:                 return "200 OK";
        case HTTP_STATUS_204_NO_CONTENT:         return "204 No Content";
        case HTTP_STATUS_400_BAD_REQUEST:        return "400 Bad Request";
        case HTTP_STATUS_404_NOT_FOUND:          return "404 Not Found";
        case HTTP_STATUS_405_METHOD_NOT_ALLOWED: return "405 Method Not Allowed";
        case HTTP_STATUS_500_INTERNAL_ERROR:     return "500 Internal Server Error";
        default:                                 return "UNKNOWN";
    }
}
