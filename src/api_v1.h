/*
 * Iron V - REST v1 (REV-23)
 *
 * Maintenance and fallback interface (normal control goes through MQTT, review 7.1 principle 3).
 * Spec: docs/reference/property-model-v1.md section 5. No authentication (REV-19 dropped).
 *
 *   GET  /api/v1/light    state JSON
 *   POST /api/v1/light    command JSON -> 200 new state | 400 {"error":"<reason>"}
 *   GET  /api/v1/device   identity, uptime, address, MQTT session
 *   GET  /api/v1/mqtt     broker settings (the password is never returned)
 *   POST /api/v1/mqtt     {"enabled","host","port","user","password"} (any subset) -> 200 | 400
 */
#ifndef IRON_V_API_V1_H
#define IRON_V_API_V1_H

#include <stddef.h>

#define API_V1_PATH_LIGHT                "/api/v1/light"
#define API_V1_PATH_DEVICE               "/api/v1/device"
#define API_V1_PATH_MQTT                 "/api/v1/mqtt"
#define API_V1_KEY_MAX                   16U

void api_v1_register_routes(void);

void api_v1_light_get(const char *query, char *body, size_t max_len);
void api_v1_light_post(const char *query, char *body, size_t max_len);
void api_v1_device_get(const char *query, char *body, size_t max_len);
void api_v1_mqtt_get(const char *query, char *body, size_t max_len);
void api_v1_mqtt_post(const char *query, char *body, size_t max_len);

#endif /* IRON_V_API_V1_H */
