/*
 * Iron V - Light property (REV-23). See light.h and docs/reference/property-model-v1.md.
 */
#include "light.h"
#include "rgb_led.h"
#include "nvs.h"
#include "string.h"
#include "json_lite.h"

#if defined(__riscv)
#include "console.h"
#else
#include <stdio.h>
#define console_puts(s) printf("%s", (s))
#endif

#define LIGHT_DEC_BUF_LEN                12U
#define LIGHT_DEC_BASE                   10U
#define LIGHT_SHELL_RGB_ARGS             3U

static light_state_t s_light;
static light_stats_t s_light_stats;
static bool     s_light_led_dirty;
static bool     s_light_report_pending;
static bool     s_light_reported_once;
static uint64_t s_light_last_report_us;
static bool     s_light_nvs_dirty;
static uint64_t s_light_changed_us;
static bool     s_light_stored_valid;
static uint32_t s_light_stored_word;

/* ========================================================================= */
/* Small text helpers                                                        */
/* ========================================================================= */

static size_t light_u32_to_dec(uint32_t v, char *buf, size_t max_len)
{
    char tmp[LIGHT_DEC_BUF_LEN];
    size_t n = 0U;
    do
    {
        tmp[n++] = (char)('0' + (v % LIGHT_DEC_BASE));
        v /= LIGHT_DEC_BASE;
    } while (v != 0U && n < sizeof(tmp));
    if (n + 1U > max_len)
    {
        if (max_len > 0U)
        {
            buf[0] = '\0';
        }
        return 0U;
    }
    for (size_t i = 0U; i < n; i++)
    {
        buf[i] = tmp[n - 1U - i];
    }
    buf[n] = '\0';
    return n;
}

/* Appends src; returns false (and leaves buf terminated) when it does not fit */
static bool light_append(char *buf, size_t max_len, size_t *pos, const char *src)
{
    size_t n = strlen(src);
    if (*pos + n + 1U > max_len)
    {
        return false;
    }
    memcpy(buf + *pos, src, n);
    *pos += n;
    buf[*pos] = '\0';
    return true;
}

static bool light_append_u32(char *buf, size_t max_len, size_t *pos, uint32_t v)
{
    char num[LIGHT_DEC_BUF_LEN];
    (void)light_u32_to_dec(v, num, sizeof(num));
    return light_append(buf, max_len, pos, num);
}

/* ========================================================================= */
/* Command parser: one JSON object, fixed keys, integers only, no allocation */
/* ========================================================================= */

/* Integer value for a known key: TYPE for fractions/negatives/non-numbers, RANGE above max */
static light_cmd_status_t light_known_uint(jl_cur_t *c, uint32_t max, uint8_t *out)
{
    if (!jl_is_number_start(c))
    {
        return LIGHT_CMD_ERR_TYPE;
    }
    uint32_t v;
    bool is_int;
    if (!jl_number(c, &v, &is_int))
    {
        return LIGHT_CMD_ERR_SYNTAX;
    }
    if (!is_int)
    {
        return LIGHT_CMD_ERR_TYPE;
    }
    if (v > max)
    {
        return LIGHT_CMD_ERR_RANGE;
    }
    *out = (uint8_t)v;
    return LIGHT_CMD_OK;
}

