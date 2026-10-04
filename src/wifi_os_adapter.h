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

/* OSAL Subsystem lifecycle */
void wifi_os_adapter_init(void);
void wifi_os_adapter_poll(void);

/* OSAL Static Arena telemetry query */
void wifi_os_adapter_get_heap_stats(size_t *used_bytes, size_t *free_bytes, size_t *peak_bytes);

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

#endif /* IRON_V_WIFI_OS_ADAPTER_H */
