/*
 * src/ble_npl.c
 *
 * NimBLE Porting Layer (NPL) Bare-Metal Implementation for ESP32-C6
 *
 * Implements freestanding event queues, software callouts, atomic CSR-based
 * critical sections, and cooperative background event dispatching.
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

#include "ble_npl.h"
#include "systimer.h"
#include "interrupt.h"
#include "string.h"

/* ========================================================================= */
/* Static Storage & Telemetry                                                */
/* ========================================================================= */

static struct ble_npl_eventq s_npl_default_evq;
static bool s_npl_initialized = false;

static struct ble_npl_callout *s_active_callouts_head = NULL;
static volatile uint32_t s_critical_nesting_depth = 0U;

static ble_npl_telemetry_t s_telemetry = {
    .events_enqueued         = 0U,
    .events_dispatched       = 0U,
    .callouts_armed          = 0U,
    .callouts_expired        = 0U,
    .callouts_stopped        = 0U,
    .active_callouts         = 0U,
    .critical_nesting_depth  = 0U
};

#if !defined(__riscv)
static volatile uint32_t s_mock_host_ticks = 0U;
#endif

/* ========================================================================= */
/* Hardware Critical Section Management (RISC-V Machine Mode CSRs)           */
/* ========================================================================= */

uint32_t ble_npl_hw_enter_critical(void)
{
#if defined(__riscv)
    uint32_t prev_mstatus;
    asm volatile("csrrci %0, mstatus, %1" : "=r"(prev_mstatus) : "i"(MSTATUS_MIE_BIT) : "memory");
    s_critical_nesting_depth++;
    s_telemetry.critical_nesting_depth = s_critical_nesting_depth;
    return prev_mstatus;
#else
    s_critical_nesting_depth++;
    s_telemetry.critical_nesting_depth = s_critical_nesting_depth;
    return 1U;
#endif
}

void ble_npl_hw_exit_critical(uint32_t ctx)
{
    if (s_critical_nesting_depth > 0U)
    {
        s_critical_nesting_depth--;
        s_telemetry.critical_nesting_depth = s_critical_nesting_depth;
    }

#if defined(__riscv)
    if (s_critical_nesting_depth == 0U && (ctx & MSTATUS_MIE_BIT))
    {
        asm volatile("csrsi mstatus, %0" :: "i"(MSTATUS_MIE_BIT) : "memory");
    }
#else
    (void)ctx;
#endif
}

bool ble_npl_hw_is_in_critical(void)
{
    return s_critical_nesting_depth > 0U;
}

uint32_t ble_npl_hw_get_critical_depth(void)
{
    return s_critical_nesting_depth;
}

/* ========================================================================= */
/* Time & Tick Management                                                    */
/* ========================================================================= */

ble_npl_time_t ble_npl_time_get(void)
{
#if defined(__riscv)
    return (ble_npl_time_t)systimer_get_ms();
#else
    return s_mock_host_ticks;
#endif
}

ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out_ticks)
{
    if (out_ticks == NULL)
    {
        return BLE_NPL_EINVAL;
    }
    *out_ticks = ms;
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *out_ms)
{
    if (out_ms == NULL)
    {
        return BLE_NPL_EINVAL;
    }
    *out_ms = ticks;
    return BLE_NPL_OK;
}

uint32_t ble_npl_time_ms_to_ticks32(uint32_t ms)
{
    return ms;
}

uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t ticks)
{
    return ticks;
}

void ble_npl_time_delay(ble_npl_time_t ticks)
{
#if defined(__riscv)
    systimer_delay_us((uint64_t)ticks * 1000ULL);
#else
    s_mock_host_ticks += ticks;
#endif
}

/* ========================================================================= */
/* Event API                                                                 */
/* ========================================================================= */

void ble_npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn fn, void *arg)
{
    if (ev == NULL)
    {
        return;
    }
    ev->queued = false;
    ev->fn = fn;
    ev->arg = arg;
    ev->next = NULL;
}

bool ble_npl_event_is_queued(const struct ble_npl_event *ev)
{
    return (ev != NULL) && ev->queued;
}

