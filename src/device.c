/*
 * Iron V - Device identity (REV-33). See device.h.
 */
#include "device.h"
#include "efuse.h"
#include "string.h"

#define DEVICE_HEX_DIGITS                "0123456789abcdef"
#define DEVICE_NIBBLE_SHIFT              4U
#define DEVICE_NIBBLE_MASK               0x0FU

static char s_device_id[DEVICE_ID_LEN];

const char *device_id(void)
{
    if (s_device_id[0] != '\0')
    {
        return s_device_id;
    }
    uint8_t mac[EFUSE_MAC_LEN];
    memset(mac, 0, sizeof(mac));
    (void)efuse_get_mac(mac);
    size_t pos = strlen(DEVICE_ID_PREFIX);
    memcpy(s_device_id, DEVICE_ID_PREFIX, pos);
    for (uint32_t i = EFUSE_MAC_LEN - DEVICE_ID_MAC_BYTES; i < EFUSE_MAC_LEN; i++)
    {
        s_device_id[pos++] = DEVICE_HEX_DIGITS[(mac[i] >> DEVICE_NIBBLE_SHIFT) & DEVICE_NIBBLE_MASK];
        s_device_id[pos++] = DEVICE_HEX_DIGITS[mac[i] & DEVICE_NIBBLE_MASK];
    }
    s_device_id[pos] = '\0';
    return s_device_id;
}
