/*
 * src/task.c
 *
 * ESP32-C6 Cooperative Coroutine Task Engine & Scheduler
 * TRM RISC-V Unprivileged Architecture (Callee-saved registers: ra, sp, s0-s11)
 */

#include "task.h"
#include "systimer.h"
#include "string.h"
#include "io_constants.h"
#include "utils.h"

/* Pre-allocated static task stacks in HP SRAM DRAM */
static uint8_t s_internal_stacks[TASK_MAX_COUNT][TASK_DEFAULT_STACK_SIZE] __attribute__((aligned(TASK_STACK_ALIGNMENT)));

/* Task Control Block Table */
static task_control_block_t s_task_table[TASK_MAX_COUNT];
static uint32_t s_current_task_idx = 0U;
static uint32_t s_total_switches = 0U;
static uint8_t  s_initialized = 0U;

static void task_trampoline(void)
{
    task_control_block_t *curr = &s_task_table[s_current_task_idx];
    if (curr && curr->entry)
    {
        curr->entry(curr->arg);
    }
    task_exit();
}

void task_init(void)
{
    /* Clear all task control blocks */
    for (uint32_t i = 0; i < TASK_MAX_COUNT; i++)
    {
        s_task_table[i].sp               = 0U;
        s_task_table[i].stack_base       = 0U;
        s_task_table[i].stack_size       = 0U;
        s_task_table[i].state            = TASK_STATE_UNUSED;
        s_task_table[i].priority         = TASK_PRIORITY_DEFAULT;
        s_task_table[i].runtime_ticks    = 0U;
        s_task_table[i].name             = NULL;
        s_task_table[i].entry            = NULL;
        s_task_table[i].arg              = NULL;
        s_task_table[i].id               = i;
        s_task_table[i].yield_count      = 0U;
        s_task_table[i].last_switch_tick = 0ULL;
    }

    /* Slot 0 represents the main thread execution context */
    s_task_table[0].sp               = 0U;
    s_task_table[0].stack_base       = HP_DRAM_START_ADDR;
    s_task_table[0].stack_size       = (HP_DRAM_END_ADDR - HP_DRAM_START_ADDR);
    s_task_table[0].state            = TASK_STATE_RUNNING;
    s_task_table[0].priority         = TASK_PRIORITY_DEFAULT;
    s_task_table[0].name             = "main";
    s_task_table[0].id               = 0U;
    s_task_table[0].last_switch_tick = systimer_get_ticks();

    s_current_task_idx = 0U;
    s_total_switches   = 0U;
    s_initialized      = 1U;
}

int task_create(const char *name, task_entry_t entry, void *arg, uint32_t priority, uint8_t *stack_buf, uint32_t stack_size)
{
    if (!s_initialized)
    {
        task_init();
    }

    if (!entry)
    {
        return TASK_ERR_INVALID_PARAM;
    }

    if (priority < TASK_PRIORITY_MIN || priority > TASK_PRIORITY_MAX)
    {
        priority = TASK_PRIORITY_DEFAULT;
    }

    /* Find next available slot */
    int slot = -1;
    for (uint32_t i = 1; i < TASK_MAX_COUNT; i++)
    {
        if (s_task_table[i].state == TASK_STATE_UNUSED ||
            s_task_table[i].state == TASK_STATE_TERMINATED)
        {
            slot = (int)i;
            break;
        }
    }

    if (slot < 0)
    {
        return TASK_ERR_FULL; /* Task table full */
    }

    /* Assign stack: use caller's static stack or pre-allocated internal stack */
    uint8_t *stack_ptr = stack_buf;
    uint32_t size = stack_size;
    if (!stack_ptr || size < (TASK_FRAME_SIZE + TASK_STACK_ALIGNMENT))
    {
        stack_ptr = s_internal_stacks[slot];
        size = TASK_DEFAULT_STACK_SIZE;
    }

    /* Calculate 16-byte aligned top of stack */
    uint32_t stack_base = (uint32_t)(uintptr_t)stack_ptr;
    uint32_t stack_top  = (stack_base + size) & ~(TASK_STACK_ALIGNMENT - 1U);

    /* Allocate initial 64-byte callee-saved stack frame */
    uint32_t *sp = (uint32_t *)(uintptr_t)(stack_top - TASK_FRAME_SIZE);

    /* Frame layout matching task_switch_asm:
     * 0(sp)  = ra
     * 4(sp)  = s0
     * 8(sp)  = s1
     * ...
     * 48(sp) = s11
     */
    sp[0] = (uint32_t)(uintptr_t)task_trampoline; /* ra: first switch jumps here */
    for (uint32_t j = 1; j < 13; j++)
    {
        sp[j] = 0U; /* s0-s11 cleared */
    }

    /* Populate Task Control Block */
    s_task_table[slot].sp               = (uint32_t)(uintptr_t)sp;
    s_task_table[slot].stack_base       = stack_base;
    s_task_table[slot].stack_size       = size;
    s_task_table[slot].state            = TASK_STATE_READY;
    s_task_table[slot].priority         = priority;
    s_task_table[slot].runtime_ticks    = 0U;
    s_task_table[slot].name             = name ? name : "task";
    s_task_table[slot].entry            = entry;
    s_task_table[slot].arg              = arg;
    s_task_table[slot].id               = (uint32_t)slot;
    s_task_table[slot].yield_count      = 0U;
    s_task_table[slot].last_switch_tick = 0ULL;

    return slot;
}