void *ble_npl_event_get_arg(const struct ble_npl_event *ev)
{
    return (ev != NULL) ? ev->arg : NULL;
}

void ble_npl_event_set_arg(struct ble_npl_event *ev, void *arg)
{
    if (ev != NULL)
    {
        ev->arg = arg;
    }
}

void ble_npl_event_run(struct ble_npl_event *ev)
{
    if (ev != NULL && ev->fn != NULL)
    {
        ev->fn(ev);
    }
}

/* ========================================================================= */
/* Event Queue API (Singly Linked FIFO with Atomic Insertion)               */
/* ========================================================================= */

void ble_npl_eventq_init(struct ble_npl_eventq *evq)
{
    if (evq == NULL)
    {
        return;
    }
    evq->head = NULL;
    evq->tail = NULL;
    evq->count = 0U;
}

struct ble_npl_eventq *ble_npl_eventq_dflt_get(void)
{
    if (!s_npl_initialized)
    {
        ble_npl_eventq_init(&s_npl_default_evq);
        s_npl_initialized = true;
    }
    return &s_npl_default_evq;
}

bool ble_npl_eventq_is_empty(const struct ble_npl_eventq *evq)
{
    return (evq == NULL) || (evq->head == NULL);
}

void ble_npl_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    if (evq == NULL || ev == NULL)
    {
        return;
    }

    uint32_t ctx = ble_npl_hw_enter_critical();

    if (ev->queued)
    {
        ble_npl_hw_exit_critical(ctx);
        return;
    }

    ev->queued = true;
    ev->next = NULL;

    if (evq->tail != NULL)
    {
        evq->tail->next = ev;
    }
    else
    {
        evq->head = ev;
    }
    evq->tail = ev;
    evq->count++;
    s_telemetry.events_enqueued++;

    ble_npl_hw_exit_critical(ctx);
}

struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *evq, ble_npl_time_t tmo)
{
    (void)tmo;
    if (evq == NULL)
    {
        return NULL;
    }

    uint32_t ctx = ble_npl_hw_enter_critical();

    struct ble_npl_event *ev = evq->head;
    if (ev != NULL)
    {
        evq->head = ev->next;
        if (evq->head == NULL)
        {
            evq->tail = NULL;
        }
        ev->next = NULL;
        ev->queued = false;
        evq->count--;
    }

    ble_npl_hw_exit_critical(ctx);
    return ev;
}

void ble_npl_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    if (evq == NULL || ev == NULL || !ev->queued)
    {
        return;
    }

    uint32_t ctx = ble_npl_hw_enter_critical();

    struct ble_npl_event *prev = NULL;
    struct ble_npl_event *cur = evq->head;

    while (cur != NULL)
    {
        if (cur == ev)
        {
            if (prev != NULL)
            {
                prev->next = cur->next;
            }
            else
            {
                evq->head = cur->next;
            }

            if (cur == evq->tail)
            {
                evq->tail = prev;
            }

            cur->next = NULL;
            cur->queued = false;
            if (evq->count > 0U)
            {
                evq->count--;
            }
            break;
        }
        prev = cur;
        cur = cur->next;
    }

    ble_npl_hw_exit_critical(ctx);
}

/* ========================================================================= */
/* Software Callout Timer API                                                */
/* ========================================================================= */

