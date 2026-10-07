/*
 * Iron V - Light property (REV-23)
 *
 * The one controllable property of the board: the onboard RGB LED as a light with on/off,
 * brightness (1-100 %) and an RGB colour. Spec: docs/reference/property-model-v1.md (REV-22).
 *
 * Every source (MQTT, REST, shell, BOOT short press) goes through the same rules: commands carry
 * the desired state, absent fields keep their value, brightness 0 means off, black is rejected,
 * a bad command is rejected as a whole. The state survives reboots (NVS, written after it has
 * been stable for LIGHT_NVS_SETTLE_US) and never changes because of network events.
 */
#ifndef IRON_V_LIGHT_H
#define IRON_V_LIGHT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIGHT_BRIGHTNESS_MIN             1U
#define LIGHT_BRIGHTNESS_MAX             100U
#define LIGHT_DEFAULT_BRIGHTNESS         100U
#define LIGHT_COLOR_MAX                  255U
#define LIGHT_PERCENT                    100U
#define LIGHT_ROUND_HALF                 (LIGHT_PERCENT / 2U)

#define LIGHT_CMD_MAX_LEN                128U       /* longest accepted command body */
#define LIGHT_STATE_JSON_MAX             96U        /* serialized state incl. NUL */
#define LIGHT_JSON_KEY_MAX               16U

#define LIGHT_NVS_KEY                    "light_state"
#define LIGHT_NVS_SETTLE_US              10000000ULL   /* write once the state held for 10 s */
#define LIGHT_STATE_COALESCE_US          250000ULL     /* at most one state report per 250 ms */

/* NVS word: [31] on, [30:24] brightness, [23:16] r, [15:8] g, [7:0] b */
#define LIGHT_PACK_ON_BIT                (1UL << 31)
#define LIGHT_PACK_BRI_SHIFT             24U
#define LIGHT_PACK_BRI_MASK              0x7FU
#define LIGHT_PACK_R_SHIFT               16U
#define LIGHT_PACK_G_SHIFT               8U
#define LIGHT_PACK_BYTE_MASK             0xFFU

typedef struct {
    bool    on;
    uint8_t brightness;          /* LIGHT_BRIGHTNESS_MIN..MAX, kept while off */
    uint8_t r, g, b;             /* hue/chroma; never all zero */
} light_state_t;

typedef struct {
    bool    has_state;
    bool    on;
    bool    has_brightness;
    uint8_t brightness;          /* 0..100; 0 is turned into on=false by light_apply_cmd */
    bool    has_color;
    uint8_t r, g, b;
} light_cmd_t;

typedef enum {
    LIGHT_CMD_OK = 0,
    LIGHT_CMD_ERR_TOO_LONG,      /* body over LIGHT_CMD_MAX_LEN */
    LIGHT_CMD_ERR_SYNTAX,        /* not a JSON object we can read */
    LIGHT_CMD_ERR_TYPE,          /* known key with the wrong type (e.g. a fraction) */
    LIGHT_CMD_ERR_RANGE,         /* brightness > 100, colour channel > 255, partial colour */
    LIGHT_CMD_ERR_STATE,         /* "state" is not "ON"/"OFF" */
    LIGHT_CMD_ERR_BLACK,         /* colour {0,0,0}: use state OFF */
    LIGHT_CMD_ERR_EMPTY          /* no known key */
} light_cmd_status_t;

typedef enum {
    LIGHT_SRC_MQTT = 0,
    LIGHT_SRC_REST,
    LIGHT_SRC_SHELL,
    LIGHT_SRC_BUTTON,
    LIGHT_SRC_COUNT
} light_source_t;

typedef struct {
    uint32_t applied[LIGHT_SRC_COUNT];   /* accepted commands that changed the state */
    uint32_t unchanged;                  /* accepted commands that changed nothing */
    uint32_t rejected;                   /* light_cmd_rejected (spec 2.2 rule 5) */
    uint32_t nvs_writes;
    uint32_t led_writes;
    uint32_t led_busy;                   /* LED still sending the previous frame: retried */
    uint32_t reports;                    /* state reports taken by the MQTT client */
} light_stats_t;

/* Pure helpers (host-tested) */
light_cmd_status_t light_parse_command(const char *json, size_t len, light_cmd_t *out);
void        light_apply_cmd(const light_state_t *cur, const light_cmd_t *cmd, light_state_t *out);
size_t      light_state_to_json(const light_state_t *s, char *buf, size_t max_len);
void        light_output_rgb(const light_state_t *s, uint8_t *r, uint8_t *g, uint8_t *b);
uint32_t    light_pack(const light_state_t *s);
bool        light_unpack(uint32_t word, light_state_t *out);
void        light_default_state(light_state_t *out);
bool        light_state_equal(const light_state_t *a, const light_state_t *b);
const char *light_cmd_status_str(light_cmd_status_t st);

/* Module */
void               light_init(uint64_t now_us);       /* NVS restore, LED driver, first output */
light_cmd_status_t light_command(const char *json, size_t len, light_source_t src, uint64_t now_us);
void               light_set(const light_state_t *s, light_source_t src, uint64_t now_us);
void               light_toggle_local(uint64_t now_us);   /* BOOT short press: the only toggle */
void               light_get(light_state_t *out);
void               light_tick(uint64_t now_us);          /* NVS settle timer, LED retry */
/* MQTT: true when a changed state is due for a report (coalesced); marks it reported */
bool               light_take_report(uint64_t now_us);
/* MQTT: the state was just reported unconditionally (after CONNACK) */
void               light_mark_reported(uint64_t now_us);
void               light_get_stats(light_stats_t *out);
void               light_shell(const char *args, uint64_t now_us);

#endif /* IRON_V_LIGHT_H */