static light_cmd_status_t light_parse_color(jl_cur_t *c, light_cmd_t *cmd)
{
    if (!jl_take(c, '{'))
    {
        return LIGHT_CMD_ERR_TYPE;
    }
    bool has_r = false, has_g = false, has_b = false;
    if (!jl_take(c, '}'))
    {
        for (;;)
        {
            char key[LIGHT_JSON_KEY_MAX];
            if (!jl_string(c, key, sizeof(key), NULL) || !jl_take(c, ':'))
            {
                return LIGHT_CMD_ERR_SYNTAX;
            }
            light_cmd_status_t st = LIGHT_CMD_OK;
            if (strcmp(key, "r") == 0)
            {
                st = light_known_uint(c, LIGHT_COLOR_MAX, &cmd->r);
                has_r = true;
            }
            else if (strcmp(key, "g") == 0)
            {
                st = light_known_uint(c, LIGHT_COLOR_MAX, &cmd->g);
                has_g = true;
            }
            else if (strcmp(key, "b") == 0)
            {
                st = light_known_uint(c, LIGHT_COLOR_MAX, &cmd->b);
                has_b = true;
            }
            else if (!jl_skip_value(c))
            {
                st = LIGHT_CMD_ERR_SYNTAX;
            }
            if (st != LIGHT_CMD_OK)
            {
                return st;
            }
            if (jl_take(c, '}'))
            {
                break;
            }
            if (!jl_take(c, ','))
            {
                return LIGHT_CMD_ERR_SYNTAX;
            }
        }
    }
    if (!(has_r && has_g && has_b))
    {
        return LIGHT_CMD_ERR_RANGE;   /* a colour needs all three channels */
    }
    if (cmd->r == 0U && cmd->g == 0U && cmd->b == 0U)
    {
        return LIGHT_CMD_ERR_BLACK;
    }
    cmd->has_color = true;
    return LIGHT_CMD_OK;
}

light_cmd_status_t light_parse_command(const char *json, size_t len, light_cmd_t *out)
{
    if (json == NULL || out == NULL)
    {
        return LIGHT_CMD_ERR_SYNTAX;
    }
    memset(out, 0, sizeof(*out));
    if (len > LIGHT_CMD_MAX_LEN)
    {
        return LIGHT_CMD_ERR_TOO_LONG;
    }
    jl_cur_t c;
    jl_init(&c, json, len);
    if (!jl_take(&c, '{'))
    {
        return LIGHT_CMD_ERR_SYNTAX;
    }
    if (!jl_take(&c, '}'))
    {
        for (;;)
        {
            char key[LIGHT_JSON_KEY_MAX];
            if (!jl_string(&c, key, sizeof(key), NULL) || !jl_take(&c, ':'))
            {
                return LIGHT_CMD_ERR_SYNTAX;
            }
            light_cmd_status_t st = LIGHT_CMD_OK;
            if (strcmp(key, "state") == 0)
            {
                char val[LIGHT_JSON_KEY_MAX];
                if (!jl_peek(&c, '"'))
                {
                    st = LIGHT_CMD_ERR_TYPE;
                }
                else if (!jl_string(&c, val, sizeof(val), NULL))
                {
                    st = LIGHT_CMD_ERR_SYNTAX;
                }
                else if (strcmp(val, "ON") == 0 || strcmp(val, "OFF") == 0)
                {
                    out->has_state = true;
                    out->on = (strcmp(val, "ON") == 0);
                }
                else
                {
                    st = LIGHT_CMD_ERR_STATE;
                }
            }
            else if (strcmp(key, "brightness") == 0)
            {
                st = light_known_uint(&c, LIGHT_BRIGHTNESS_MAX, &out->brightness);
                out->has_brightness = (st == LIGHT_CMD_OK);
            }
            else if (strcmp(key, "color") == 0)
            {
                st = light_parse_color(&c, out);
            }
            else if (!jl_skip_value(&c))
            {
                st = LIGHT_CMD_ERR_SYNTAX;   /* unknown keys are skipped (HA may add transition etc.) */
            }
            if (st != LIGHT_CMD_OK)
            {
                return st;
            }
            if (jl_take(&c, '}'))
            {
                break;
            }
            if (!jl_take(&c, ','))
            {
                return LIGHT_CMD_ERR_SYNTAX;
            }
        }
    }
    if (!jl_at_end(&c))
    {
        return LIGHT_CMD_ERR_SYNTAX;
    }
    if (!out->has_state && !out->has_brightness && !out->has_color)
    {
        return LIGHT_CMD_ERR_EMPTY;
    }
    return LIGHT_CMD_OK;
}

/* ========================================================================= */
/* Pure state helpers                                                        */
/* ========================================================================= */

void light_default_state(light_state_t *out)
{
    out->on = false;
    out->brightness = LIGHT_DEFAULT_BRIGHTNESS;
    out->r = LIGHT_COLOR_MAX;
    out->g = LIGHT_COLOR_MAX;
    out->b = LIGHT_COLOR_MAX;
}

