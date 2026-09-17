/*
 * src/vendor/include/sdkconfig.h
 *
 * Minimal vendor SDK configuration & freestanding RTOS shim types.
 */

#ifndef SDKCONFIG_H
#define SDKCONFIG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define CONFIG_IDF_TARGET_ARCH_RISCV            1
#define CONFIG_IDF_TARGET_ESP32C6               1
#define CONFIG_IDF_TARGET                       "esp32c6"

#define CONFIG_SOC_WIFI_SUPPORTED               1
#define CONFIG_ESP_WIFI_ENABLED                 1
#define CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM    10
#define CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM   32
#define CONFIG_ESP_WIFI_TX_BUFFER_TYPE          1
#define CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM    0
#define CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM   32
#define CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF     1
#define CONFIG_ESP_WIFI_RX_MGMT_BUF_NUM_DEF     5
#define CONFIG_ESP_WIFI_MGMT_SBUF_NUM           32
#define CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM     4
#define CONFIG_ESP_WIFI_AMPDU_TX_ENABLED        0
#define CONFIG_ESP_WIFI_AMPDU_RX_ENABLED        0
#define CONFIG_ESP_WIFI_RX_BA_WIN               4
#define CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM  0

#define CONFIG_ESP_PHY_MAX_TX_POWER             20
#define CONFIG_ESP_PHY_MAX_WIFI_TX_POWER        20
#define CONFIG_ESP_WIFI_FTM_ENABLE              1

/* Freestanding RTOS shim types */
typedef uint32_t                                TickType_t;
typedef uint32_t                                UBaseType_t;
typedef int32_t                                 BaseType_t;
typedef void*                                   QueueHandle_t;

#endif /* SDKCONFIG_H */
#define CONFIG_SOC_WIFI_HE_SUPPORT 1
