/*
 * Iron V - REST v1 (REV-23). See api_v1.h.
 */
#include "api_v1.h"
#include "http_server.h"
#include "light.h"
#include "mqtt.h"
#include "json_lite.h"
#include "net.h"
#include "systimer.h"
#include "config.h"
#include "string.h"

#define API_V1_DEC_BUF_LEN               12U
#define API_V1_DEC_BASE                  10U
#define API_V1_US_PER_S                  1000000ULL
#define API_V1_PORT_MAX                  65535U
#define API_V1_ASCII_PRINTABLE_MIN       0x20

static void api_append(char *buf, size_t max, const char *src)
{
    size_t pos = strlen(buf);
    size_t n = strlen(src);
    if (pos + n + 1U > max)
    {
        return;
    }
    memcpy(buf + pos, src, n + 1U);
}

static void api_append_u32(char *buf, size_t max, uint32_t v)
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
    api_append(buf, max, out);
}

/* JSON string body: quotes and backslashes escaped, control characters dropped */
static void api_append_escaped(char *buf, size_t max, const char *src)
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
        api_append(buf, max, one);
    }
}

static void api_append_ip(char *buf, size_t max, uint32_t ip)
{
    char s[NET_IP_STR_BUF_LEN];
    net_ip_to_str(ip, s, sizeof(s));
    api_append(buf, max, s);
}

static void api_error(char *body, size_t max_len, const char *reason)
{
    http_response_set_status(HTTP_STATUS_400_BAD_REQUEST);
    body[0] = '\0';
    api_append(body, max_len, "{\"error\":\"");
    api_append(body, max_len, reason);
    api_append(body, max_len, "\"}\r\n");
}

/* ========================================================================= */
/* /api/v1/light                                                             */
/* ========================================================================= */

void api_v1_light_get(const char *query, char *body, size_t max_len)
{
    (void)query;
    if (body == NULL || max_len == 0U)
    {
        return;
    }
    light_state_t s;
    light_get(&s);
    body[0] = '\0';
    (void)light_state_to_json(&s, body, max_len);
    api_append(body, max_len, "\r\n");
}

void api_v1_light_post(const char *query, char *body, size_t max_len)
{
    if (body == NULL || max_len == 0U)
    {
        return;
    }
    const char *cmd = (query != NULL) ? query : "";
    light_cmd_status_t st = light_command(cmd, strlen(cmd), LIGHT_SRC_REST, systimer_get_us());
    if (st != LIGHT_CMD_OK)
    {
        api_error(body, max_len, light_cmd_status_str(st));
        return;
    }
    api_v1_light_get(NULL, body, max_len);
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
    mqtt_status_t ms;
    mqtt_get_status(&ms);
    mqtt_config_t mc;
    mqtt_get_config(&mc);

    body[0] = '\0';
    api_append(body, max_len, "{\"device_id\":\"");
    api_append(body, max_len, mqtt_device_id());
    api_append(body, max_len, "\",\"hostname\":\"" CONFIG_DEVICE_HOSTNAME "\",\"fw\":\"" CONFIG_FIRMWARE_REVISION
                              "\",\"uptime_s\":");
    api_append_u32(body, max_len, (uint32_t)(systimer_get_us() / API_V1_US_PER_S));
    api_append(body, max_len, ",\"ip\":\"");
    api_append_ip(body, max_len, nc.ip);
    api_append(body, max_len, "\",\"mqtt\":{\"state\":\"");
    api_append(body, max_len, mqtt_state_class_str(ms.state));
    api_append(body, max_len, "\",\"detail\":\"");
    api_append(body, max_len, mqtt_state_str(ms.state));
    api_append(body, max_len, "\",\"broker\":\"");
    if (mc.host != 0U)
    {
        api_append_ip(body, max_len, mc.host);
    }
    api_append(body, max_len, "\",\"last_error\":\"");
    api_append(body, max_len, mqtt_error_str(ms.last_error));
    api_append(body, max_len, "\",\"reconnects\":");
    api_append_u32(body, max_len, ms.reconnects);
    api_append(body, max_len, "}}\r\n");
}

/* ========================================================================= */
/* /api/v1/mqtt                                                              */
/* ========================================================================= */

