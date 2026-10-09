/*
 * Iron V - MQTT module (REV-33): registers the MQTT client (REV-23) with the core.
 *
 * Optional, off in the light profile: add `mqtt` to MODULES (see README.md in this directory).
 * The client (mqtt.c), REST v1 /api/v1/mqtt for the broker settings, the MQTT session in
 * /api/v1/device and the `mqtt` command.
 */
#include "mqtt.h"
#include "module.h"
#include "shell.h"
#include "http_server.h"
#include "api_v1.h"
#include "json_lite.h"
#include "net.h"
#include "systimer.h"
#include "string.h"

#define MQTT_API_PATH                    "/api/v1/mqtt"
#define MQTT_API_PORT_MAX                65535U

/* ========================================================================= */
/* /api/v1/mqtt                                                              */
/* ========================================================================= */

static void mqtt_api_get(const char *query, char *body, size_t max_len)
{
    (void)query;
    if (body == NULL || max_len == 0U)
    {
        return;
    }
    mqtt_config_t mc;
    mqtt_get_config(&mc);
    body[0] = '\0';
    api_v1_append(body, max_len, mc.enabled ? "{\"enabled\":true,\"host\":\"" : "{\"enabled\":false,\"host\":\"");
    if (mc.host != 0U)
    {
        api_v1_append_ip(body, max_len, mc.host);
    }
    api_v1_append(body, max_len, "\",\"port\":");
    api_v1_append_u32(body, max_len, mc.port);
    api_v1_append(body, max_len, ",\"user\":\"");
    api_v1_append_escaped(body, max_len, mc.user);
    api_v1_append(body, max_len, mc.pass[0] != '\0' ? "\",\"has_password\":true}\r\n" : "\",\"has_password\":false}\r\n");
}

/* Dotted quad only (net_str_to_ip stops at the first other character) */
static bool mqtt_api_parse_ipv4(const char *s, uint32_t *out)
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

static void mqtt_api_post(const char *query, char *body, size_t max_len)
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
        api_v1_error(body, max_len, "syntax");
        return;
    }
    if (!jl_take(&c, '}'))
    {
        for (;;)
        {
            char key[API_V1_KEY_MAX];
            if (!jl_string(&c, key, sizeof(key), NULL) || !jl_take(&c, ':'))
            {
                api_v1_error(body, max_len, "syntax");
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
                if (!jl_string(&c, host, sizeof(host), &trunc) || trunc || !mqtt_api_parse_ipv4(host, &cfg.host))
                {
                    bad = "host";
                }
            }
            else if (strcmp(key, "port") == 0)
            {
                uint32_t v = 0U;
                bool is_int = false;
                if (!jl_number(&c, &v, &is_int) || !is_int || v == 0U || v > MQTT_API_PORT_MAX)
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
                api_v1_error(body, max_len, bad);
                return;
            }
            if (jl_take(&c, '}'))
            {
                break;
            }
            if (!jl_take(&c, ','))
            {
                api_v1_error(body, max_len, "syntax");
                return;
            }
        }
    }
    if (!jl_at_end(&c))
    {
        api_v1_error(body, max_len, "syntax");
        return;
    }
    if (cfg.enabled && cfg.host == 0U)
    {
        api_v1_error(body, max_len, "host_required");
        return;
    }
    if (!mqtt_set_config(&cfg, systimer_get_us()))
    {
        api_v1_error(body, max_len, "save_failed");
        return;
    }
    mqtt_api_get(NULL, body, max_len);
}


/* ,"mqtt":{...} in GET /api/v1/device */
static void mqtt_module_device_json(char *body, size_t max_len)
{
    mqtt_status_t ms;
    mqtt_get_status(&ms);
    mqtt_config_t mc;
    mqtt_get_config(&mc);
    api_v1_append(body, max_len, ",\"mqtt\":{\"state\":\"");
    api_v1_append(body, max_len, mqtt_state_class_str(ms.state));
    api_v1_append(body, max_len, "\",\"detail\":\"");
    api_v1_append(body, max_len, mqtt_state_str(ms.state));
    api_v1_append(body, max_len, "\",\"broker\":\"");
    if (mc.host != 0U)
    {
        api_v1_append_ip(body, max_len, mc.host);
    }
    api_v1_append(body, max_len, "\",\"last_error\":\"");
    api_v1_append(body, max_len, mqtt_error_str(ms.last_error));
    api_v1_append(body, max_len, "\",\"reconnects\":");
    api_v1_append_u32(body, max_len, ms.reconnects);
    api_v1_append(body, max_len, "}");
}

static void mqtt_module_routes(void)
{
    (void)http_route_register(MQTT_API_PATH, HTTP_METHOD_GET, mqtt_api_get);
    (void)http_route_register(MQTT_API_PATH, HTTP_METHOD_POST, mqtt_api_post);
}

static void mqtt_module_init(uint64_t now_us)
{
    (void)now_us;
    mqtt_init();
}

static void mqtt_module_tick(uint64_t now_us)
{
    mqtt_tick(now_us, mqtt_link_usable());
}

static void mqtt_module_shell(char *args)
{
    mqtt_shell(args, systimer_get_us());
}

MODULE_DEFINE(s_mqtt_module, {
    .name        = "mqtt",
    .order       = MODULE_ORDER_MQTT,
    .init        = mqtt_module_init,
    .routes      = mqtt_module_routes,
    .tick        = mqtt_module_tick,
    .device_json = mqtt_module_device_json,
});

SHELL_COMMAND_DEFINE(s_mqtt_cmd, {
    .name = "mqtt",
    .help = "mqtt [on|off|set host|port|user|pass <v>] - MQTT client to the bridge's broker",
    .run  = mqtt_module_shell,
});
