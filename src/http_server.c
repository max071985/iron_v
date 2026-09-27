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
#include "web_assets.h"
#include "arena.h"
#include "string.h"
#include "wdt.h"
#include "clock.h"

#if defined(__riscv)
#include "systimer.h"
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

/* ========================================================================= */
/* Core Lifecycle & Routing Implementations                                  */
/* ========================================================================= */

http_status_t http_server_init(void)
{
    s_http_route_count = 0U;
    memset(s_http_routes, 0, sizeof(s_http_routes));
    memset(&s_http_telemetry, 0, sizeof(s_http_telemetry));
    s_http_listener_pcb = NULL;

    /* Register standard routes */
    http_route_register("/", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/index.html", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/generate_204", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/gen_204", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/hotspot-detect.html", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/ncsi.txt", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/connecttest.txt", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/canonical.html", HTTP_METHOD_GET, http_handler_root);
    http_route_register("/api/status", HTTP_METHOD_GET, http_handler_status);
    http_route_register("/api/info", HTTP_METHOD_GET, http_handler_info);
    http_route_register("/api/telemetry", HTTP_METHOD_GET, http_handler_telemetry);
    http_route_register("/api/wdt/feed", HTTP_METHOD_POST, http_handler_wdt_feed);

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

    /* 3. Match Route */
    bool path_matched = false;
    const http_route_t *route = http_route_find(path_buf, method, &path_matched);

    char body_buf[HTTP_BODY_MAX_LEN];
    body_buf[0] = '\0';
    const char *status_line = "HTTP/1.1 200 OK\r\n";
    const char *content_type = HTTP_MIME_JSON;
    http_status_t ret_status = HTTP_OK;

    if (route != NULL)
    {
        /* Matched route and method -> execute handler */
        route->handler(query_buf, body_buf, sizeof(body_buf));
        s_http_telemetry.responses_200++;

        if (strcmp(path_buf, "/") == 0 || strcmp(path_buf, "/index.html") == 0 ||
            strcmp(path_buf, "/generate_204") == 0 || strcmp(path_buf, "/gen_204") == 0 ||
            strcmp(path_buf, "/hotspot-detect.html") == 0 || strcmp(path_buf, "/canonical.html") == 0)
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

    /* 4. Format HTTP/1.1 Response */
    size_t body_len = strlen(body_buf);
    char len_buf[16];
    http_u32_to_dec((uint32_t)body_len, len_buf, sizeof(len_buf));

    out_response[0] = '\0';
    http_str_append(out_response, max_resp_len, status_line);
    http_str_append(out_response, max_resp_len, HTTP_SERVER_HEADER);
    http_str_append(out_response, max_resp_len, HTTP_CONN_CLOSE_HEADER);
    http_str_append(out_response, max_resp_len, "Access-Control-Allow-Origin: *\r\n");
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

static net_status_t http_tcp_recv_cb(void *arg, tcp_pcb_t *pcb, const uint8_t *data, uint16_t len)
{
    (void)arg;
    if (pcb == NULL || data == NULL || len == 0U)
    {
        return NET_ERR_INVALID_ARG;
    }

    arena_scratch_mark_t mark = arena_scratch_mark();
    char *tx_buf = (char *)arena_scratch_alloc(HTTP_RESPONSE_BUF_SIZE);
    if (tx_buf == NULL)
    {
        return NET_ERR_BUFFER_TOO_SMALL;
    }

    size_t out_len = 0U;
    http_process_request((const char *)data, (size_t)len,
                         tx_buf, HTTP_RESPONSE_BUF_SIZE,
                         &out_len);

    if (out_len > 0U)
    {
        tcp_write(pcb, tx_buf, (uint16_t)out_len);
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
