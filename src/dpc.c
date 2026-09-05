/*
 * src/dpc.c
 *
 * Lock-Free Single-Producer Single-Consumer (SPSC) Deferred Procedure Call (DPC) Queue Engine
 * Authoritative Architecture: ESP32-C6 RISC-V Bare-Metal Runtime
 */

#include "dpc.h"

#if defined(__riscv)
#include "interrupt.h"
#define DPC_BARRIER_RELEASE() asm volatile("fence rw, w" ::: "memory")
#define DPC_BARRIER_ACQUIRE() asm volatile("fence r, rw" ::: "memory")
#define DPC_BARRIER_FULL()    asm volatile("fence rw, rw" ::: "memory")
#else
#define DPC_BARRIER_RELEASE() __atomic_thread_fence(__ATOMIC_RELEASE)
#define DPC_BARRIER_ACQUIRE() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define DPC_BARRIER_FULL()    __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

/* Global System DPC Queue and Telemetry Counters */
static dpc_queue_t g_system_dpc_queue;
static volatile uint32_t g_dpc_processed_count = 0U;

void dpc_queue_init(dpc_queue_t *q)
{
    if (!q) return;

    q->head = 0U;
    q->tail = 0U;
    q->drop_count = 0U;

    for (uint32_t i = 0U; i < DPC_QUEUE_CAPACITY; i++)
    {
        q->events[i].type = DPC_TYPE_UART0_RX;
        q->events[i].arg0 = 0U;
        q->events[i].arg1 = 0U;
        q->events[i].handler = NULL;
    }

    DPC_BARRIER_FULL();
}

int dpc_queue_enqueue(dpc_queue_t *q, const dpc_event_t *event)
{
    if (!q || !event)
    {
        return DPC_STATUS_ERR_INVALID;
    }

    uint32_t current_head = q->head;
    uint32_t current_tail = q->tail;

    /* Check if queue is full (capacity reached) */
    if ((uint32_t)(current_head - current_tail) >= DPC_QUEUE_CAPACITY)
    {
        q->drop_count++;
        return DPC_STATUS_ERR_FULL;
    }

    /* Store event at head slot (masked for circular wrap) */
    uint32_t slot = current_head & DPC_QUEUE_MASK;
    q->events[slot] = *event;

    /* Release barrier: Ensure event data write completes before head is incremented */
    DPC_BARRIER_RELEASE();

    q->head = current_head + 1U;
    return DPC_STATUS_OK;
}

int dpc_queue_dequeue(dpc_queue_t *q, dpc_event_t *event)
{
    if (!q || !event)
    {
        return DPC_STATUS_ERR_INVALID;
    }

    uint32_t current_tail = q->tail;
    uint32_t current_head = q->head;

    /* Check if queue is empty */
    if (current_tail == current_head)
    {
        return DPC_STATUS_ERR_EMPTY;
    }

    /* Acquire barrier: Ensure head read completes before event data read */
    DPC_BARRIER_ACQUIRE();

    uint32_t slot = current_tail & DPC_QUEUE_MASK;
    *event = q->events[slot];

    /* Release barrier: Ensure event data read completes before tail is incremented */
    DPC_BARRIER_RELEASE();

    q->tail = current_tail + 1U;
    return DPC_STATUS_OK;
}

uint32_t dpc_queue_size(const dpc_queue_t *q)
{
    if (!q) return 0U;
    uint32_t current_head = q->head;
    uint32_t current_tail = q->tail;
    return (uint32_t)(current_head - current_tail);
}

uint32_t dpc_queue_is_empty(const dpc_queue_t *q)
{
    if (!q) return 1U;
    return (q->head == q->tail) ? 1U : 0U;
}

uint32_t dpc_queue_is_full(const dpc_queue_t *q)
{
    if (!q) return 0U;
    uint32_t current_head = q->head;
    uint32_t current_tail = q->tail;
    return ((uint32_t)(current_head - current_tail) >= DPC_QUEUE_CAPACITY) ? 1U : 0U;
}

void dpc_init(void)
{
    dpc_queue_init(&g_system_dpc_queue);
    g_dpc_processed_count = 0U;
}

int dpc_enqueue(dpc_type_t type, uint32_t arg0, uint32_t arg1, dpc_handler_t handler)
{
    dpc_event_t ev;
    ev.type = type;
    ev.arg0 = arg0;
    ev.arg1 = arg1;
    ev.handler = handler;

    return dpc_enqueue_event(&ev);
}

int dpc_enqueue_event(const dpc_event_t *event)
{
    if (!event)
    {
        return DPC_STATUS_ERR_INVALID;
    }

#if defined(__riscv)
    /* Guard global system queue against concurrent nested ISR producer arbitration */
    uint32_t prev_mstatus = interrupt_global_save_and_disable();
    int result = dpc_queue_enqueue(&g_system_dpc_queue, event);
    interrupt_global_restore(prev_mstatus);
    return result;
#else
    return dpc_queue_enqueue(&g_system_dpc_queue, event);
#endif
}

int dpc_dequeue(dpc_event_t *event)
{
    return dpc_queue_dequeue(&g_system_dpc_queue, event);
}

int dpc_process(void)
{
    dpc_event_t ev;
    int res = dpc_dequeue(&ev);
    if (res != DPC_STATUS_OK)
    {
        return 0;
    }

    if (ev.handler != NULL)
    {
        ev.handler(ev.arg0, ev.arg1);
    }

    g_dpc_processed_count++;
    return 1;
}

uint32_t dpc_process_all(void)
{
    uint32_t count = 0U;
    while (dpc_process() > 0)
    {
        count++;
    }
    return count;
}

uint32_t dpc_get_size(void)
{
    return dpc_queue_size(&g_system_dpc_queue);
}

uint32_t dpc_get_drop_count(void)
{
    return g_system_dpc_queue.drop_count;
}

uint32_t dpc_get_processed_count(void)
{
    return g_dpc_processed_count;
}

void dpc_get_stats(dpc_queue_t *stats_out)
{
    if (!stats_out) return;
    *stats_out = g_system_dpc_queue;
}
