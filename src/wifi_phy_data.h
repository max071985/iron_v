/*
 * src/wifi_phy_data.h
 *
 * Iron V Canonical PHY Initialization Parameters & Types for ESP32-C6
 *
 * Design Choice Attribution:
 * Canonical baseband RF and analog PHY initialization parameter array for ESP32-C6
 * adapted from Espressif Systems' ESP-IDF PHY specifications
 * (components/esp_phy/esp32c6/phy_init_data.c and components/esp_phy/include/esp_phy_init.h).
 *
 * Conforms to Iron V Project Standards:
 * - Zero magic numbers: explicit uppercase #define constants for buffer geometry
 * - Freestanding bare-metal compliance: static ROM/flash table for PHY registration
 */

#ifndef IRON_V_WIFI_PHY_DATA_H
#define IRON_V_WIFI_PHY_DATA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PHY Initialization Data Byte Array Geometry */
#define WIFI_PHY_INIT_DATA_LEN       128U
#define WIFI_PHY_CAL_VERSION_LEN     4U
#define WIFI_PHY_CAL_MAC_LEN         6U
#define WIFI_PHY_CAL_OPAQUE_LEN      1894U

/* Calibration modes matching Espressif libphy.a */
#define PHY_RF_CAL_PARTIAL           0x00000000
#define PHY_RF_CAL_NONE              0x00000001
#define PHY_RF_CAL_FULL              0x00000002

/* Opaque PHY init parameters container */
typedef struct {
    uint8_t params[WIFI_PHY_INIT_DATA_LEN];
} esp_phy_init_data_t;

/* Opaque PHY calibration state */
typedef struct {
    uint8_t version[WIFI_PHY_CAL_VERSION_LEN];
    uint8_t mac[WIFI_PHY_CAL_MAC_LEN];
    uint8_t opaque[WIFI_PHY_CAL_OPAQUE_LEN];
} esp_phy_calibration_data_t;

/* Canonical factory PHY initialization parameters for ESP32-C6 */
extern const esp_phy_init_data_t phy_init_data;

#ifdef __cplusplus
}
#endif

#endif /* IRON_V_WIFI_PHY_DATA_H */