void api_v1_mqtt_get(const char *query, char *body, size_t max_len)
{
    (void)query;
    if (body == NULL || max_len == 0U)
    {
        return;
    }
    mqtt_config_t mc;
    mqtt_get_config(&mc);
    body[0] = '\0';
    api_append(body, max_len, mc.enabled ? "{\"enabled\":true,\"host\":\"" : "{\"enabled\":false,\"host\":\"");
    if (mc.host != 0U)
    {
        api_append_ip(body, max_len, mc.host);
    }
    api_append(body, max_len, "\",\"port\":");
    api_append_u32(body, max_len, mc.port);
    api_append(body, max_len, ",\"user\":\"");
    api_append_escaped(body, max_len, mc.user);
    api_append(body, max_len, mc.pass[0] != '\0' ? "\",\"has_password\":true}\r\n" : "\",\"has_password\":false}\r\n");
}

/* Dotted quad only (net_str_to_ip stops at the first other character) */
static bool api_parse_ipv4(const char *s, uint32_t *out)
{
    for (const char *p = s; *p != '\0'; p++)
    {
        if (!((*p >= '0' && *p <= '9') || *p == '.'))
        {
            return false;
        }
    }
    uint32_t ip = net_str_to_ip(s);
    if (ip == 0U)
    {
        return false;
    }
    *out = ip;
    return true;
}

void api_v1_mqtt_post(const char *query, char *body, size_t max_len)
{
    if (body == NULL || max_len == 0U)
    {
        return;
    }
    const char *text = (query != NULL) ? query : "";
    mqtt_config_t cfg;
    mqtt_get_config(&cfg);

    jl_cur_t c;
    jl_init(&c, text, strlen(text));
    if (!jl_take(&c, '{'))
    {
        api_error(body, max_len, "syntax");
        return;
    }
    if (!jl_take(&c, '}'))
    {
        for (;;)
        {
            char key[API_V1_KEY_MAX];
            if (!jl_string(&c, key, sizeof(key), NULL) || !jl_take(&c, ':'))
            {
                api_error(body, max_len, "syntax");
                return;
            }
            const char *bad = NULL;
            if (strcmp(key, "enabled") == 0)
            {
                if (!jl_bool(&c, &cfg.enabled))
                {
                    bad = "enabled";
                }
            }
            else if (strcmp(key, "host") == 0)
            {
                char host[NET_IP_STR_BUF_LEN];
                bool trunc = false;
                if (!jl_string(&c, host, sizeof(host), &trunc) || trunc || !api_parse_ipv4(host, &cfg.host))
                {
                    bad = "host";
                }
            }
            else if (strcmp(key, "port") == 0)
            {
                uint32_t v = 0U;
                bool is_int = false;
                if (!jl_number(&c, &v, &is_int) || !is_int || v == 0U || v > API_V1_PORT_MAX)
                {
                    bad = "port";
                }
                else
                {
                    cfg.port = (uint16_t)v;
                }
            }
            else if (strcmp(key, "user") == 0)
            {
                bool trunc = false;
                if (!jl_string(&c, cfg.user, sizeof(cfg.user), &trunc) || trunc)
                {
                    bad = "user";
                }
            }
            else if (strcmp(key, "password") == 0)
            {
                bool trunc = false;
                if (!jl_string(&c, cfg.pass, sizeof(cfg.pass), &trunc) || trunc)
                {
                    bad = "password";
                }
            }
            else if (!jl_skip_value(&c))
            {
                bad = "syntax";
            }
            if (bad != NULL)
            {
                api_error(body, max_len, bad);
                return;
            }
            if (jl_take(&c, '}'))
            {
                break;
            }
            if (!jl_take(&c, ','))
            {
                api_error(body, max_len, "syntax");
                return;
            }
        }
    }
    if (!jl_at_end(&c))
    {
        api_error(body, max_len, "syntax");
        return;
    }
    if (cfg.enabled && cfg.host == 0U)
    {
        api_error(body, max_len, "host_required");
        return;
    }
    if (!mqtt_set_config(&cfg, systimer_get_us()))
    {
        api_error(body, max_len, "save_failed");
        return;
    }
    api_v1_mqtt_get(NULL, body, max_len);
}

void api_v1_register_routes(void)
{
    (void)http_route_register(API_V1_PATH_LIGHT, HTTP_METHOD_GET, api_v1_light_get);
    (void)http_route_register(API_V1_PATH_LIGHT, HTTP_METHOD_POST, api_v1_light_post);
    (void)http_route_register(API_V1_PATH_DEVICE, HTTP_METHOD_GET, api_v1_device_get);
    (void)http_route_register(API_V1_PATH_MQTT, HTTP_METHOD_GET, api_v1_mqtt_get);
    (void)http_route_register(API_V1_PATH_MQTT, HTTP_METHOD_POST, api_v1_mqtt_post);
}
