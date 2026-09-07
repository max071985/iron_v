/*
 * src/task.h
 *
 * ESP32-C6 Cooperative Coroutine Task Engine & Scheduler
 * TRM RISC-V Unprivileged Architecture (Callee-saved registers: ra, sp, s0-s11)
 *
 * Provides deterministic cooperative multitasking, static stack isolation,
 * zero-heap coroutine creation, and callee-saved context switching.
 */

#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>

/* Task Scheduling Constants */
#define TASK_MAX_COUNT              8U
#define TASK_DEFAULT_STACK_SIZE     2048U
#define TASK_STACK_ALIGNMENT        16U
#define TASK_FRAME_SIZE             64U
#define TASK_NAME_MAX_LEN           16U
#define TASK_PRIORITY_DEFAULT       10U
#define TASK_PRIORITY_MIN           1U
#define TASK_PRIORITY_MAX           15U

/* Task States */
typedef enum {
    TASK_STATE_UNUSED = 0,
    TASK_STATE_READY,
    TASK_STATE_RUNNING,
    TASK_STATE_BLOCKED,
    TASK_STATE_TERMINATED
} task_state_t;

/* Task Function Pointer */
typedef void (*task_entry_t)(void *arg);

/* Concrete Task Control Block (per Roadmap §3.3) */
typedef struct {
    uint32_t            sp;               /* Saved stack pointer */
    uint32_t            stack_base;       /* Stack allocation base address */
    uint32_t            stack_size;       /* Stack size in bytes */
    task_state_t        state;            /* Current execution state */
    uint32_t            priority;         /* Task priority (1-15) */
    uint32_t            runtime_ticks;    /* Total elapsed runtime in ticks */
    const char          *name;            /* Human-readable task name */
    task_entry_t        entry;            /* Task entry function */
    void                *arg;             /* Entry argument */
    uint32_t            id;               /* Unique task identifier */
    uint32_t            yield_count;      /* Total times task yielded */
    uint64_t            last_switch_tick; /* Last context switch timestamp */
} task_control_block_t;

/* Scheduler Telemetry Structure */
typedef struct {
    uint32_t active_task_count;
    uint32_t total_switches;
    uint32_t current_task_id;
    task_control_block_t tasks[TASK_MAX_COUNT];
} task_scheduler_status_t;

/* Lifecycle & Task Management Primitives */
void     task_init(void);
int      task_create(const char *name, task_entry_t entry, void *arg, uint32_t priority, uint8_t *stack_buf, uint32_t stack_size);
void     task_yield(void);
void     task_exit(void);
int      task_terminate(uint32_t task_id);

/* Query & Status APIs */
task_control_block_t *task_get_current(void);
task_control_block_t *task_get_by_id(uint32_t task_id);
uint32_t              task_get_count(void);
void                  task_get_status(task_scheduler_status_t *out_status);
const char           *task_state_name(task_state_t state);

/* Low-level Context Switcher (implemented in src/task_switch.S) */
extern void task_switch_asm(uint32_t *prev_sp_ptr, uint32_t next_sp);

#endif /* TASK_H */
