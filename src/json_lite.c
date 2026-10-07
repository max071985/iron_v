/*
 * Iron V - Minimal JSON reader (REV-23). See json_lite.h.
 */
#include "json_lite.h"
#include "string.h"

void jl_init(jl_cur_t *c, const char *text, size_t len)
{
    c->p = text;
    c->end = text + len;
}

void jl_ws(jl_cur_t *c)
{
    while (c->p < c->end && (*c->p == ' ' || *c->p == '\t' || *c->p == '\r' || *c->p == '\n'))
    {
        c->p++;
    }
}

bool jl_take(jl_cur_t *c, char ch)
{
    jl_ws(c);
    if (c->p < c->end && *c->p == ch)
    {
        c->p++;
        return true;
    }
    return false;
}

bool jl_peek(jl_cur_t *c, char ch)
{
    jl_ws(c);
    return c->p < c->end && *c->p == ch;
}

bool jl_string(jl_cur_t *c, char *out, size_t out_max, bool *truncated)
{
    if (truncated != NULL)
    {
        *truncated = false;
    }
    if (!jl_take(c, '"'))
    {
        return false;
    }
    size_t n = 0U;
    while (c->p < c->end && *c->p != '"')
    {
        char ch = *c->p++;
        if (ch == '\\')
        {
            if (c->p >= c->end)
            {
                return false;
            }
            ch = *c->p++;
        }
        if (out != NULL)
        {
            if (n + 1U < out_max)
            {
                out[n++] = ch;
            }
            else if (truncated != NULL)
            {
                *truncated = true;
            }
        }
    }
    if (c->p >= c->end)
    {
        return false;
    }
    c->p++;   /* closing quote */
    if (out != NULL && out_max > 0U)
    {
        out[n] = '\0';
    }
    return true;
}

static bool jl_is_num_char(char ch)
{
    return (ch >= '0' && ch <= '9') || ch == '-' || ch == '+' || ch == '.' || ch == 'e' || ch == 'E';
}

bool jl_is_number_start(jl_cur_t *c)
{
    jl_ws(c);
    return c->p < c->end && jl_is_num_char(*c->p);
}

bool jl_number(jl_cur_t *c, uint32_t *val, bool *is_int)
{
    jl_ws(c);
    uint32_t v = 0U;
    bool digits = false;
    bool plain = true;
    while (c->p < c->end && jl_is_num_char(*c->p))
    {
        char ch = *c->p++;
        if (ch >= '0' && ch <= '9')
        {
            digits = true;
            if (v <= JL_NUM_LIMIT)
            {
                v = v * JL_DEC_BASE + (uint32_t)(ch - '0');
            }
        }
        else
        {
            plain = false;
        }
    }
    if (!digits)
    {
        return false;
    }
    *val = v;
    *is_int = plain;
    return true;
}

static bool jl_literal(jl_cur_t *c, const char *lit)
{
    jl_ws(c);
    size_t n = strlen(lit);
    if ((size_t)(c->end - c->p) < n || strncmp(c->p, lit, n) != 0)
    {
        return false;
    }
    c->p += n;
    return true;
}

bool jl_bool(jl_cur_t *c, bool *out)
{
    if (jl_literal(c, "true"))
    {
        *out = true;
        return true;
    }
    if (jl_literal(c, "false"))
    {
        *out = false;
        return true;
    }
    return false;
}

static bool jl_skip_at(jl_cur_t *c, uint32_t depth);

static bool jl_skip_container(jl_cur_t *c, char close, bool keyed, uint32_t depth)
{
    if (depth >= JL_MAX_DEPTH)
    {
        return false;
    }
    if (jl_take(c, close))
    {
        return true;
    }
    for (;;)
    {
        if (keyed && (!jl_string(c, NULL, 0U, NULL) || !jl_take(c, ':')))
        {
            return false;
        }
        if (!jl_skip_at(c, depth + 1U))
        {
            return false;
        }
        if (jl_take(c, close))
        {
            return true;
        }
        if (!jl_take(c, ','))
        {
            return false;
        }
    }
}

static bool jl_skip_at(jl_cur_t *c, uint32_t depth)
{
    jl_ws(c);
    if (c->p >= c->end)
    {
        return false;
    }
    if (*c->p == '"')
    {
        return jl_string(c, NULL, 0U, NULL);
    }
    if (jl_take(c, '{'))
    {
        return jl_skip_container(c, '}', true, depth);
    }
    if (jl_take(c, '['))
    {
        return jl_skip_container(c, ']', false, depth);
    }
    bool b;
    if (jl_bool(c, &b) || jl_literal(c, "null"))
    {
        return true;
    }
    uint32_t v;
    bool is_int;
    return jl_number(c, &v, &is_int);
}

bool jl_skip_value(jl_cur_t *c)
{
    return jl_skip_at(c, 0U);
}

bool jl_at_end(jl_cur_t *c)
{
    jl_ws(c);
    while (c->p < c->end && *c->p == '\0')
    {
        c->p++;
    }
    return c->p == c->end;
}
