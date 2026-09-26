/*
 * src/ble_npl.h
 *
 * NimBLE Porting Layer (NPL) Bare-Metal Implementation for ESP32-C6
 *
 * Provides a freestanding, zero-allocation cooperative event queue, software
 * callout timers, atomic hardware critical sections, and synchronization
 * primitives for Bluetooth LE controller and host stacks.
 *
 * Design adapted from Apache Mynewt NimBLE NPL under Apache License 2.0.
 * Copyright (c) 2015-2023 The Apache Software Foundation.
 * Copyright (c) 2020-2024 Espressif Systems (Shanghai) Co., Ltd.
 * Copyright (c) 2026 Iron V Operating System Project.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef IRON_V_BLE_NPL_H
#define IRON_V_BLE_NPL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Symbolic Constants & Error Codes (Zero Magic Numbers Rule)                */
/* ========================================================================= */
#define BLE_NPL_TICKS_PER_SEC           1000U
#define BLE_NPL_TIME_FOREVER            0xFFFFFFFFU
#define BLE_NPL_TIME_MAX                0xFFFFFFFFU

#define BLE_NPL_TIME_AFTER(t1, t2)      ((int32_t)((uint32_t)(t1) - (uint32_t)(t2)) > 0)

typedef enum {
    BLE_NPL_OK       = 0,
    BLE_NPL_ENOENT   = 2,
    BLE_NPL_ENOMEM   = 12,
    BLE_NPL_EBUSY    = 16,
    BLE_NPL_EINVAL   = 22,
    BLE_NPL_ETIMEOUT = 110
} ble_npl_error_t;

typedef uint32_t ble_npl_time_t;

/* ========================================================================= */
/* Concrete Data Structures (Singly Linked FIFO Event Queue & Callouts)      */
/* ========================================================================= */

struct ble_npl_event;
typedef void (*ble_npl_event_fn)(struct ble_npl_event *ev);

struct ble_npl_event {
    bool queued;
    ble_npl_event_fn fn;
    void *arg;
    struct ble_npl_event *next;
};

struct ble_npl_eventq {
    struct ble_npl_event *head;
    struct ble_npl_event *tail;
    uint32_t count;
};

struct ble_npl_callout {
    struct ble_npl_event c_ev;
    struct ble_npl_eventq *c_evq;
    ble_npl_time_t c_ticks;
    bool c_active;
    struct ble_npl_callout *c_next;
};

struct ble_npl_mutex {
    volatile uint32_t lock;
};

struct ble_npl_sem {
    volatile uint16_t tokens;
};

typedef struct {
    uint32_t events_enqueued;
    uint32_t events_dispatched;
    uint32_t callouts_armed;
    uint32_t callouts_expired;
    uint32_t callouts_stopped;
    uint32_t active_callouts;
    uint32_t critical_nesting_depth;
} ble_npl_telemetry_t;

/* ========================================================================= */
/* Event API                                                                 */
/* ========================================================================= */
void ble_npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn fn, void *arg);
bool ble_npl_event_is_queued(const struct ble_npl_event *ev);
void *ble_npl_event_get_arg(const struct ble_npl_event *ev);
void ble_npl_event_set_arg(struct ble_npl_event *ev, void *arg);
void ble_npl_event_run(struct ble_npl_event *ev);

/* ========================================================================= */
/* Event Queue API                                                           */
/* ========================================================================= */
void ble_npl_eventq_init(struct ble_npl_eventq *evq);
void ble_npl_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev);
struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *evq, ble_npl_time_t tmo);
void ble_npl_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev);
bool ble_npl_eventq_is_empty(const struct ble_npl_eventq *evq);
struct ble_npl_eventq *ble_npl_eventq_dflt_get(void);

/* ========================================================================= */
/* Software Callout Timer API                                                */
/* ========================================================================= */
int ble_npl_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *evq,
                         ble_npl_event_fn ev_cb, void *ev_arg);
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *co, ble_npl_time_t ticks);
void ble_npl_callout_stop(struct ble_npl_callout *co);
bool ble_npl_callout_is_active(const struct ble_npl_callout *co);
ble_npl_time_t ble_npl_callout_get_ticks(const struct ble_npl_callout *co);
ble_npl_time_t ble_npl_callout_remaining_ticks(const struct ble_npl_callout *co, ble_npl_time_t now);

/* ========================================================================= */
/* Time & Tick Conversion API                                                */
/* ========================================================================= */
ble_npl_time_t ble_npl_time_get(void);
ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out_ticks);
ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *out_ms);
uint32_t ble_npl_time_ms_to_ticks32(uint32_t ms);
uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t ticks);
void ble_npl_time_delay(ble_npl_time_t ticks);

/* ========================================================================= */
/* Hardware Critical Section Management                                      */
/* ========================================================================= */
uint32_t ble_npl_hw_enter_critical(void);
void ble_npl_hw_exit_critical(uint32_t ctx);
bool ble_npl_hw_is_in_critical(void);
uint32_t ble_npl_hw_get_critical_depth(void);

/* ========================================================================= */
/* Synchronization Primitives (Mutex & Semaphore)                            */
/* ========================================================================= */
ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *mu);
ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *mu, ble_npl_time_t timeout);
ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *mu);

ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens);
ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *sem, ble_npl_time_t timeout);
ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *sem);
uint16_t ble_npl_sem_get_count(struct ble_npl_sem *sem);

/* ========================================================================= */
/* Super-Loop Background Dispatcher & Driver Telemetry                       */
/* ========================================================================= */
void ble_npl_service_background(void);
void ble_npl_get_telemetry(ble_npl_telemetry_t *out_telemetry);
uint32_t ble_npl_get_processed_count(void);
uint32_t ble_npl_get_active_callout_count(void);

#ifdef __cplusplus
}
#endif

#endif /* IRON_V_BLE_NPL_H */
