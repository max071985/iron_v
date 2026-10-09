/*
 * src/http_server.h
 *
 * Zero-Allocation Local REST/HTTP Engine & Embedded Web Server
 * RFC 2616 (HTTP/1.1) & RFC 7230 / RFC 7231 (HTTP Semantics)
 *
 * Implements deterministic static route tables, zero-copy HTTP/1.1 request
 * parser, JSON REST endpoints, and static HTML asset delivery over bare-metal TCP.
 */

#ifndef IRON_V_HTTP_SERVER_H
#define IRON_V_HTTP_SERVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"
#include "tcp.h"

/* ========================================================================= */
/* Server Sizing & Protocol Constants (Zero Dynamic Heap Allocation)         */
/* ========================================================================= */
#define HTTP_SERVER_DEFAULT_PORT         CONFIG_TCP_DEFAULT_HTTP_PORT
#define HTTP_MAX_ROUTES                  28U   /* 25 used after REV-23 (v1 API) */
#define HTTP_MAX_PATH_LEN                64U
#define HTTP_MAX_QUERY_LEN               256U
#define HTTP_REQUEST_BUF_SIZE            1536U   /* one assembled request: phone headers + JSON body */
#define HTTP_REQUEST_SLOTS               2U      /* requests being assembled at the same time */
#define HTTP_HEADER_END                  "\r\n\r\n"
#define HTTP_CONTENT_LENGTH_HEADER       "content-length:"
#define HTTP_RESPONSE_BUF_SIZE           4096U   /* headers + body; from the scratch arena */
#define HTTP_BODY_MAX_LEN                3584U   /* largest page: /setup (REV-29) */
#define HTTP_HEADER_RESERVE              512U    /* upper bound of the response headers we write */

/* Protocol Version & Header Literals */
#define HTTP_VERSION_STR                 "HTTP/1.1"
#define HTTP_SERVER_HEADER               "Server: Iron-V-BareMetal\r\n"
#define HTTP_CONN_CLOSE_HEADER           "Connection: close\r\n"
#define HTTP_GZIP_HEADER                 "Content-Encoding: gzip\r\n"

/* Captive portal: OS connectivity probes are redirected to the setup page so phones
 * report "sign in to network" instead of believing the SoftAP has internet access. */
#define HTTP_STATUS_LINE_302             "HTTP/1.1 302 Found\r\n"
#define HTTP_STATUS_LINE_400             "HTTP/1.1 400 Bad Request\r\n"
#define HTTP_STATUS_LINE_404             "HTTP/1.1 404 Not Found\r\n"
#define HTTP_STATUS_LINE_500             "HTTP/1.1 500 Internal Server Error\r\n"
#define HTTP_CAPTIVE_PORTAL_PATH         "/setup"
#define HTTP_MIME_JSON                   "application/json"
#define HTTP_MIME_HTML                   "text/html; charset=utf-8"
#define HTTP_MIME_TEXT                   "text/plain"

/* ========================================================================= */
/* HTTP Enumerations                                                         */
/* ========================================================================= */
typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_HEAD,
    HTTP_METHOD_OPTIONS,
    HTTP_METHOD_UNKNOWN
} http_method_t;

typedef enum {
    HTTP_STATUS_200_OK               = 200,
    HTTP_STATUS_204_NO_CONTENT       = 204,
    HTTP_STATUS_400_BAD_REQUEST      = 400,
    HTTP_STATUS_404_NOT_FOUND        = 404,
    HTTP_STATUS_405_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_500_INTERNAL_ERROR   = 500
} http_status_code_t;

typedef enum {
    HTTP_OK                          = 0,
    HTTP_ERR_INVALID_ARG             = -1,
    HTTP_ERR_BUFFER_TOO_SMALL        = -2,
    HTTP_ERR_NOT_FOUND               = -3,
    HTTP_ERR_METHOD_NOT_ALLOWED      = -4,
    HTTP_ERR_MALFORMED               = -5,
    HTTP_ERR_ROUTE_TABLE_FULL        = -6,
    HTTP_ERR_NOT_INITIALIZED         = -7,
    HTTP_ERR_SOCKET                  = -8
} http_status_t;

/* ========================================================================= */
/* Route Handler Callback & Route Definition                                 */
/* ========================================================================= */
typedef void (*http_route_handler_t)(const char *query_params, char *response_body, size_t max_len);

typedef struct {
    const char *path;
    http_method_t method;
    void (*handler)(const char *query_params, char *response_body, size_t max_len);
} http_route_t;

/* ========================================================================= */
/* HTTP Subsystem Telemetry Structure                                        */
/* ========================================================================= */
typedef struct {
    uint32_t requests_total;
    uint32_t requests_get;
    uint32_t requests_post;
    uint32_t responses_200;
    uint32_t responses_404;
    uint32_t responses_405;
    uint32_t responses_err;
    uint32_t requests_deferred;    /* GETs left unacknowledged while the TCP send buffer was full */
    uint32_t bytes_tx;
    uint32_t bytes_rx;
    uint16_t active_routes;
    bool     server_running;
} http_telemetry_t;

/* ========================================================================= */
/* Public HTTP Server APIs                                                   */
/* ========================================================================= */

/* Core lifecycle initialization & control */
http_status_t http_server_init(void);
http_status_t http_server_start(uint16_t port);
http_status_t http_server_stop(void);
bool http_server_is_running(void);

/* Static Route Registration & Lookup */
http_status_t http_route_register(const char *path, http_method_t method, http_route_handler_t handler);
/* Called by a handler to answer with another status than 200 (REV-23: 400 for rejected input) */
void http_response_set_status(http_status_code_t code);
/* Called by a handler to answer with a gzip-encoded body from flash instead of its text body (REV-25) */
void http_response_set_gzip_body(const uint8_t *data, size_t len);
const http_route_t *http_route_find(const char *path, http_method_t method, bool *out_path_matched);

/* Request & Response Processing */
http_status_t http_process_request(const char *raw_request, size_t req_len,
                                   char *out_response, size_t max_resp_len,
                                   size_t *out_resp_len);

/* Telemetry & Queries */
http_status_t http_server_get_telemetry(http_telemetry_t *out_telemetry);
uint16_t http_server_get_route_count(void);
const char *http_method_to_str(http_method_t method);
const char *http_status_to_str(http_status_code_t code);

/* Request assembly: browsers may send the headers and the body in separate TCP segments */
typedef enum {
    HTTP_REQ_INCOMPLETE = 0,   /* headers or Content-Length body bytes still missing */
    HTTP_REQ_COMPLETE,
    HTTP_REQ_TOO_LARGE         /* does not fit HTTP_REQUEST_BUF_SIZE */
} http_req_state_t;
http_req_state_t http_request_state(const char *buf, size_t len);

#endif /* IRON_V_HTTP_SERVER_H */