int ble_npl_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *evq,
                         ble_npl_event_fn ev_cb, void *ev_arg)
{
    if (co == NULL)
    {
        return (int)BLE_NPL_EINVAL;
    }

    ble_npl_event_init(&co->c_ev, ev_cb, ev_arg);
    co->c_evq = (evq != NULL) ? evq : ble_npl_eventq_dflt_get();
    co->c_ticks = 0U;
    co->c_active = false;
    co->c_next = NULL;

    return (int)BLE_NPL_OK;
}

ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *co, ble_npl_time_t ticks)
{
    if (co == NULL)
    {
        return BLE_NPL_EINVAL;
    }

    uint32_t ctx = ble_npl_hw_enter_critical();

    /* Remove from active list if already scheduled */
    if (co->c_active)
    {
        struct ble_npl_callout *prev = NULL;
        struct ble_npl_callout *cur = s_active_callouts_head;
        while (cur != NULL)
        {
            if (cur == co)
            {
                if (prev != NULL)
                {
                    prev->c_next = cur->c_next;
                }
                else
                {
                    s_active_callouts_head = cur->c_next;
                }
                break;
            }
            prev = cur;
            cur = cur->c_next;
        }
        co->c_active = false;
        if (s_telemetry.active_callouts > 0U)
        {
            s_telemetry.active_callouts--;
        }
    }

    /* Remove associated event from event queue if pending */
    if (co->c_ev.queued && co->c_evq != NULL)
    {
        ble_npl_eventq_remove(co->c_evq, &co->c_ev);
    }

    co->c_ticks = ble_npl_time_get() + ticks;
    co->c_active = true;

    /* Insert into active callouts list */
    co->c_next = s_active_callouts_head;
    s_active_callouts_head = co;
    s_telemetry.active_callouts++;
    s_telemetry.callouts_armed++;

    ble_npl_hw_exit_critical(ctx);
    return BLE_NPL_OK;
}

void ble_npl_callout_stop(struct ble_npl_callout *co)
{
    if (co == NULL)
    {
        return;
    }

    uint32_t ctx = ble_npl_hw_enter_critical();

    if (co->c_active)
    {
        struct ble_npl_callout *prev = NULL;
        struct ble_npl_callout *cur = s_active_callouts_head;
        while (cur != NULL)
        {
            if (cur == co)
            {
                if (prev != NULL)
                {
                    prev->c_next = cur->c_next;
                }
                else
                {
                    s_active_callouts_head = cur->c_next;
                }
                break;
            }
            prev = cur;
            cur = cur->c_next;
        }
        co->c_active = false;
        co->c_next = NULL;
        if (s_telemetry.active_callouts > 0U)
        {
            s_telemetry.active_callouts--;
        }
        s_telemetry.callouts_stopped++;
    }

    if (co->c_ev.queued && co->c_evq != NULL)
    {
        ble_npl_eventq_remove(co->c_evq, &co->c_ev);
    }

    ble_npl_hw_exit_critical(ctx);
}

bool ble_npl_callout_is_active(const struct ble_npl_callout *co)
{
    return (co != NULL) && co->c_active;
}

ble_npl_time_t ble_npl_callout_get_ticks(const struct ble_npl_callout *co)
{
    return (co != NULL) ? co->c_ticks : 0U;
}

ble_npl_time_t ble_npl_callout_remaining_ticks(const struct ble_npl_callout *co, ble_npl_time_t now)
{
    if (co == NULL || !co->c_active)
    {
        return 0U;
    }

    if (BLE_NPL_TIME_AFTER(now, co->c_ticks))
    {
        return 0U;
    }

    return (co->c_ticks - now);
}

/* ========================================================================= */
/* Synchronization Primitives (Mutex & Semaphore)                            */
/* ========================================================================= */

ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *mu)
{
    if (mu == NULL)
    {
        return BLE_NPL_EINVAL;
    }
    mu->lock = 0U;
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *mu, ble_npl_time_t timeout)
{
    if (mu == NULL)
    {
        return BLE_NPL_EINVAL;
    }

    ble_npl_time_t start = ble_npl_time_get();
    while (1)
    {
        uint32_t ctx = ble_npl_hw_enter_critical();
        if (mu->lock == 0U)
        {
            mu->lock = 1U;
            ble_npl_hw_exit_critical(ctx);
            return BLE_NPL_OK;
        }
        ble_npl_hw_exit_critical(ctx);

        if (timeout == 0U)
        {
            return BLE_NPL_ETIMEOUT;
        }

        if (timeout != BLE_NPL_TIME_FOREVER)
        {
            ble_npl_time_t now = ble_npl_time_get();
            if (BLE_NPL_TIME_AFTER(now, start + timeout))
            {
                return BLE_NPL_ETIMEOUT;
            }
        }
    }
}

ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *mu)
{
    if (mu == NULL)
    {
        return BLE_NPL_EINVAL;
    }
    uint32_t ctx = ble_npl_hw_enter_critical();
    mu->lock = 0U;
    ble_npl_hw_exit_critical(ctx);
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{
    if (sem == NULL)
    {
        return BLE_NPL_EINVAL;
    }
    sem->tokens = tokens;
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *sem, ble_npl_time_t timeout)
{
    if (sem == NULL)
    {
        return BLE_NPL_EINVAL;
    }

    ble_npl_time_t start = ble_npl_time_get();
    while (1)
    {
        uint32_t ctx = ble_npl_hw_enter_critical();
        if (sem->tokens > 0U)
        {
            sem->tokens--;
            ble_npl_hw_exit_critical(ctx);
            return BLE_NPL_OK;
        }
        ble_npl_hw_exit_critical(ctx);

        if (timeout == 0U)
        {
            return BLE_NPL_ETIMEOUT;
        }

        if (timeout != BLE_NPL_TIME_FOREVER)
        {
            ble_npl_time_t now = ble_npl_time_get();
            if (BLE_NPL_TIME_AFTER(now, start + timeout))
            {
                return BLE_NPL_ETIMEOUT;
            }
        }
    }
}

ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *sem)
{
    if (sem == NULL)
    {
        return BLE_NPL_EINVAL;
    }
    uint32_t ctx = ble_npl_hw_enter_critical();
    sem->tokens++;
    ble_npl_hw_exit_critical(ctx);
    return BLE_NPL_OK;
}