bool light_state_equal(const light_state_t *a, const light_state_t *b)
{
    return a->on == b->on && a->brightness == b->brightness && a->r == b->r && a->g == b->g && a->b == b->b;
}

void light_apply_cmd(const light_state_t *cur, const light_cmd_t *cmd, light_state_t *out)
{
    *out = *cur;
    if (cmd->has_color)
    {
        out->r = cmd->r;
        out->g = cmd->g;
        out->b = cmd->b;
    }
    if (cmd->has_brightness)
    {
        if (cmd->brightness == 0U)
        {
            out->on = false;   /* Google's 0 %: off, the stored level stays */
        }
        else
        {
            out->brightness = cmd->brightness;
        }
    }
    if (cmd->has_state && !(cmd->has_brightness && cmd->brightness == 0U))
    {
        out->on = cmd->on;
    }
}

size_t light_state_to_json(const light_state_t *s, char *buf, size_t max_len)
{
    if (buf == NULL || max_len == 0U)
    {
        return 0U;
    }
    size_t pos = 0U;
    buf[0] = '\0';
    bool ok = light_append(buf, max_len, &pos, s->on ? "{\"state\":\"ON\"" : "{\"state\":\"OFF\"") &&
              light_append(buf, max_len, &pos, ",\"brightness\":") &&
              light_append_u32(buf, max_len, &pos, s->brightness) &&
              light_append(buf, max_len, &pos, ",\"color_mode\":\"rgb\",\"color\":{\"r\":") &&
              light_append_u32(buf, max_len, &pos, s->r) &&
              light_append(buf, max_len, &pos, ",\"g\":") &&
              light_append_u32(buf, max_len, &pos, s->g) &&
              light_append(buf, max_len, &pos, ",\"b\":") &&
              light_append_u32(buf, max_len, &pos, s->b) &&
              light_append(buf, max_len, &pos, "}}");
    if (!ok)
    {
        buf[0] = '\0';
        return 0U;
    }
    return pos;
}

static uint8_t light_scale(uint8_t c, uint8_t brightness)
{
    return (uint8_t)(((uint32_t)c * brightness + LIGHT_ROUND_HALF) / LIGHT_PERCENT);
}

void light_output_rgb(const light_state_t *s, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!s->on)
    {
        *r = 0U;
        *g = 0U;
        *b = 0U;
        return;
    }
    *r = light_scale(s->r, s->brightness);
    *g = light_scale(s->g, s->brightness);
    *b = light_scale(s->b, s->brightness);
}

uint32_t light_pack(const light_state_t *s)
{
    return (s->on ? LIGHT_PACK_ON_BIT : 0U) |
           (((uint32_t)s->brightness & LIGHT_PACK_BRI_MASK) << LIGHT_PACK_BRI_SHIFT) |
           ((uint32_t)s->r << LIGHT_PACK_R_SHIFT) | ((uint32_t)s->g << LIGHT_PACK_G_SHIFT) | (uint32_t)s->b;
}

bool light_unpack(uint32_t word, light_state_t *out)
{
    light_state_t s;
    s.on = (word & LIGHT_PACK_ON_BIT) != 0U;
    s.brightness = (uint8_t)((word >> LIGHT_PACK_BRI_SHIFT) & LIGHT_PACK_BRI_MASK);
    s.r = (uint8_t)((word >> LIGHT_PACK_R_SHIFT) & LIGHT_PACK_BYTE_MASK);
    s.g = (uint8_t)((word >> LIGHT_PACK_G_SHIFT) & LIGHT_PACK_BYTE_MASK);
    s.b = (uint8_t)(word & LIGHT_PACK_BYTE_MASK);
    if (s.brightness < LIGHT_BRIGHTNESS_MIN || s.brightness > LIGHT_BRIGHTNESS_MAX ||
        (s.r == 0U && s.g == 0U && s.b == 0U))
    {
        return false;
    }
    *out = s;
    return true;
}

