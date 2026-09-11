/*
 * src/ble_gatt.h
 *
 * Bluetooth Low Energy Generic Attribute Profile (GATT) Database & Dispatcher
 * Bluetooth Core Specification v5.3 (Vol 3, Part G)
 *
 * Defines static GATT attribute structures, standard 16-bit UUIDs, permissions,
 * and zero-allocation database traversal, read, and write operations.
 */

#ifndef IRON_V_BLE_GATT_H
#define IRON_V_BLE_GATT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================= */
/* GATT Attribute Permissions (Bluetooth Core Spec v5.3)                     */
/* ========================================================================= */
#define GATT_PERM_NONE                  0x00U
#define GATT_PERM_READ                  (1U << 0)
#define GATT_PERM_WRITE                 (1U << 1)
#define GATT_PERM_NOTIFY                (1U << 2)
#define GATT_PERM_INDICATE              (1U << 3)
#define GATT_PERM_READ_ENCRYPTED        (1U << 4)
#define GATT_PERM_WRITE_ENCRYPTED       (1U << 5)

/* ========================================================================= */
/* Standard Bluetooth 16-Bit Declarations & Service UUIDs                    */
/* ========================================================================= */
#define GATT_UUID_PRIMARY_SERVICE       0x2800U
#define GATT_UUID_SECONDARY_SERVICE     0x2801U
#define GATT_UUID_INCLUDE               0x2802U
#define GATT_UUID_CHARACTERISTIC        0x2803U

#define GATT_UUID_CHAR_EXT_PROPERTIES   0x2900U
#define GATT_UUID_CHAR_USER_DESCRIPTION 0x2901U
#define GATT_UUID_CLIENT_CHAR_CONFIG    0x2902U /* CCCD */
#define GATT_UUID_SERVER_CHAR_CONFIG    0x2903U

/* Service UUIDs */
#define GATT_UUID_SERVICE_GAP           0x1800U /* Generic Access */
#define GATT_UUID_SERVICE_GATT          0x1801U /* Generic Attribute */
#define GATT_UUID_SERVICE_DEVICE_INFO   0x180AU /* Device Information Service */
#define GATT_UUID_SERVICE_CUSTOM_AUTO   0xFFE0U /* Custom Automation Service */

/* Characteristic UUIDs */
#define GATT_UUID_CHAR_DEVICE_NAME      0x2A00U
#define GATT_UUID_CHAR_APPEARANCE       0x2A01U
#define GATT_UUID_CHAR_SYSTEM_ID        0x2A23U
#define GATT_UUID_CHAR_MODEL_NUMBER     0x2A24U
#define GATT_UUID_CHAR_FIRMWARE_REV     0x2A26U
#define GATT_UUID_CHAR_MANUFACTURER     0x2A29U
#define GATT_UUID_CHAR_CUSTOM_DATA      0xFFE1U

/* ========================================================================= */
/* Characteristic Property Flags (Bitmask in Characteristic Declaration)     */
/* ========================================================================= */
#define GATT_CHAR_PROP_BROADCAST        0x01U
#define GATT_CHAR_PROP_READ             0x02U
#define GATT_CHAR_PROP_WRITE_NO_RESP    0x04U
#define GATT_CHAR_PROP_WRITE            0x08U
#define GATT_CHAR_PROP_NOTIFY           0x10U
#define GATT_CHAR_PROP_INDICATE         0x20U
#define GATT_CHAR_PROP_AUTH_SIGNED_WR   0x40U
#define GATT_CHAR_PROP_EXT_PROPS        0x80U

/* ========================================================================= */
/* GATT Database Capacity & Limits                                           */
/* ========================================================================= */
#define GATT_MAX_ATTRIBUTES             32U
#define GATT_MAX_ATTR_VALUE_SIZE        64U

/* ========================================================================= */
/* Return Status Enumerations                                                */
/* ========================================================================= */
typedef enum {
    GATT_OK = 0,
    GATT_ERR_INVALID_HANDLE = -1,
    GATT_ERR_READ_NOT_PERMITTED = -2,
    GATT_ERR_WRITE_NOT_PERMITTED = -3,
    GATT_ERR_INVALID_ARG = -4,
    GATT_ERR_BUFFER_TOO_SMALL = -5,
    GATT_ERR_DB_FULL = -6
} gatt_status_t;

/* ========================================================================= */
/* Concrete Data Structures (docs/development-roadmap.md:649-656)            */
/* ========================================================================= */
typedef struct {
    uint16_t handle;
    uint16_t uuid;
    uint8_t permissions; /* Read, Write, Notify */
    uint8_t *value_ptr;
    uint16_t value_len;
} gatt_attribute_t;

/* ========================================================================= */
/* GATT Database Management APIs                                             */
/* ========================================================================= */

/* Initialize static GATT database with baseline services & characteristics */
gatt_status_t gatt_db_init(void);

/* Query attribute counts and verify database consistency */
uint16_t gatt_db_get_count(void);

/* Attribute lookup */
const gatt_attribute_t *gatt_db_find_by_handle(uint16_t handle);
const gatt_attribute_t *gatt_db_find_by_uuid(uint16_t uuid);

/* Attribute read / write operations */
gatt_status_t gatt_db_read(uint16_t handle, uint8_t *out_buf, uint16_t max_len, uint16_t *out_len);
gatt_status_t gatt_db_write(uint16_t handle, const uint8_t *data, uint16_t len);

#endif /* IRON_V_BLE_GATT_H */
