/*
 * src/wifi_os_adapter.h
 *
 * Header interface for the bare-metal Operating System Abstraction Layer (OSAL)
 * hosting Espressif closed-source Wi-Fi & PHY static libraries.
 */

#ifndef IRON_V_WIFI_OS_ADAPTER_H
#define IRON_V_WIFI_OS_ADAPTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "wifi_vendor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum simultaneously tracked software timers */
#define WIFI_MAX_ACTIVE_TIMERS 24U
/* PHY PLL tracking period (ESP-IDF CONFIG_ESP_PHY_PLL_TRACK_PERIOD_MS default) */
#define WIFI_PHY_PLL_TRACK_PERIOD_US 1000000ULL

/* OSAL Subsystem lifecycle */
void wifi_os_adapter_init(void);
void wifi_os_adapter_poll(void);
void wifi_os_adapter_print_timers(void);

/* OSAL Static Arena telemetry query */
void wifi_os_adapter_get_heap_stats(size_t *used_bytes, size_t *free_bytes, size_t *peak_bytes);
void wifi_os_adapter_heap_peak_reset(void);

/* WPA Supplicant callbacks registration */
void wifi_os_adapter_register_wpa_stubs(void);

/* Memory allocations within the OSAL static arena */
void *wifi_osi_malloc(size_t size);
void *wifi_osi_zalloc(size_t size);
void *wifi_osi_calloc(size_t n, size_t size);
void *wifi_osi_realloc(void *ptr, size_t size);
void  wifi_osi_free(void *ptr);

#ifdef __cplusplus
}
#endif

/* Blob task stack (the blob asks for its size in task_create) and its high-water fill pattern */
#define WIFI_TASK_STACK_BUF_SIZE 8192U
#define WIFI_TASK_STACK_FILL   0xA5U
uint32_t wifi_os_adapter_task_stack_free(uint32_t *out_size, uint32_t *out_requested);

#endif /* IRON_V_WIFI_OS_ADAPTER_H */