const char *light_cmd_status_str(light_cmd_status_t st)
{
    switch (st)
    {
        case LIGHT_CMD_OK:           return "ok";
        case LIGHT_CMD_ERR_TOO_LONG: return "too_long";
        case LIGHT_CMD_ERR_SYNTAX:   return "syntax";
        case LIGHT_CMD_ERR_TYPE:     return "type";
        case LIGHT_CMD_ERR_RANGE:    return "range";
        case LIGHT_CMD_ERR_STATE:    return "state";
        case LIGHT_CMD_ERR_BLACK:    return "black";
        case LIGHT_CMD_ERR_EMPTY:    return "empty";
        default:                     return "unknown";
    }
}

/* ========================================================================= */
/* Module                                                                    */
/* ========================================================================= */

static void light_push_output(void)
{
    uint8_t r, g, b;
    light_output_rgb(&s_light, &r, &g, &b);
    if (rgb_led_write(r, g, b))
    {
        s_light_led_dirty = false;
        s_light_stats.led_writes++;
    }
    else
    {
        s_light_led_dirty = true;
        s_light_stats.led_busy++;
    }
}

void light_init(uint64_t now_us)
{
    memset(&s_light_stats, 0, sizeof(s_light_stats));
    light_default_state(&s_light);
    s_light_stored_valid = false;
    uint32_t word = 0U;
    light_state_t saved;
    if (nvs_get_u32(LIGHT_NVS_KEY, &word) == NVS_OK && light_unpack(word, &saved))
    {
        s_light = saved;
        s_light_stored_valid = true;
        s_light_stored_word = word;
    }
    s_light_nvs_dirty = false;
    s_light_changed_us = now_us;
    s_light_report_pending = true;
    s_light_reported_once = false;
    s_light_last_report_us = now_us;
    rgb_led_init();
    light_push_output();
}

void light_set(const light_state_t *s, light_source_t src, uint64_t now_us)
{
    if (light_state_equal(s, &s_light))
    {
        s_light_stats.unchanged++;
        return;
    }
    s_light = *s;
    if (src < LIGHT_SRC_COUNT)
    {
        s_light_stats.applied[src]++;
    }
    s_light_report_pending = true;
    s_light_nvs_dirty = true;
    s_light_changed_us = now_us;
    light_push_output();
}

light_cmd_status_t light_command(const char *json, size_t len, light_source_t src, uint64_t now_us)
{
    light_cmd_t cmd;
    light_cmd_status_t st = light_parse_command(json, len, &cmd);
    if (st != LIGHT_CMD_OK)
    {
        s_light_stats.rejected++;
        return st;
    }
    light_state_t next;
    light_apply_cmd(&s_light, &cmd, &next);
    light_set(&next, src, now_us);
    return LIGHT_CMD_OK;
}

void light_toggle_local(uint64_t now_us)
{
    light_state_t next = s_light;
    next.on = !next.on;
    light_set(&next, LIGHT_SRC_BUTTON, now_us);
}

void light_get(light_state_t *out)
{
    *out = s_light;
}

void light_tick(uint64_t now_us)
{
    if (s_light_led_dirty)
    {
        light_push_output();
    }
    if (s_light_nvs_dirty && (now_us - s_light_changed_us) >= LIGHT_NVS_SETTLE_US)
    {
        s_light_nvs_dirty = false;
        uint32_t word = light_pack(&s_light);
        if (!s_light_stored_valid || word != s_light_stored_word)
        {
            if (nvs_set_u32(LIGHT_NVS_KEY, word) == NVS_OK)
            {
                s_light_stored_valid = true;
                s_light_stored_word = word;
                s_light_stats.nvs_writes++;
            }
        }
    }
}

bool light_take_report(uint64_t now_us)
{
    if (!s_light_report_pending)
    {
        return false;
    }
    if (s_light_reported_once && (now_us - s_light_last_report_us) < LIGHT_STATE_COALESCE_US)
    {
        return false;
    }
    light_mark_reported(now_us);
    return true;
}

void light_mark_reported(uint64_t now_us)
{
    s_light_report_pending = false;
    s_light_reported_once = true;
    s_light_last_report_us = now_us;
    s_light_stats.reports++;
}

void light_get_stats(light_stats_t *out)
{
    *out = s_light_stats;
}

/* ========================================================================= */
/* Shell: light | light on|off | light bri <1-100> | light rgb <r> <g> <b>   */
/* ========================================================================= */

