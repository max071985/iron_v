/*
 * Iron V - REST v1 (REV-23), core part (REV-33): JSON helpers for the modules' resources and
 * /api/v1/device. See api_v1.h.
 */
#include "api_v1.h"
#include "http_server.h"
#include "device.h"
#include "module.h"
#include "net.h"
#include "systimer.h"
#include "config.h"
#include "string.h"

#define API_V1_DEC_BUF_LEN               12U
#define API_V1_DEC_BASE                  10U
#define API_V1_US_PER_S                  1000000ULL
#define API_V1_ASCII_PRINTABLE_MIN       0x20

void api_v1_append(char *buf, size_t max, const char *src)
{
    size_t pos = strlen(buf);
    size_t n = strlen(src);
    if (pos + n + 1U > max)
    {
        return;
    }
    memcpy(buf + pos, src, n + 1U);
}

void api_v1_append_u32(char *buf, size_t max, uint32_t v)
{
    char tmp[API_V1_DEC_BUF_LEN];
    char out[API_V1_DEC_BUF_LEN];
    size_t n = 0U;
    do
    {
        tmp[n++] = (char)('0' + (v % API_V1_DEC_BASE));
        v /= API_V1_DEC_BASE;
    } while (v != 0U && n < sizeof(tmp) - 1U);
    for (size_t i = 0U; i < n; i++)
    {
        out[i] = tmp[n - 1U - i];
    }
    out[n] = '\0';
    api_v1_append(buf, max, out);
}

/* JSON string body: quotes and backslashes escaped, control characters dropped */
void api_v1_append_escaped(char *buf, size_t max, const char *src)
{
    char one[3];
    for (; *src != '\0'; src++)
    {
        if ((unsigned char)*src < API_V1_ASCII_PRINTABLE_MIN)
        {
            continue;
        }
        if (*src == '"' || *src == '\\')
        {
            one[0] = '\\';
            one[1] = *src;
            one[2] = '\0';
        }
        else
        {
            one[0] = *src;
            one[1] = '\0';
        }
        api_v1_append(buf, max, one);
    }
}

void api_v1_append_ip(char *buf, size_t max, uint32_t ip)
{
    char s[NET_IP_STR_BUF_LEN];
    net_ip_to_str(ip, s, sizeof(s));
    api_v1_append(buf, max, s);
}

void api_v1_error(char *body, size_t max_len, const char *reason)
{
    http_response_set_status(HTTP_STATUS_400_BAD_REQUEST);
    body[0] = '\0';
    api_v1_append(body, max_len, "{\"error\":\"");
    api_v1_append(body, max_len, reason);
    api_v1_append(body, max_len, "\"}\r\n");
}

/* ========================================================================= */
/* /api/v1/device                                                            */
/* ========================================================================= */

void api_v1_device_get(const char *query, char *body, size_t max_len)
{
    (void)query;
    if (body == NULL || max_len == 0U)
    {
        return;
    }
    net_config_t nc;
    net_get_config(&nc);

    body[0] = '\0';
    api_v1_append(body, max_len, "{\"device_id\":\"");
    api_v1_append(body, max_len, device_id());
    api_v1_append(body, max_len, "\",\"hostname\":\"" CONFIG_DEVICE_HOSTNAME "\",\"fw\":\"" CONFIG_FIRMWARE_REVISION
                                 "\",\"profile\":\"" CONFIG_PROFILE "\",\"uptime_s\":");
    api_v1_append_u32(body, max_len, (uint32_t)(systimer_get_us() / API_V1_US_PER_S));
    api_v1_append(body, max_len, ",\"ip\":\"");
    api_v1_append_ip(body, max_len, nc.ip);
    api_v1_append(body, max_len, "\"");
    modules_device_json(body, max_len);   /* e.g. the MQTT session (REV-33) */
    api_v1_append(body, max_len, "}\r\n");
}

void api_v1_register_routes(void)
{
    (void)http_route_register(API_V1_PATH_DEVICE, HTTP_METHOD_GET, api_v1_device_get);
}
