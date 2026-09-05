/*
 * src/dpc.h
 *
 * Lock-Free Single-Producer Single-Consumer (SPSC) Deferred Procedure Call (DPC) Queue Engine
 * Authoritative Architecture: ESP32-C6 RISC-V Bare-Metal Runtime
 *
 * Provides a lock-free ring buffer for deferring bottom-half interrupt work
 * to thread context without holding interrupts disabled or stalling ISRs.
 */

#ifndef DPC_H
#define DPC_H

#include <stdint.h>
#include <stddef.h>

/* DPC Event Types */
typedef enum {
    DPC_TYPE_UART0_RX       = 0,
    DPC_TYPE_USB_SERIAL_RX  = 1,
    DPC_TYPE_TIMER_TICK     = 2,
    DPC_TYPE_WIFI_PACKET    = 3,
    DPC_TYPE_BLE_EVENT      = 4,
    DPC_TYPE_LP_MAILBOX     = 5,
    DPC_TYPE_TEST_EVENT     = 6
} dpc_type_t;

/* Callback handler type for DPC events */
typedef void (*dpc_handler_t)(uint32_t arg0, uint32_t arg1);

/* DPC Event Structure */
typedef struct {
    dpc_type_t    type;
    uint32_t      arg0;
    uint32_t      arg1;
    dpc_handler_t handler;
} dpc_event_t;

/* Queue Capacity - strictly a power of two for bitwise mask wrapping */
#define DPC_QUEUE_CAPACITY          64U
#define DPC_QUEUE_MASK              (DPC_QUEUE_CAPACITY - 1U)

/* SPSC DPC Ring Buffer Queue Structure */
typedef struct {
    dpc_event_t      events[DPC_QUEUE_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t drop_count;
} dpc_queue_t;

/* DPC Status / Return Codes */
typedef enum {
    DPC_STATUS_OK                 =  0,
    DPC_STATUS_ERR_FULL           = -1,
    DPC_STATUS_ERR_EMPTY          = -2,
    DPC_STATUS_ERR_INVALID        = -3
} dpc_status_t;

/* --- Instance API (operates on any dpc_queue_t instance) --- */

/* Initialize a DPC queue instance to empty state */
void dpc_queue_init(dpc_queue_t *q);

/* Lock-free SPSC enqueue into a specific queue */
int dpc_queue_enqueue(dpc_queue_t *q, const dpc_event_t *event);

/* Lock-free SPSC dequeue from a specific queue */
int dpc_queue_dequeue(dpc_queue_t *q, dpc_event_t *event);

/* Query state of a specific queue */
uint32_t dpc_queue_size(const dpc_queue_t *q);
uint32_t dpc_queue_is_empty(const dpc_queue_t *q);
uint32_t dpc_queue_is_full(const dpc_queue_t *q);

/* --- Global System DPC Engine API --- */

/* Initialize the global system DPC engine */
void dpc_init(void);

/* Enqueue a DPC event into the global system queue */
int dpc_enqueue(dpc_type_t type, uint32_t arg0, uint32_t arg1, dpc_handler_t handler);

/* Enqueue an event struct into the global system queue */
int dpc_enqueue_event(const dpc_event_t *event);

/* Dequeue an event from the global system queue */
int dpc_dequeue(dpc_event_t *event);

/* Dequeue and execute a single event from the global system queue */
int dpc_process(void);

/* Dequeue and execute all pending events in the global system queue */
uint32_t dpc_process_all(void);

/* Query global system queue telemetry */
uint32_t dpc_get_size(void);
uint32_t dpc_get_drop_count(void);
uint32_t dpc_get_processed_count(void);

/* Snapshot global queue state and counters */
void dpc_get_stats(dpc_queue_t *stats_out);

#endif /* DPC_H */
