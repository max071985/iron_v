/*
 * Iron V - Minimal JSON reader (REV-23)
 *
 * Cursor helpers for reading small, flat JSON request bodies (light commands, MQTT settings)
 * without allocation: whitespace, punctuation, strings (escapes copied as the escaped char),
 * non-negative integers and skipping any value up to a nesting limit.
 */
#ifndef IRON_V_JSON_LITE_H
#define IRON_V_JSON_LITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JL_MAX_DEPTH                     4U        /* nesting skipped inside unknown keys */
#define JL_NUM_LIMIT                     65535U    /* numbers stop growing here (callers range-check) */
#define JL_DEC_BASE                      10U

typedef struct {
    const char *p;
    const char *end;
} jl_cur_t;

void jl_init(jl_cur_t *c, const char *text, size_t len);
void jl_ws(jl_cur_t *c);
/* Skips whitespace, then consumes ch if it is next */
bool jl_take(jl_cur_t *c, char ch);
/* true if the next non-blank character is ch (not consumed) */
bool jl_peek(jl_cur_t *c, char ch);
/* String into out (NULL: skip); truncated to out_max - 1. *truncated (optional) reports it. */
bool jl_string(jl_cur_t *c, char *out, size_t out_max, bool *truncated);
/* Number; *is_int false for fractions, exponents or a sign (value still consumed) */
bool jl_number(jl_cur_t *c, uint32_t *val, bool *is_int);
bool jl_is_number_start(jl_cur_t *c);
/* true/false literal */
bool jl_bool(jl_cur_t *c, bool *out);
bool jl_skip_value(jl_cur_t *c);
/* Only whitespace (or NUL padding) left */
bool jl_at_end(jl_cur_t *c);

#endif /* IRON_V_JSON_LITE_H */