static bool light_shell_uint(const char **p, uint32_t *out)
{
    while (**p == ' ')
    {
        (*p)++;
    }
    uint32_t v = 0U;
    bool any = false;
    while (**p >= '0' && **p <= '9')
    {
        any = true;
        if (v <= JL_NUM_LIMIT)
        {
            v = v * LIGHT_DEC_BASE + (uint32_t)(**p - '0');
        }
        (*p)++;
    }
    *out = v;
    return any && (**p == ' ' || **p == '\0');
}

static void light_shell_print(void)
{
    char json[LIGHT_STATE_JSON_MAX];
    (void)light_state_to_json(&s_light, json, sizeof(json));
    console_puts("Light: ");
    console_puts(json);
    console_puts("\r\n");
    char line[LIGHT_STATE_JSON_MAX];
    size_t pos = 0U;
    line[0] = '\0';
    (void)(light_append(line, sizeof(line), &pos, "  applied mqtt/rest/shell/button: ") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.applied[LIGHT_SRC_MQTT]) &&
           light_append(line, sizeof(line), &pos, "/") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.applied[LIGHT_SRC_REST]) &&
           light_append(line, sizeof(line), &pos, "/") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.applied[LIGHT_SRC_SHELL]) &&
           light_append(line, sizeof(line), &pos, "/") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.applied[LIGHT_SRC_BUTTON]) &&
           light_append(line, sizeof(line), &pos, "\r\n"));
    console_puts(line);
    pos = 0U;
    line[0] = '\0';
    (void)(light_append(line, sizeof(line), &pos, "  unchanged ") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.unchanged) &&
           light_append(line, sizeof(line), &pos, ", rejected ") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.rejected) &&
           light_append(line, sizeof(line), &pos, ", reports ") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.reports) &&
           light_append(line, sizeof(line), &pos, ", nvs writes ") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.nvs_writes) &&
           light_append(line, sizeof(line), &pos, ", led writes ") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.led_writes) &&
           light_append(line, sizeof(line), &pos, " (busy ") &&
           light_append_u32(line, sizeof(line), &pos, s_light_stats.led_busy) &&
           light_append(line, sizeof(line), &pos, ")\r\n"));
    console_puts(line);
}

void light_shell(const char *args, uint64_t now_us)
{
    while (*args == ' ')
    {
        args++;
    }
    light_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    uint32_t v[LIGHT_SHELL_RGB_ARGS];

    if (*args == '\0' || strcmp(args, "stats") == 0)
    {
        light_shell_print();
        return;
    }
    if (strcmp(args, "on") == 0 || strcmp(args, "off") == 0)
    {
        cmd.has_state = true;
        cmd.on = (args[1] == 'n');
    }
    else if (strncmp(args, "bri ", 4) == 0)
    {
        const char *p = args + 4;
        if (!light_shell_uint(&p, &v[0]) || v[0] < LIGHT_BRIGHTNESS_MIN || v[0] > LIGHT_BRIGHTNESS_MAX)
        {
            console_puts("Usage: light bri <1-100>\r\n");
            return;
        }
        cmd.has_brightness = true;
        cmd.brightness = (uint8_t)v[0];
    }
    else if (strncmp(args, "rgb ", 4) == 0)
    {
        const char *p = args + 4;
        bool ok = true;
        for (uint32_t i = 0U; i < LIGHT_SHELL_RGB_ARGS && ok; i++)
        {
            ok = light_shell_uint(&p, &v[i]) && v[i] <= LIGHT_COLOR_MAX;
        }
        if (!ok || (v[0] == 0U && v[1] == 0U && v[2] == 0U))
        {
            console_puts("Usage: light rgb <r> <g> <b> (0-255 each, not all 0)\r\n");
            return;
        }
        cmd.has_color = true;
        cmd.r = (uint8_t)v[0];
        cmd.g = (uint8_t)v[1];
        cmd.b = (uint8_t)v[2];
    }
    else
    {
        console_puts("Usage: light [stats] | light on|off | light bri <1-100> | light rgb <r> <g> <b>\r\n");
        return;
    }
    light_state_t next;
    light_apply_cmd(&s_light, &cmd, &next);
    light_set(&next, LIGHT_SRC_SHELL, now_us);
    light_shell_print();
}
