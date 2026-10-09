/*
 * Iron V - Light module (REV-33): registers the light (REV-23) with the core.
 *
 * The onboard RGB LED (light.c, rgb_led.c), its page at / (index.html, gzip in flash, REV-25),
 * REST v1 /api/v1/light, the `light` command and the BOOT short press.
 */
#include "light.h"
#include "module.h"
#include "shell.h"
#include "http_server.h"
#include "api_v1.h"
#include "systimer.h"
#include "string.h"
#include "web_index_gz.h"   /* generated from src/modules/light/index.html (scripts/gen_web.py) */

#define LIGHT_PATH_PAGE                  "/"
#define LIGHT_PATH_PAGE_INDEX            "/index.html"
#define LIGHT_PATH_API                   "/api/v1/light"

/* The page goes out in one response: headers + compressed page <= response buffer */
_Static_assert(WEB_INDEX_GZ_LEN + HTTP_HEADER_RESERVE <= HTTP_RESPONSE_BUF_SIZE, "light page too large");

/* GET / and GET /index.html -> the light page, gzip-compressed in flash */
static void light_page_get(const char *query, char *body, size_t max_len)
{
    (void)query;
    if (body != NULL && max_len > 0U)
    {
        body[0] = '\0';
    }
    http_response_set_gzip_body(g_web_index_gz, WEB_INDEX_GZ_LEN);
}

/* GET /api/v1/light -> state JSON */
static void light_api_get(const char *query, char *body, size_t max_len)
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
    api_v1_append(body, max_len, "\r\n");
}

/* POST /api/v1/light -> 200 new state | 400 {"error":"<reason>"} */
static void light_api_post(const char *query, char *body, size_t max_len)
{
    if (body == NULL || max_len == 0U)
    {
        return;
    }
    const char *cmd = (query != NULL) ? query : "";
    light_cmd_status_t st = light_command(cmd, strlen(cmd), LIGHT_SRC_REST, systimer_get_us());
    if (st != LIGHT_CMD_OK)
    {
        api_v1_error(body, max_len, light_cmd_status_str(st));
        return;
    }
    light_api_get(NULL, body, max_len);
}

static void light_module_routes(void)
{
    (void)http_route_register(LIGHT_PATH_PAGE, HTTP_METHOD_GET, light_page_get);
    (void)http_route_register(LIGHT_PATH_PAGE_INDEX, HTTP_METHOD_GET, light_page_get);
    (void)http_route_register(LIGHT_PATH_API, HTTP_METHOD_GET, light_api_get);
    (void)http_route_register(LIGHT_PATH_API, HTTP_METHOD_POST, light_api_post);
}

static void light_module_event(module_event_t ev, uint64_t now_us)
{
    if (ev == MODULE_EVENT_BUTTON_SHORT_PRESS)
    {
        light_toggle_local(now_us);   /* the device-side change for REV-26 */
    }
}

static void light_module_shell(char *args)
{
    light_shell(args, systimer_get_us());
}

MODULE_DEFINE(s_light_module, {
    .name   = "light",
    .order  = MODULE_ORDER_LIGHT,
    .init   = light_init,
    .routes = light_module_routes,
    .tick   = light_tick,
    .event  = light_module_event,
});

SHELL_COMMAND_DEFINE(s_light_cmd, {
    .name = "light",
    .help = "light [on|off|bri <1-100>|rgb <r> <g> <b>] - Onboard RGB LED (BOOT short press toggles)",
    .run  = light_module_shell,
});
