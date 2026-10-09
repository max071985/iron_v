/*
 * Iron V - REST v1 (REV-23)
 *
 * Maintenance and local control interface. Spec: docs/reference/property-model-v1.md section 5.
 * No authentication (REV-19 dropped). The core serves /api/v1/device; the resources of a module
 * are registered by that module (REV-33):
 *
 *   GET  /api/v1/device   identity, profile, uptime, address, plus module fields (core)
 *   GET  /api/v1/light    state JSON (light module)
 *   POST /api/v1/light    command JSON -> 200 new state | 400 {"error":"<reason>"} (light module)
 *   GET  /api/v1/mqtt     broker settings, the password is never returned (mqtt module)
 *   POST /api/v1/mqtt     {"enabled","host","port","user","password"} (any subset) -> 200 | 400 (mqtt module)
 */
#ifndef IRON_V_API_V1_H
#define IRON_V_API_V1_H

#include <stddef.h>
#include <stdint.h>

#define API_V1_PATH_DEVICE               "/api/v1/device"
#define API_V1_KEY_MAX                   16U

void api_v1_register_routes(void);
void api_v1_device_get(const char *query, char *body, size_t max_len);

/* JSON building blocks for module resources: append to the NUL-terminated body, never past max */
void api_v1_append(char *buf, size_t max, const char *src);
void api_v1_append_u32(char *buf, size_t max, uint32_t v);
void api_v1_append_escaped(char *buf, size_t max, const char *src);   /* string body, quotes escaped */
void api_v1_append_ip(char *buf, size_t max, uint32_t ip);
/* 400 with {"error":"<reason>"} */
void api_v1_error(char *body, size_t max_len, const char *reason);

#endif /* IRON_V_API_V1_H */
