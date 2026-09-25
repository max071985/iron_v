/*
 * src/wifi_regulatory.h
 *
 * Iron V Bare-Metal Wi-Fi 6 Regulatory Domain & Channel Geometry Definitions
 *
 * Design Choice Attribution:
 * 802.11 regulatory channel boundaries, bandwidth limits, EIRP power constraints,
 * and regulatory domain country-code mapping structures adapted from Espressif Systems'
 * ESP-IDF Wi-Fi regulatory model (components/esp_wifi/src/esp_wifi_regulatory.c).
 *
 * Conforms to Iron V Project Standards:
 * - Zero magic numbers: explicit #define constants for channels, bandwidths, power levels
 * - Static tables only: freestanding bare-metal ROM/flash mapping
 */

#ifndef IRON_V_WIFI_REGULATORY_H
#define IRON_V_WIFI_REGULATORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of regulatory rules per domain in 2.4 GHz band */
#define WIFI_MAX_REGULATORY_RULE_NUM     2U

/* 2.4 GHz 802.11 Channel Boundaries */
#define WIFI_REG_CHAN_MIN                1U
#define WIFI_REG_CHAN_MAX_11             11U
#define WIFI_REG_CHAN_MAX_13             13U
#define WIFI_REG_CHAN_MAX_14             14U

/* Bandwidth Enumeration Indices (matching IEEE 802.11 / ESP-IDF) */
#define WIFI_REG_BW_20M                  1U
#define WIFI_REG_BW_40M                  2U

/* Maximum Equivalent Isotropically Radiated Power (EIRP) limits in dBm */
#define WIFI_REG_EIRP_20DBM              20U
#define WIFI_REG_EIRP_23DBM              23U
#define WIFI_REG_EIRP_26DBM              26U
#define WIFI_REG_EIRP_30DBM              30U
#define WIFI_REG_EIRP_33DBM              33U
#define WIFI_REG_EIRP_36DBM              36U

/* Dynamic Frequency Selection (DFS) flags */
#define WIFI_REG_DFS_DISABLED            0U
#define WIFI_REG_DFS_ENABLED             1U

/* Reserved Bitfield Padding */
#define WIFI_REG_RESERVED_ZERO           0U

/* Regulatory Domain Profile Types (indices into regulatory_data[]) */
typedef enum {
    ESP_WIFI_REGULATORY_TYPE_DEFAULT = 0,
    ESP_WIFI_REGULATORY_TYPE_CE,
    ESP_WIFI_REGULATORY_TYPE_ACMA,
    ESP_WIFI_REGULATORY_TYPE_ANATEL,
    ESP_WIFI_REGULATORY_TYPE_ISED,
    ESP_WIFI_REGULATORY_TYPE_SRRC,
    ESP_WIFI_REGULATORY_TYPE_OFCA,
    ESP_WIFI_REGULATORY_TYPE_WPC,
    ESP_WIFI_REGULATORY_TYPE_MIC,
    ESP_WIFI_REGULATORY_TYPE_KCC,
    ESP_WIFI_REGULATORY_TYPE_IFETEL,
    ESP_WIFI_REGULATORY_TYPE_RCM,
    ESP_WIFI_REGULATORY_TYPE_NCC,
    ESP_WIFI_REGULATORY_TYPE_FCC,
    ESP_WIFI_REGULATORY_TYPE_GT,
    ESP_WIFI_REGULATORY_TYPE_KE,
    ESP_WIFI_REGULATORY_TYPE_KP,
    ESP_WIFI_REGULATORY_TYPE_MY,
    ESP_WIFI_REGULATORY_TYPE_PK,
    ESP_WIFI_REGULATORY_TYPE_TG,
    ESP_WIFI_REGULATORY_TYPE_MAX,
} esp_wifi_regulatory_type_t;

/* Regulatory rule structure (4 bytes, 2-byte aligned) */
typedef struct {
    uint8_t  start_channel;      /* Start channel of regulatory rule */
    uint8_t  end_channel;        /* End channel of regulatory rule */
    uint16_t max_bandwidth : 3;  /* Max bandwidth (MHz): 1: 20M, 2: 40M */
    uint16_t max_eirp : 6;       /* Max EIRP (dBm) */
    uint16_t is_dfs : 1;         /* DFS channel flag */
    uint16_t reserved : 6;       /* Reserved bits */
} wifi_reg_rule_t;

/* Regulatory profile structure (10 bytes: 1 byte rule count, 1 byte pad, 2 * 4 byte rules) */
typedef struct {
    uint8_t         n_reg_rules;
    wifi_reg_rule_t reg_rules[WIFI_MAX_REGULATORY_RULE_NUM];
} wifi_regulatory_t;

/* Country code mapping structure (3 bytes packed: 2-byte ISO code + 1-byte regulatory type) */
typedef struct __attribute__((packed)) {
    char    cn[2];
    uint8_t regulatory_type;
} wifi_regdomain_t;

/* Country code table and regulatory rules linked by libnet80211.a */
extern const wifi_regdomain_t regdomain_table[];
extern const wifi_regulatory_t regulatory_data[];

#ifdef __cplusplus
}
#endif

#endif /* IRON_V_WIFI_REGULATORY_H */