void task_yield(void)
{
    if (!s_initialized)
    {
        return;
    }

    /* Find next ready task in round-robin sequence */
    uint32_t next_idx = s_current_task_idx;
    for (uint32_t i = 1; i <= TASK_MAX_COUNT; i++)
    {
        uint32_t candidate = (s_current_task_idx + i) % TASK_MAX_COUNT;
        if (s_task_table[candidate].state == TASK_STATE_READY)
        {
            next_idx = candidate;
            break;
        }
    }

    /* If no other ready task exists: continue current task or return if idle */
    if (next_idx == s_current_task_idx)
    {
        if (s_task_table[s_current_task_idx].state == TASK_STATE_TERMINATED)
        {
            /* Current task terminated and no other tasks ready: return to main (0) if not main */
            if (s_current_task_idx != 0U)
            {
                next_idx = 0U;
            }
            else
            {
                return;
            }
        }
        else
        {
            return;
        }
    }

    uint64_t now = systimer_get_ticks();
    uint64_t elapsed = now - s_task_table[s_current_task_idx].last_switch_tick;
    s_task_table[s_current_task_idx].runtime_ticks += (uint32_t)elapsed;
    s_task_table[s_current_task_idx].yield_count++;

    uint32_t prev_idx = s_current_task_idx;
    if (s_task_table[prev_idx].state == TASK_STATE_RUNNING)
    {
        s_task_table[prev_idx].state = TASK_STATE_READY;
    }

    s_task_table[next_idx].state            = TASK_STATE_RUNNING;
    s_task_table[next_idx].last_switch_tick = now;
    s_current_task_idx                      = next_idx;
    s_total_switches++;

    /* Perform low-level callee-saved register save/restore and SP switch */
    task_switch_asm(&s_task_table[prev_idx].sp, s_task_table[next_idx].sp);

    /* Context resumed */
    s_task_table[s_current_task_idx].last_switch_tick = systimer_get_ticks();
}

void task_exit(void)
{
    s_task_table[s_current_task_idx].state = TASK_STATE_TERMINATED;
    task_yield();

    /* Should never reach here */
    while (1)
    {
        /* Spin if scheduled after termination */
    }
}

int task_terminate(uint32_t task_id)
{
    if (task_id >= TASK_MAX_COUNT || s_task_table[task_id].state == TASK_STATE_UNUSED)
    {
        return TASK_ERR_INVALID_PARAM;
    }

    s_task_table[task_id].state = TASK_STATE_TERMINATED;
    if (task_id == s_current_task_idx)
    {
        task_yield();
    }
    return TASK_OK;
}

task_control_block_t *task_get_current(void)
{
    if (!s_initialized)
    {
        return NULL;
    }
    return &s_task_table[s_current_task_idx];
}

task_control_block_t *task_get_by_id(uint32_t task_id)
{
    if (task_id >= TASK_MAX_COUNT)
    {
        return NULL;
    }
    return &s_task_table[task_id];
}

uint32_t task_get_count(void)
{
    if (!s_initialized)
    {
        return 0U;
    }

    uint32_t count = 0U;
    for (uint32_t i = 0; i < TASK_MAX_COUNT; i++)
    {
        if (s_task_table[i].state != TASK_STATE_UNUSED &&
            s_task_table[i].state != TASK_STATE_TERMINATED)
        {
            count++;
        }
    }
    return count;
}

void task_get_status(task_scheduler_status_t *out_status)
{
    if (!out_status)
    {
        return;
    }

    out_status->active_task_count = task_get_count();
    out_status->total_switches    = s_total_switches;
    out_status->current_task_id   = s_current_task_idx;

    for (uint32_t i = 0; i < TASK_MAX_COUNT; i++)
    {
        out_status->tasks[i] = s_task_table[i];
    }
}

const char *task_state_name(task_state_t state)
{
    switch (state)
    {
        case TASK_STATE_READY:      return "READY";
        case TASK_STATE_RUNNING:    return "RUNNING";
        case TASK_STATE_BLOCKED:    return "BLOCKED";
        case TASK_STATE_TERMINATED: return "TERMINATED";
        default:                    return "UNUSED";
    }
}