uint16_t ble_npl_sem_get_count(struct ble_npl_sem *sem)
{
    return (sem != NULL) ? sem->tokens : 0U;
}

/* ========================================================================= */
/* Super-Loop Background Dispatcher & Driver Telemetry                       */
/* ========================================================================= */

void ble_npl_service_background(void)
{
    ble_npl_time_t now = ble_npl_time_get();

    /* 1. Scan and process expired software callouts */
    uint32_t ctx = ble_npl_hw_enter_critical();
    struct ble_npl_callout *prev = NULL;
    struct ble_npl_callout *cur = s_active_callouts_head;

    while (cur != NULL)
    {
        struct ble_npl_callout *next = cur->c_next;

        if (!BLE_NPL_TIME_AFTER(cur->c_ticks, now))
        {
            /* Callout expired: remove from active list */
            if (prev != NULL)
            {
                prev->c_next = next;
            }
            else
            {
                s_active_callouts_head = next;
            }

            cur->c_active = false;
            cur->c_next = NULL;
            if (s_telemetry.active_callouts > 0U)
            {
                s_telemetry.active_callouts--;
            }
            s_telemetry.callouts_expired++;

            /* Enqueue callout event into its target event queue */
            if (cur->c_evq != NULL)
            {
                /* Safe enqueue under critical section */
                if (!cur->c_ev.queued)
                {
                    cur->c_ev.queued = true;
                    cur->c_ev.next = NULL;
                    if (cur->c_evq->tail != NULL)
                    {
                        cur->c_evq->tail->next = &cur->c_ev;
                    }
                    else
                    {
                        cur->c_evq->head = &cur->c_ev;
                    }
                    cur->c_evq->tail = &cur->c_ev;
                    cur->c_evq->count++;
                    s_telemetry.events_enqueued++;
                }
            }
        }
        else
        {
            prev = cur;
        }

        cur = next;
    }
    ble_npl_hw_exit_critical(ctx);

    /* 2. Dispatch pending events from default event queue */
    struct ble_npl_eventq *dflt_q = ble_npl_eventq_dflt_get();
    while (1)
    {
        struct ble_npl_event *ev = ble_npl_eventq_get(dflt_q, 0U);
        if (ev == NULL)
        {
            break;
        }
        ble_npl_event_run(ev);
        s_telemetry.events_dispatched++;
    }
}

void ble_npl_get_telemetry(ble_npl_telemetry_t *out_telemetry)
{
    if (out_telemetry == NULL)
    {
        return;
    }
    uint32_t ctx = ble_npl_hw_enter_critical();
    *out_telemetry = s_telemetry;
    ble_npl_hw_exit_critical(ctx);
}

uint32_t ble_npl_get_processed_count(void)
{
    return s_telemetry.events_dispatched;
}

uint32_t ble_npl_get_active_callout_count(void)
{
    return s_telemetry.active_callouts;
}
