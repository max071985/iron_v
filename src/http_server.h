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
#define HTTP_MAX_ROUTES                  24U
#define HTTP_MAX_PATH_LEN                64U
#define HTTP_MAX_QUERY_LEN               256U
#define HTTP_REQUEST_BUF_SIZE            1024U
#define HTTP_RESPONSE_BUF_SIZE           2048U
#define HTTP_BODY_MAX_LEN                1600U

/* Protocol Version & Header Literals */
#define HTTP_VERSION_STR                 "HTTP/1.1"
#define HTTP_SERVER_HEADER               "Server: Iron-V-BareMetal\r\n"
#define HTTP_CONN_CLOSE_HEADER           "Connection: close\r\n"

/* Captive portal: OS connectivity probes are redirected to the setup page so phones
 * report "sign in to network" instead of believing the SoftAP has internet access. */
#define HTTP_STATUS_LINE_302             "HTTP/1.1 302 Found\r\n"
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

#endif /* IRON_V_HTTP_SERVER_H */
