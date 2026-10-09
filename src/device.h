/*
 * Iron V - Device identity (REV-22 spec, REV-33)
 *
 * One id for REST v1 (/api/v1/device) and the optional MQTT module (client id, topics):
 * "ironv-" + the last three bytes of the factory MAC in lowercase hex, e.g. "ironv-451e14".
 */
#ifndef IRON_V_DEVICE_H
#define IRON_V_DEVICE_H

#define DEVICE_ID_PREFIX                 "ironv-"
#define DEVICE_ID_MAC_BYTES              3U      /* last three bytes of the factory MAC */
#define DEVICE_ID_LEN                    16U     /* prefix + 6 hex + NUL, rounded up */

/* Computed from the eFuse MAC on first use */
const char *device_id(void);

#endif /* IRON_V_DEVICE_H */
