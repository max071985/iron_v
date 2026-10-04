/*
 * src/wifi_os_adapter.c
 *
 * Bare-Metal Operating System Abstraction Layer (OSAL) for Espressif Wi-Fi & PHY Libraries
 * Bridges Espressif vendor static libraries (libnet80211.a, libpp.a, libphy.a, libcore.a)
 * to Iron-V bare-metal runtime:
 * - Deterministic static arena memory allocator (56 KB static pool, zero dynamic heap calls)
 * - INTMTX & INTPRI interrupt routing, priority configuration, and atomic state restore
 * - High-resolution 64-bit SYSTIMER microsecond hardware timebase & ets_timer dispatcher
 * - Cooperative coroutine queues, semaphores, mutexes, and event groups
 * - Modem clock gating & PHY RF calibration
 * - Hardware TRNG for cryptographically secure pseudo-random numbers
 */

#include "wifi_os_adapter.h"
#include "interrupt.h"
#include "systimer.h"
#include "task.h"
#include "modem.h"
#include "wifi.h"
#include "console.h"
#include "string.h"
#include "utils.h"
#include "io_constants.h"
#include "regs/efuse.h"
#include "regs/lp_peri.h"
#include "regs/modem_rf.h"
#include "wifi_phy_data.h"
#include "wifi_vendor_types.h"
#include "wdt.h"
#include "wpa2_client.h"

/* ========================================================================= */
/* 1. Deterministic Static Memory Arena for Wi-Fi Subsystem (54 KB)          */
/* Zero dynamic heap memory calls permitted (AGENTS.md execution standard)   */
/* ========================================================================= */
#define WIFI_HEAP_SIZE          (54U * 1024U)
#define WIFI_BLOCK_MAGIC        0xA55AU
#define WIFI_ALLOC_ALIGN_MASK   7U

typedef struct wifi_block {
    size_t size;              /* Usable payload size in bytes */
    struct wifi_block *next;  /* Next physical block in arena */
    uint16_t is_free;         /* 1 if free, 0 if allocated */
    uint16_t magic;           /* 0xA55A validation magic */
} wifi_block_t;

static uint8_t s_wifi_heap[WIFI_HEAP_SIZE] __attribute__((aligned(16)));
static wifi_block_t *s_heap_head = NULL;
static size_t s_allocated_bytes = 0U;
static size_t s_peak_bytes = 0U;
static void wifi_timer_invalidate_handle(void *ptr);

static void wifi_heap_init(void)
{
    s_heap_head = (wifi_block_t *)(void *)s_wifi_heap;
    s_heap_head->size = WIFI_HEAP_SIZE - sizeof(wifi_block_t);
    s_heap_head->next = NULL;
    s_heap_head->is_free = 1U;
    s_heap_head->magic = WIFI_BLOCK_MAGIC;
    s_allocated_bytes = 0U;
    s_peak_bytes = 0U;
}

static void wifi_heap_coalesce(void)
{
    wifi_block_t *curr = s_heap_head;
    while (curr != NULL && curr->next != NULL)
    {
        if (curr->is_free && curr->next->is_free)
        {
            curr->size += sizeof(wifi_block_t) + curr->next->size;
            curr->next = curr->next->next;
        }
        else
        {
            curr = curr->next;
        }
    }
}

void *wifi_osi_malloc(size_t size)
{
    if (size == 0U)
    {
        return NULL;
    }

    /* 8-byte alignment */
    size = (size + WIFI_ALLOC_ALIGN_MASK) & ~WIFI_ALLOC_ALIGN_MASK;

    uint32_t prev_mstatus = interrupt_global_save_and_disable();

    if (s_heap_head == NULL)
    {
        wifi_heap_init();
    }

    wifi_block_t *curr = s_heap_head;
    wifi_block_t *best = NULL;

    /* First fit search */
    while (curr != NULL)
    {
        if (curr->magic != WIFI_BLOCK_MAGIC)
        {
            console_puts("[wifi_malloc] CORRUPT: curr=0x");
            put_hex((uint32_t)(uintptr_t)curr);
            console_puts(" magic=0x");

            put_hex(curr->magic);
            console_puts("\r\n");
            interrupt_global_restore(prev_mstatus);
            return NULL; /* Memory corruption detected */
        }

        if (curr->is_free && curr->size >= size)
        {
            best = curr;
            break;
        }
        curr = curr->next;
    }

    if (best == NULL)
    {
        /* Coalesce adjacent free blocks and retry */
        wifi_heap_coalesce();
        curr = s_heap_head;
        while (curr != NULL)
        {
            if (curr->is_free && curr->size >= size)
            {
                best = curr;
                break;
            }
            curr = curr->next;
        }
    }

    if (best == NULL)
    {
        console_puts("[wifi_malloc] OOM: need=");
        put_dec((uint32_t)size);
        console_puts(" used=");
        put_dec((uint32_t)s_allocated_bytes);
        console_puts(" / ");
        put_dec(WIFI_HEAP_SIZE);
        console_puts("\r\n");
        interrupt_global_restore(prev_mstatus);
        return NULL; /* Out of memory in Wi-Fi arena */
    }

    /* Split if remaining space can hold another block descriptor + payload */
    if (best->size >= size + sizeof(wifi_block_t) + 16U)
    {
        wifi_block_t *new_block = (wifi_block_t *)(void *)((uint8_t *)(best + 1) + size);
        new_block->size = best->size - size - sizeof(wifi_block_t);
        new_block->next = best->next;
        new_block->is_free = 1U;
        new_block->magic = WIFI_BLOCK_MAGIC;

        best->size = size;
        best->next = new_block;
    }

    best->is_free = 0U;
    s_allocated_bytes += best->size;
    if (s_allocated_bytes > s_peak_bytes)
    {
        s_peak_bytes = s_allocated_bytes;
    }

    interrupt_global_restore(prev_mstatus);
    return (void *)(best + 1);
}

void wifi_osi_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    uint32_t prev_mstatus = interrupt_global_save_and_disable();

    wifi_block_t *block = (wifi_block_t *)ptr - 1;
    if (block->magic == WIFI_BLOCK_MAGIC && !block->is_free)
    {
        block->is_free = 1U;
        if (s_allocated_bytes >= block->size)
        {
            s_allocated_bytes -= block->size;
        }
        wifi_heap_coalesce();
    }

    /* Invalidate any timer tracker referencing freed memory */
    wifi_timer_invalidate_handle(ptr);

    interrupt_global_restore(prev_mstatus);
}

void *wifi_osi_calloc(size_t n, size_t size)
{
    size_t total = n * size;
    void *p = wifi_osi_malloc(total);
    if (p != NULL)
    {
        memset(p, 0, total);
    }
    return p;
}

void *wifi_osi_zalloc(size_t size)
{
    return wifi_osi_calloc(1U, size);
}

void *wifi_osi_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
    {
        return wifi_osi_malloc(size);
    }
    if (size == 0U)
    {
        wifi_osi_free(ptr);
        return NULL;
    }

    wifi_block_t *block = (wifi_block_t *)ptr - 1;
    if (block->magic != WIFI_BLOCK_MAGIC)
    {
        return NULL;
    }

    if (block->size >= size)
    {
        return ptr;
    }

    void *new_p = wifi_osi_malloc(size);
    if (new_p != NULL)
    {
        memcpy(new_p, ptr, block->size);
        wifi_osi_free(ptr);
    }
    return new_p;
}

void wifi_os_adapter_get_heap_stats(size_t *used_bytes, size_t *free_bytes, size_t *peak_bytes)
{
    if (used_bytes != NULL) *used_bytes = s_allocated_bytes;
    if (free_bytes != NULL) *free_bytes = (WIFI_HEAP_SIZE > s_allocated_bytes) ? (WIFI_HEAP_SIZE - s_allocated_bytes) : 0U;
    if (peak_bytes != NULL) *peak_bytes = s_peak_bytes;
}

#if defined(__riscv)

static uint32_t get_free_heap_size_wrapper(void)
{
    return (uint32_t)((WIFI_HEAP_SIZE > s_allocated_bytes) ? (WIFI_HEAP_SIZE - s_allocated_bytes) : 0U);
}

/* ========================================================================= */

/* 2. Interrupt Management & Hardware State Mapping                          */
/* ========================================================================= */
static void set_intr_wrapper(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num, int32_t intr_prio)
{
    (void)cpu_no;
    console_puts("[wifi_os] set_intr src=");
    put_dec(intr_source);
    console_puts(" num=");
    put_dec(intr_num);
    console_puts(" prio=");
    put_dec((uint32_t)intr_prio);
    console_puts("\r\n");
    interrupt_route((interrupt_source_t)intr_source, intr_num);
    interrupt_set_priority(intr_num, (uint32_t)intr_prio);
    interrupt_set_type(intr_num, INTR_TYPE_LEVEL);
}

static void clear_intr_wrapper(uint32_t intr_source, uint32_t intr_num)
{
    (void)intr_source;
    (void)intr_num;
}

static void set_isr_wrapper(int32_t n, void *f, void *arg)
{
    console_puts("[wifi_os] set_isr n=");
    put_dec((uint32_t)n);
    console_puts(" f=0x");
    put_hex((uint32_t)(uintptr_t)f);
    console_puts("\r\n");
    interrupt_register_handler((uint32_t)n, (isr_handler_t)f, arg);
}

static void enable_intr_wrapper(uint32_t intr_mask)
{
    console_puts("[wifi_os] enable_intr mask=0x");
    put_hex(intr_mask);
    console_puts("\r\n");
    for (uint32_t ch = 0U; ch < INTERRUPT_CPU_CHANNELS; ch++)
    {
        if ((intr_mask & (1U << ch)) != 0U)
        {
            interrupt_enable(ch);
        }
    }
}

static void disable_intr_wrapper(uint32_t intr_mask)
{
    for (uint32_t ch = 0U; ch < INTERRUPT_CPU_CHANNELS; ch++)
    {
        if ((intr_mask & (1U << ch)) != 0U)
        {
            interrupt_disable(ch);
        }
    }
}

static bool is_from_isr_wrapper(void)
{
    return interrupt_in_isr();
}

/* ========================================================================= */
/* 3. Synchronization: Spinlocks & Mutexes (Real Hardware Protection)        */
/* Zero fake returns; authentic interrupt state save and task yielding       */
/* ========================================================================= */
typedef struct {
    volatile uint32_t lock;
} baremetal_spinlock_t;

static void *spin_lock_create_wrapper(void)
{
    baremetal_spinlock_t *l = (baremetal_spinlock_t *)wifi_osi_malloc(sizeof(baremetal_spinlock_t));
    if (l != NULL)
    {
        l->lock = 0U;
    }
    return (void *)l;
}

static void spin_lock_delete_wrapper(void *lock)
{
    if (lock != NULL)
    {
        wifi_osi_free(lock);
    }
}

static uint32_t wifi_int_disable_wrapper(void *wifi_int_mux)
{
    (void)wifi_int_mux;
    return interrupt_global_save_and_disable();
}

static void wifi_int_restore_wrapper(void *wifi_int_mux, uint32_t tmp)
{
    (void)wifi_int_mux;
    interrupt_global_restore(tmp);
}

typedef struct {
    volatile uint32_t owner;
    volatile uint32_t count;
    bool is_recursive;
} baremetal_mutex_t;

static void *mutex_create_wrapper(void)
{
    baremetal_mutex_t *m = (baremetal_mutex_t *)wifi_osi_malloc(sizeof(baremetal_mutex_t));
    if (m != NULL)
    {
        m->owner = 0U;
        m->count = 0U;
        m->is_recursive = false;
    }
    return (void *)m;
}

static void *recursive_mutex_create_wrapper(void)
{
    baremetal_mutex_t *m = (baremetal_mutex_t *)wifi_osi_malloc(sizeof(baremetal_mutex_t));
    if (m != NULL)
    {
        m->owner = 0U;
        m->count = 0U;
        m->is_recursive = true;
    }
    return (void *)m;
}

static void mutex_delete_wrapper(void *mutex)
{
    if (mutex != NULL)
    {
        wifi_osi_free(mutex);
    }
}

static int32_t mutex_lock_wrapper(void *mutex)
{
    if (mutex == NULL) return 0;
    baremetal_mutex_t *m = (baremetal_mutex_t *)mutex;
    uint32_t current_task = (uint32_t)(uintptr_t)task_get_current();
    if (current_task == 0U)
    {
        current_task = 1U;
    }

    while (1)
    {
        uint32_t prev = interrupt_global_save_and_disable();
        if (m->count == 0U)
        {
            m->owner = current_task;
            m->count = 1U;
            interrupt_global_restore(prev);
            return 1;
        }
        else if (m->is_recursive && m->owner == current_task)
        {
            m->count++;
            interrupt_global_restore(prev);
            return 1;
        }
        interrupt_global_restore(prev);

        if (task_get_count() > 1U)
        {
            task_yield();
        }
    }
}

static int32_t mutex_unlock_wrapper(void *mutex)
{
    if (mutex == NULL) return 0;
    baremetal_mutex_t *m = (baremetal_mutex_t *)mutex;
    uint32_t prev = interrupt_global_save_and_disable();
    if (m->count > 0U)
    {
        m->count--;
        if (m->count == 0U)
        {
            m->owner = 0U;
        }
    }
    interrupt_global_restore(prev);
    return 1;
}

/* ========================================================================= */
/* 4. Counting Semaphores with Cooperative Yield                             */
/* ========================================================================= */
typedef struct {
    uint32_t count;
    uint32_t max;
} baremetal_sem_t;

static void *semphr_create_wrapper(uint32_t max, uint32_t init)
{
    baremetal_sem_t *s = (baremetal_sem_t *)wifi_osi_malloc(sizeof(baremetal_sem_t));
    if (s != NULL)
    {
        s->count = init;
        s->max = max;
    }
    return (void *)s;
}

static void semphr_delete_wrapper(void *semphr)
{
    if (semphr != NULL)
    {
        wifi_osi_free(semphr);
    }
}

static int32_t semphr_take_wrapper(void *semphr, uint32_t block_time_tick)
{
    if (semphr == NULL) return 0;
    baremetal_sem_t *s = (baremetal_sem_t *)semphr;

    uint64_t start_ms = systimer_get_ms();
    while (1)
    {
        wifi_os_adapter_poll();
        wdt_feed();
        lp_wdt_feed();

        uint32_t prev = interrupt_global_save_and_disable();
        if (s->count > 0U)
        {
            s->count--;
            interrupt_global_restore(prev);
            return 1;
        }
        interrupt_global_restore(prev);

        if (block_time_tick == 0U)
        {
            return 0;
        }

        if (block_time_tick != OSI_FUNCS_TIME_BLOCKING)
        {
            if ((systimer_get_ms() - start_ms) >= (uint64_t)block_time_tick)
            {
                return 0;
            }
        }

        if (task_get_count() > 1U)
        {
            task_yield();
        }
    }
}

static int32_t semphr_give_wrapper(void *semphr)
{
    if (semphr == NULL) return 0;
    baremetal_sem_t *s = (baremetal_sem_t *)semphr;

    uint32_t prev = interrupt_global_save_and_disable();
    if (s->count < s->max)
    {
        s->count++;
    }
    interrupt_global_restore(prev);
    return 1;
}

static baremetal_sem_t s_thread_sem = { .count = 0U, .max = 1U };
static void *wifi_thread_semphr_get_wrapper(void)
{
    return (void *)&s_thread_sem;
}

/* ========================================================================= */
/* 5. Queues (Thread-Safe Single-Producer Single-Consumer Ring Buffers)       */
/* ========================================================================= */
#define BAREMETAL_QUEUE_MAGIC 0x51554555U /* "QUEU" */

typedef struct baremetal_queue {
    uint32_t magic;
    uint32_t len;
    uint32_t item_size;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t  *storage;
} baremetal_queue_t;

typedef struct wifi_queue_wrapper {
    void *handle;
} wifi_queue_wrapper_t;

static inline baremetal_queue_t *resolve_queue(void *queue)
{
    if (queue == NULL) return NULL;
    baremetal_queue_t *q = (baremetal_queue_t *)queue;
    if (q->magic == BAREMETAL_QUEUE_MAGIC)
    {
        return q;
    }
    baremetal_queue_t **wrapper = (baremetal_queue_t **)queue;
    if (wrapper[0] != NULL && wrapper[0]->magic == BAREMETAL_QUEUE_MAGIC)
    {
        return wrapper[0];
    }
    return NULL;
}

static void *queue_create_wrapper(uint32_t queue_len, uint32_t item_size)
{
    baremetal_queue_t *q = (baremetal_queue_t *)wifi_osi_malloc(sizeof(baremetal_queue_t));
    if (q == NULL) return NULL;

    q->storage = (uint8_t *)wifi_osi_malloc(queue_len * item_size);
    if (q->storage == NULL)
    {
        wifi_osi_free(q);
        return NULL;
    }

    q->magic = BAREMETAL_QUEUE_MAGIC;
    q->len = queue_len;
    q->item_size = item_size;
    q->head = 0U;
    q->tail = 0U;
    q->count = 0U;
    return (void *)q;
}

static void queue_delete_wrapper(void *queue)
{
    if (queue != NULL)
    {
        baremetal_queue_t *q = resolve_queue(queue);
        if (q != NULL)
        {
            if (q->storage != NULL)
            {
                wifi_osi_free(q->storage);
            }
            q->magic = 0U;
            wifi_osi_free(q);
        }
        if ((void *)q != queue)
        {
            wifi_osi_free(queue);
        }
    }
}

static int32_t queue_send_generic(void *queue, void *item, uint32_t block_time_tick, bool to_front)
{
    baremetal_queue_t *q = resolve_queue(queue);
    if (q == NULL || item == NULL) return 0;

    uint64_t start_ms = systimer_get_ms();
    while (1)
    {
        uint32_t prev = interrupt_global_save_and_disable();
        if (q->count < q->len)
        {
            if (to_front)
            {
                q->head = (q->head == 0U) ? (q->len - 1U) : (q->head - 1U);
                memcpy(q->storage + (q->head * q->item_size), item, q->item_size);
            }
            else
            {
                memcpy(q->storage + (q->tail * q->item_size), item, q->item_size);
                q->tail = (q->tail + 1U) % q->len;
            }
            q->count++;
            interrupt_global_restore(prev);
            return 1;
        }
        interrupt_global_restore(prev);

        if (block_time_tick == 0U)
        {
            return 0;
        }

        if (block_time_tick != OSI_FUNCS_TIME_BLOCKING)
        {
            if ((systimer_get_ms() - start_ms) >= (uint64_t)block_time_tick)
            {
                return 0;
            }
        }

        if (task_get_count() > 1U)
        {
            task_yield();
        }
    }
}

static int32_t queue_send_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
    return queue_send_generic(queue, item, block_time_tick, false);
}

static int32_t queue_send_from_isr_wrapper(void *queue, void *item, void *hptw)
{
    if (hptw != NULL) *(int32_t *)hptw = 0;
    return queue_send_generic(queue, item, 0U, false);
}

static int32_t queue_send_to_back_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
    return queue_send_generic(queue, item, block_time_tick, false);
}

static int32_t queue_send_to_front_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
    return queue_send_generic(queue, item, block_time_tick, true);
}

static int32_t queue_recv_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
    baremetal_queue_t *q = resolve_queue(queue);
    if (q == NULL || item == NULL) return 0;

    uint64_t start_ms = systimer_get_ms();
    while (1)
    {
        wifi_os_adapter_poll();

        uint32_t prev = interrupt_global_save_and_disable();
        if (q->count > 0U)
        {
            memcpy(item, q->storage + (q->head * q->item_size), q->item_size);
            q->head = (q->head + 1U) % q->len;
            q->count--;
            interrupt_global_restore(prev);
            return 1;
        }
        interrupt_global_restore(prev);

        if (block_time_tick == 0U)
        {
            return 0;
        }

        if (block_time_tick != OSI_FUNCS_TIME_BLOCKING)
        {
            if ((systimer_get_ms() - start_ms) >= (uint64_t)block_time_tick)
            {
                return 0;
            }
        }

        if (task_get_count() > 1U)
        {
            task_yield();
        }
    }
}

static uint32_t queue_msg_waiting_wrapper(void *queue)
{
    baremetal_queue_t *q = resolve_queue(queue);
    if (q == NULL) return 0U;
    return q->count;
}

static void *wifi_create_queue_typed(int queue_len, int item_size)
{
    void *q = queue_create_wrapper((uint32_t)queue_len, (uint32_t)item_size);
    if (q == NULL) return NULL;

    wifi_queue_wrapper_t *w = (wifi_queue_wrapper_t *)wifi_osi_malloc(sizeof(wifi_queue_wrapper_t));
    if (w == NULL)
    {
        queue_delete_wrapper(q);
        return NULL;
    }
    w->handle = q;
    return (void *)w;
}

static void wifi_delete_queue_typed(void *queue)
{
    queue_delete_wrapper(queue);
}

/* ========================================================================= */
/* 6. Event Groups                                                           */
/* ========================================================================= */
typedef struct {
    uint32_t bits;
} baremetal_event_group_t;

static void *event_group_create_wrapper(void)
{
    baremetal_event_group_t *eg = (baremetal_event_group_t *)wifi_osi_zalloc(sizeof(baremetal_event_group_t));
    return (void *)eg;
}

static void event_group_delete_wrapper(void *event)
{
    if (event != NULL)
    {
        wifi_osi_free(event);
    }
}

static uint32_t event_group_set_bits_wrapper(void *event, uint32_t bits)
{
    if (event == NULL) return 0U;
    baremetal_event_group_t *eg = (baremetal_event_group_t *)event;
    uint32_t prev = interrupt_global_save_and_disable();
    eg->bits |= bits;
    uint32_t res = eg->bits;
    interrupt_global_restore(prev);
    return res;
}

static uint32_t event_group_clear_bits_wrapper(void *event, uint32_t bits)
{
    if (event == NULL) return 0U;
    baremetal_event_group_t *eg = (baremetal_event_group_t *)event;
    uint32_t prev = interrupt_global_save_and_disable();
    uint32_t orig = eg->bits;
    eg->bits &= ~bits;
    interrupt_global_restore(prev);
    return orig;
}

static uint32_t event_group_wait_bits_wrapper(void *event, uint32_t bits_to_wait_for, int clear_on_exit, int wait_for_all_bits, uint32_t block_time_tick)
{
    if (event == NULL) return 0U;
    baremetal_event_group_t *eg = (baremetal_event_group_t *)event;

    uint64_t start_ms = systimer_get_ms();
    while (1)
    {
        wifi_os_adapter_poll();

        uint32_t prev = interrupt_global_save_and_disable();
        bool match = wait_for_all_bits ?
            ((eg->bits & bits_to_wait_for) == bits_to_wait_for) :
            ((eg->bits & bits_to_wait_for) != 0U);

        if (match)
        {
            uint32_t res = eg->bits;
            if (clear_on_exit)
            {
                eg->bits &= ~bits_to_wait_for;
            }
            interrupt_global_restore(prev);
            return res;
        }
        interrupt_global_restore(prev);

        if (block_time_tick == 0U)
        {
            return 0U;
        }

        if (block_time_tick != OSI_FUNCS_TIME_BLOCKING)
        {
            if ((systimer_get_ms() - start_ms) >= (uint64_t)block_time_tick)
            {
                return 0U;
            }
        }

        if (task_get_count() > 1U)
        {
            task_yield();
        }
    }
}

/* ========================================================================= */
/* 7. High-Resolution Timers (ets_timer_t Engine)                            */
/* ========================================================================= */
typedef void (*ETSTimerFunc)(void *timer_arg);

typedef struct _ETSTIMER_ {
    struct _ETSTIMER_    *timer_next;   /**< timer linker (4 bytes) */
    uint32_t              timer_expire; /**< abstract expiration (4 bytes) */
    uint32_t              timer_period; /**< period (4 bytes) */
    ETSTimerFunc          timer_func;   /**< timer callback (4 bytes) */
    void                 *timer_arg;    /**< timer callback argument (4 bytes) */
} ETSTimer;

/* Memory validator to guarantee function pointers are in executable memory */
static inline bool is_valid_instruction_address(uintptr_t addr)
{
    if ((addr & 1U) != 0U) return false;
    if (addr >= 0x40000000U && addr < 0x40060000U) return true; /* ROM */
    if (addr >= 0x40800000U && addr < 0x40829000U) return true; /* IRAM */
    if (addr >= 0x42000000U && addr < 0x42800000U) return true; /* Flash XIP text */
    return false;
}

typedef struct {
    void         *timer_handle;  /* Opaque handle passed by caller */
    ETSTimerFunc  fn;            /* Validated callback function pointer */
    void         *arg;           /* Callback argument */
    uint64_t      expire_us;     /* Expiration timestamp in microseconds */
    uint32_t      period_us;     /* Period in microseconds */
    bool          repeat;        /* Periodic timer */
    bool          active;        /* Arm status */
} wifi_timer_tracker_t;

static wifi_timer_tracker_t s_timer_trackers[WIFI_MAX_ACTIVE_TIMERS];

static void timer_setfn_wrapper(void *ptimer, void *pfunction, void *parg)
{
    if (ptimer == NULL) return;

    /* Maintain ETSTimer internal struct for compatibility */
    ETSTimer *t = (ETSTimer *)ptimer;
    t->timer_func = (ETSTimerFunc)pfunction;
    t->timer_arg = parg;
    t->timer_next = NULL;
    t->timer_expire = 0U;
    t->timer_period = 0U;

    uint32_t prev = interrupt_global_save_and_disable();
    wifi_timer_tracker_t *slot = NULL;

    for (uint32_t i = 0U; i < WIFI_MAX_ACTIVE_TIMERS; i++)
    {
        if (s_timer_trackers[i].timer_handle == ptimer)
        {
            slot = &s_timer_trackers[i];
            break;
        }
    }

    if (slot == NULL)
    {
        for (uint32_t i = 0U; i < WIFI_MAX_ACTIVE_TIMERS; i++)
        {
            if (s_timer_trackers[i].timer_handle == NULL)
            {
                slot = &s_timer_trackers[i];
                break;
            }
        }
    }

    if (slot != NULL)
    {
        slot->timer_handle = ptimer;
        slot->fn = (ETSTimerFunc)pfunction;
        slot->arg = parg;
        slot->active = false;
        slot->repeat = false;
        slot->period_us = 0U;
        slot->expire_us = 0ULL;
    }
    interrupt_global_restore(prev);
}

static void timer_arm_us_wrapper(void *ptimer, uint32_t us, bool repeat)
{
    if (ptimer == NULL) return;

    uint32_t prev = interrupt_global_save_and_disable();
    wifi_timer_tracker_t *slot = NULL;

    for (uint32_t i = 0U; i < WIFI_MAX_ACTIVE_TIMERS; i++)
    {
        if (s_timer_trackers[i].timer_handle == ptimer)
        {
            slot = &s_timer_trackers[i];
            break;
        }
    }

    if (slot == NULL)
    {
        ETSTimer *t = (ETSTimer *)ptimer;
        if (t != NULL && is_valid_instruction_address((uintptr_t)t->timer_func))
        {
            for (uint32_t i = 0U; i < WIFI_MAX_ACTIVE_TIMERS; i++)
            {
                if (s_timer_trackers[i].timer_handle == NULL)
                {
                    slot = &s_timer_trackers[i];
                    slot->timer_handle = ptimer;
                    slot->fn = t->timer_func;
                    slot->arg = t->timer_arg;
                    break;
                }
            }
        }
    }

    if (slot != NULL)
    {
        if (slot->fn == NULL || !is_valid_instruction_address((uintptr_t)slot->fn))
        {
            ETSTimer *t = (ETSTimer *)ptimer;
            if (t != NULL && is_valid_instruction_address((uintptr_t)t->timer_func))
            {
                slot->fn = t->timer_func;
                slot->arg = t->timer_arg;
            }
        }

        if (slot->fn != NULL && is_valid_instruction_address((uintptr_t)slot->fn))
        {
            slot->expire_us = systimer_get_us() + (uint64_t)us;
            slot->period_us = us;
            slot->repeat = repeat;
            slot->active = true;
        }
    }
    interrupt_global_restore(prev);
}

static void timer_arm_wrapper(void *timer, uint32_t tmout_ms, bool repeat)
{
    timer_arm_us_wrapper(timer, tmout_ms * 1000U, repeat);
}

static void timer_disarm_wrapper(void *timer)
{
    if (timer == NULL) return;

    uint32_t prev = interrupt_global_save_and_disable();
    for (uint32_t i = 0U; i < WIFI_MAX_ACTIVE_TIMERS; i++)
    {
        if (s_timer_trackers[i].timer_handle == timer)
        {
            s_timer_trackers[i].active = false;
        }
    }
    interrupt_global_restore(prev);
}

static void timer_done_wrapper(void *ptimer)
{
    if (ptimer == NULL) return;

    uint32_t prev = interrupt_global_save_and_disable();
    for (uint32_t i = 0U; i < WIFI_MAX_ACTIVE_TIMERS; i++)
    {
        if (s_timer_trackers[i].timer_handle == ptimer)
        {
            s_timer_trackers[i].active = false;
            s_timer_trackers[i].timer_handle = NULL;
            s_timer_trackers[i].fn = NULL;
            s_timer_trackers[i].arg = NULL;
            s_timer_trackers[i].expire_us = 0ULL;
            s_timer_trackers[i].period_us = 0U;
            s_timer_trackers[i].repeat = false;
        }
    }
    interrupt_global_restore(prev);
}

static void wifi_timer_invalidate_handle(void *ptr)
{
    if (ptr == NULL) return;

    for (uint32_t i = 0U; i < WIFI_MAX_ACTIVE_TIMERS; i++)
    {
        if (s_timer_trackers[i].timer_handle == ptr)
        {
            s_timer_trackers[i].active = false;
            s_timer_trackers[i].timer_handle = NULL;
            s_timer_trackers[i].fn = NULL;
            s_timer_trackers[i].arg = NULL;
            s_timer_trackers[i].expire_us = 0ULL;
            s_timer_trackers[i].period_us = 0U;
            s_timer_trackers[i].repeat = false;
        }
    }
}

static bool s_in_poll = false;
void wifi_os_adapter_poll(void)
{
    if (s_in_poll) return;
    s_in_poll = true;

    wdt_feed();
    lp_wdt_feed();
    wifi_poll_rx_traffic();

    uint64_t now_us = systimer_get_us();

    for (uint32_t i = 0U; i < WIFI_MAX_ACTIVE_TIMERS; i++)
    {
        uint32_t prev = interrupt_global_save_and_disable();
        if (s_timer_trackers[i].active &&
            s_timer_trackers[i].fn != NULL &&
            now_us >= s_timer_trackers[i].expire_us)
        {
            ETSTimerFunc fn = s_timer_trackers[i].fn;
            void *arg = s_timer_trackers[i].arg;

            if (s_timer_trackers[i].repeat && s_timer_trackers[i].period_us > 0U)
            {
                s_timer_trackers[i].expire_us += (uint64_t)s_timer_trackers[i].period_us;
            }
            else
            {
                s_timer_trackers[i].active = false;
            }

            interrupt_global_restore(prev);

            if (is_valid_instruction_address((uintptr_t)fn))
            {
                fn(arg);
            }
        }
        else
        {
            interrupt_global_restore(prev);
        }
    }
    s_in_poll = false;
}

/* ========================================================================= */
/* 8. Tasks & Scheduling                                                     */
/* ========================================================================= */
static uint8_t s_wifi_task_stack[8192] __attribute__((aligned(16)));
static bool s_wifi_task_stack_used = false;

static int32_t task_create_pinned_to_core_wrapper(void *task_func, const char *name, uint32_t stack_depth, void *param, uint32_t prio, void *task_handle, uint32_t core_id)
{
    (void)core_id;
    uint8_t *stack = NULL;
    uint32_t stack_sz = stack_depth;

    if (!s_wifi_task_stack_used && stack_sz <= sizeof(s_wifi_task_stack))
    {
        stack = s_wifi_task_stack;
        s_wifi_task_stack_used = true;
    }
    else
    {
        stack = (uint8_t *)wifi_osi_malloc(stack_sz);
    }

    if (stack == NULL)
    {
        return 0;
    }

    int rc = task_create(name, (task_entry_t)task_func, param, prio, stack, stack_sz);
    if (rc >= 0)
    {
        if (task_handle != NULL)
        {
            *(void **)task_handle = (void *)task_get_by_id((uint32_t)rc);
        }
        return 1;
    }
    return 0;
}

static int32_t task_create_wrapper(void *task_func, const char *name, uint32_t stack_depth, void *param, uint32_t prio, void *task_handle)
{
    return task_create_pinned_to_core_wrapper(task_func, name, stack_depth, param, prio, task_handle, 0U);
}

static void task_delete_wrapper(void *task_handle)
{
    (void)task_handle;
}

static void task_delay_wrapper(uint32_t tick)
{
    uint64_t start_ms = systimer_get_ms();
    while ((systimer_get_ms() - start_ms) < (uint64_t)tick)
    {
        wifi_os_adapter_poll();
        if (task_get_count() > 1U)
        {
            task_yield();
        }
    }
}

static int32_t task_ms_to_tick_wrapper(uint32_t ms)
{
    return (int32_t)ms;
}

static void *task_get_current_task_wrapper(void)
{
    return (void *)task_get_current();
}

static int32_t task_get_max_priority_wrapper(void)
{
    return 15;
}

static void task_yield_from_isr_wrapper(void)
{
}

/* ========================================================================= */
/* 9. Hardware Clocks, PHY & RF Frontend                                     */
/* ========================================================================= */
extern int register_chipv7_phy(const void *init_data, void *cal_data, int cal_mode);
extern void phy_wakeup_init(void);

static bool s_phy_registered = false;

static void wifi_clock_enable_wrapper(void)
{
    modem_enable_wifi_clocks();
}

static void wifi_clock_disable_wrapper(void)
{
    /* Keep Wi-Fi RF Front-End and Baseband clocks active in baremetal */
}

static void wifi_reset_mac_wrapper(void)
{
    *MODEM_SYSCON_MODEM_RST_CONF_REG |= MODEM_RST_WIFIMAC_BIT;
    for (volatile int i = 0; i < 10; i++) asm volatile("nop");
    *MODEM_SYSCON_MODEM_RST_CONF_REG &= ~MODEM_RST_WIFIMAC_BIT;
}

static esp_phy_calibration_data_t s_phy_cal_data;

extern void phy_init_param_set(uint8_t val);
extern void phy_wifi_enable_set(uint8_t en);
extern const esp_phy_init_data_t phy_init_data;

static void phy_enable_wrapper(void)
{
    console_puts("[wifi_os] phy_enable_wrapper entered\r\n");
    modem_enable_wifi_clocks();

    phy_init_param_set(1U);

    if (!s_phy_registered)
    {
        wifi_get_mac_addr(s_phy_cal_data.mac);
        lp_wdt_feed();
        lp_wdt_disable();
        wdt_disable();
        console_puts("[wifi_os] calling register_chipv7_phy(FULL)...\r\n");
        int ret = register_chipv7_phy(&phy_init_data, &s_phy_cal_data, PHY_RF_CAL_FULL);
        wdt_enable();
        lp_wdt_enable();
        lp_wdt_feed();
        console_puts("[wifi_os] register_chipv7_phy returned ");
        put_dec((uint32_t)ret);
        console_puts("\r\n");
        s_phy_registered = true;
    }
    else
    {
        phy_wakeup_init();
    }

    phy_wifi_enable_set(1U);
#if defined(__riscv)
    *MODEM_RF_ENABLE_REG |= MODEM_RF_ENABLE_MASTER_BIT;
    asm volatile("fence" ::: "memory");
#endif
}

static void phy_disable_wrapper(void)
{
    phy_wifi_enable_set(0U);
}

static int phy_update_country_info_wrapper(const char *country)
{
    (void)country;
    return 0;
}

static int read_mac_wrapper(uint8_t *mac, unsigned int type)
{
    uint32_t mac0 = *EFUSE_MAC_SYS_0_REG;
    uint32_t mac1 = *EFUSE_MAC_SYS_1_REG;

    mac[0] = (uint8_t)((mac1 >> 8U) & 0xFFU);
    mac[1] = (uint8_t)(mac1 & 0xFFU);
    mac[2] = (uint8_t)((mac0 >> 24U) & 0xFFU);
    mac[3] = (uint8_t)((mac0 >> 16U) & 0xFFU);
    mac[4] = (uint8_t)((mac0 >> 8U) & 0xFFU);
    mac[5] = (uint8_t)(mac0 & 0xFFU);

    if (type == 1U)
    {
        /* Derive distinct SoftAP MAC address (base MAC + 1 with carry propagation) */
        uint32_t carry = 1U;
        for (int i = (int)WIFI_MAC_ADDR_LEN - 1; i >= 0 && carry != 0U; i--)
        {
            uint32_t sum = (uint32_t)mac[i] + carry;
            mac[i] = (uint8_t)(sum & 0xFFU);
            carry = sum >> 8U;
        }
    }
    return 0;
}

/* ========================================================================= */
/* 10. Hardware TRNG & Miscellaneous Wrappers                                */
/* ========================================================================= */
static bool env_is_chip_wrapper(void) { return true; }
static int64_t esp_timer_get_time_wrapper(void) { return (int64_t)systimer_get_us(); }
static uint32_t log_timestamp_wrapper(void) { return (uint32_t)systimer_get_ms(); }
static uint32_t slowclk_cal_get_wrapper(void) { return 0U; }

static uint32_t rand_wrapper(void)
{
    /* Enable LP_PERI RNG clock and read true physical random number */
    *LP_PERI_CLK_EN_REG |= LP_PERI_CLK_EN_RNG_CK_EN_M;
    return *LP_PERI_RNG_DATA_REG;
}

static unsigned long random_wrapper(void)
{
    return (unsigned long)rand_wrapper();
}

static int get_random_wrapper(uint8_t *buf, size_t len)
{
    if (buf == NULL) return -1;
    *LP_PERI_CLK_EN_REG |= LP_PERI_CLK_EN_RNG_CK_EN_M;
    for (size_t i = 0U; i < len; i += 4U)
    {
        uint32_t r = *LP_PERI_RNG_DATA_REG;
        for (size_t j = 0U; j < 4U && (i + j) < len; j++)
        {
            buf[i + j] = (uint8_t)(r >> (j * 8U));
        }
    }
    return 0;
}

/* os_get_time(): monotonic time since boot (ESP-IDF uses gettimeofday). The blob
 * uses it for station ageing; leaving it unwritten handed the blob stack garbage. */
static int get_time_wrapper(void *t)
{
    if (t == NULL)
    {
        return -1;
    }
    uint64_t now_us = systimer_get_us();
    wifi_os_time_t *ot = (wifi_os_time_t *)t;
    ot->sec = now_us / US_PER_SECOND;
    ot->usec = (long)(now_us % US_PER_SECOND);
    return 0;
}

extern esp_err_t esp_event_send_internal(esp_event_base_t event_base,
                                         int32_t event_id,
                                         void* event_data,
                                         size_t event_data_size,
                                         TickType_t ticks_to_wait);

static int32_t event_post_wrapper(const char *event_base, int32_t event_id, void *event_data, size_t event_data_size, uint32_t ticks_to_wait)
{
    return esp_event_send_internal(event_base, event_id, event_data, event_data_size, ticks_to_wait);
}

static void log_writev_wrapper(unsigned int level, const char *tag, const char *format, va_list args)
{
    (void)level;
    if (tag != NULL)
    {
        console_puts("[");
        console_puts(tag);
        console_puts("] ");
    }
    char log_buf[256];
    mini_vsnprintf(log_buf, sizeof(log_buf), format, args);
    console_puts(log_buf);
}

static void log_write_wrapper(unsigned int level, const char *tag, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    log_writev_wrapper(level, tag, format, ap);
    va_end(ap);
}

#define ESP_ERR_NVS_NOT_FOUND 0x1102

/* NVS stubs (WIFI_NVS_ENABLED = 1 via memory defaults) */
static int nvs_stub_set_i8(uint32_t handle, const char *key, int8_t value) { (void)handle; (void)key; (void)value; return 0; }
static int nvs_stub_get_i8(uint32_t handle, const char *key, int8_t *out_val) { (void)handle; (void)key; (void)out_val; return ESP_ERR_NVS_NOT_FOUND; }
static int nvs_stub_set_u8(uint32_t handle, const char *key, uint8_t value) { (void)handle; (void)key; (void)value; return 0; }
static int nvs_stub_get_u8(uint32_t handle, const char *key, uint8_t *out_val)
{
    (void)handle;
    if (key != NULL && out_val != NULL)
    {
        if (strcmp(key, "ap.lowrate") == 0 ||
            strcmp(key, "sta.lowrate") == 0 ||
            strcmp(key, "lorate") == 0)
        {
            *out_val = 0U;
            return 0;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
static int nvs_stub_set_u16(uint32_t handle, const char *key, uint16_t value) { (void)handle; (void)key; (void)value; return 0; }
static int nvs_stub_get_u16(uint32_t handle, const char *key, uint16_t *out_val) { (void)handle; (void)key; (void)out_val; return ESP_ERR_NVS_NOT_FOUND; }
static int nvs_open_stub(const char *name, unsigned int open_mode, uint32_t *out_handle)
{
    (void)name;
    (void)open_mode;
    if (out_handle != NULL)
    {
        *out_handle = (uint32_t)WIFI_NVS_STUB_DEFAULT_HANDLE;
    }
    return 0;
}
static void nvs_stub_void(uint32_t handle) { (void)handle; }
static int nvs_stub_commit(uint32_t handle) { (void)handle; return 0; }
static int nvs_stub_set_blob(uint32_t handle, const char *key, const void *val, size_t len) { (void)handle; (void)key; (void)val; (void)len; return 0; }
static int nvs_stub_get_blob(uint32_t handle, const char *key, void *val, size_t *len) { (void)handle; (void)key; (void)val; (void)len; return ESP_ERR_NVS_NOT_FOUND; }
static int nvs_stub_erase_key(uint32_t handle, const char *key) { (void)handle; (void)key; return 0; }

/*
 * Coexistence stubs. This build has no software coexistence (no BLE/802.15.4
 * controller), so these match ESP-IDF's esp_adapter.c with
 * CONFIG_ESP_COEX_SW_COEXIST_ENABLE off: status 0, status-bit updates ignored,
 * schedule interval 0 and no phase.
 * Measured with the unmodified radio libraries (libs/esp32c6/VERSION): AP
 * beacons, station scans receive real APs, no pm_coex assert. A nonzero status
 * makes the blob enter its BT-coex scheduler, which asserts in pm_coex.c on scan
 * without a real coex phase; 316be2c's fake phase, 1000 ms interval and zeroed
 * PTI/duration outputs starved Wi-Fi.
 */
static int coex_init_wrapper(void) { return 0; }
static void coex_deinit_wrapper(void) {}
static int coex_enable_wrapper(void) { return 0; }
static void coex_disable_wrapper(void) {}
static uint32_t coex_status_get_wrapper(void) { return 0U; }
static void coex_condition_set_wrapper(uint32_t type, bool dissatisfy) { (void)type; (void)dissatisfy; }
static int coex_wifi_request_wrapper(uint32_t event, uint32_t latency, uint32_t duration) { (void)event; (void)latency; (void)duration; return 0; }
static int coex_wifi_release_wrapper(uint32_t event) { (void)event; return 0; }
static int coex_wifi_channel_set_wrapper(uint8_t primary, uint8_t secondary) { (void)primary; (void)secondary; return 0; }
/* Leave *duration and *pti untouched: the blob keeps its own defaults. Writing 0
 * sets every Wi-Fi frame's PTA priority to 0 (boot log "11ax coex: WDEVAX_PTI0(0x00000000)")
 * and the MAC stops transmitting. */
static int coex_event_duration_get_wrapper(uint32_t event, uint32_t *duration) { (void)event; (void)duration; return 0; }
static int coex_pti_get_wrapper(uint32_t event, uint8_t *pti) { (void)event; (void)pti; return 0; }
static void coex_schm_status_bit_clear_wrapper(uint32_t type, uint32_t status) { (void)type; (void)status; }
static void coex_schm_status_bit_set_wrapper(uint32_t type, uint32_t status) { (void)type; (void)status; }
static int coex_schm_interval_set_wrapper(uint32_t interval) { (void)interval; return 0; }
static uint32_t coex_schm_interval_get_wrapper(void) { return 0U; }
static uint8_t coex_schm_curr_period_get_wrapper(void) { return 0U; }
static void *coex_schm_curr_phase_get_wrapper(void) { return NULL; }
static int coex_schm_process_restart_wrapper(void) { return 0; }
static int coex_schm_register_cb_wrapper(int a, int (*cb)(int)) { (void)a; (void)cb; return 0; }
static int coex_register_start_cb_wrapper(int (*cb)(void)) { (void)cb; return 0; }
static void regdma_link_set_write_wait_content_wrapper(void *a, uint32_t b, uint32_t c) { (void)a; (void)b; (void)c; }
static void *sleep_retention_find_link_by_id_wrapper(int id) { (void)id; return NULL; }
static int coex_schm_flexible_period_set_wrapper(uint8_t a) { (void)a; return 0; }
static uint8_t coex_schm_flexible_period_get_wrapper(void) { return 0U; }
static void *coex_schm_get_phase_by_idx_wrapper(int a) { (void)a; return NULL; }
static bool wifi_disable_ac_ax_wrapper(void) { return false; }

static void empty_wrapper(void) {}
static int32_t dummy_success(void) { return 1; }

/* ========================================================================= */
/* 11. Global OS Adapter Dispatch Table                                      */
/* ========================================================================= */
wifi_osi_funcs_t g_wifi_osi_funcs = {
    ._version = ESP_WIFI_OS_ADAPTER_VERSION,
    ._env_is_chip = env_is_chip_wrapper,
    ._set_intr = set_intr_wrapper,
    ._clear_intr = clear_intr_wrapper,
    ._set_isr = set_isr_wrapper,
    ._ints_on = enable_intr_wrapper,
    ._ints_off = disable_intr_wrapper,
    ._is_from_isr = is_from_isr_wrapper,
    ._spin_lock_create = spin_lock_create_wrapper,
    ._spin_lock_delete = spin_lock_delete_wrapper,
    ._wifi_int_disable = wifi_int_disable_wrapper,
    ._wifi_int_restore = wifi_int_restore_wrapper,
    ._task_yield_from_isr = task_yield_from_isr_wrapper,
    ._semphr_create = semphr_create_wrapper,
    ._semphr_delete = semphr_delete_wrapper,
    ._semphr_take = semphr_take_wrapper,
    ._semphr_give = semphr_give_wrapper,
    ._wifi_thread_semphr_get = wifi_thread_semphr_get_wrapper,
    ._mutex_create = mutex_create_wrapper,
    ._recursive_mutex_create = recursive_mutex_create_wrapper,
    ._mutex_delete = mutex_delete_wrapper,
    ._mutex_lock = mutex_lock_wrapper,
    ._mutex_unlock = mutex_unlock_wrapper,
    ._queue_create = queue_create_wrapper,
    ._queue_delete = queue_delete_wrapper,
    ._queue_send = queue_send_wrapper,
    ._queue_send_from_isr = queue_send_from_isr_wrapper,
    ._queue_send_to_back = queue_send_to_back_wrapper,
    ._queue_send_to_front = queue_send_to_front_wrapper,
    ._queue_recv = queue_recv_wrapper,
    ._queue_msg_waiting = queue_msg_waiting_wrapper,
    ._event_group_create = event_group_create_wrapper,
    ._event_group_delete = event_group_delete_wrapper,
    ._event_group_set_bits = event_group_set_bits_wrapper,
    ._event_group_clear_bits = event_group_clear_bits_wrapper,
    ._event_group_wait_bits = event_group_wait_bits_wrapper,
    ._task_create_pinned_to_core = task_create_pinned_to_core_wrapper,
    ._task_create = task_create_wrapper,
    ._task_delete = task_delete_wrapper,
    ._task_delay = task_delay_wrapper,
    ._task_ms_to_tick = task_ms_to_tick_wrapper,
    ._task_get_current_task = task_get_current_task_wrapper,
    ._task_get_max_priority = task_get_max_priority_wrapper,
    ._malloc = wifi_osi_malloc,
    ._free = wifi_osi_free,
    ._event_post = event_post_wrapper,
    ._get_free_heap_size = get_free_heap_size_wrapper,
    ._rand = rand_wrapper,
    ._dport_access_stall_other_cpu_start_wrap = empty_wrapper,
    ._dport_access_stall_other_cpu_end_wrap = empty_wrapper,
    ._wifi_pm_sleep_lock_acquire = empty_wrapper,
    ._wifi_pm_sleep_lock_release = empty_wrapper,
    ._phy_disable = phy_disable_wrapper,
    ._phy_enable = phy_enable_wrapper,
    ._phy_update_country_info = phy_update_country_info_wrapper,
    ._read_mac = read_mac_wrapper,
    ._timer_arm = timer_arm_wrapper,
    ._timer_disarm = timer_disarm_wrapper,
    ._timer_done = timer_done_wrapper,
    ._timer_setfn = timer_setfn_wrapper,
    ._timer_arm_us = timer_arm_us_wrapper,
    ._wifi_reset_mac = wifi_reset_mac_wrapper,
    ._wifi_clock_enable = wifi_clock_enable_wrapper,
    ._wifi_clock_disable = wifi_clock_disable_wrapper,
    ._wifi_rtc_enable_iso = empty_wrapper,
    ._wifi_rtc_disable_iso = empty_wrapper,
    ._esp_timer_get_time = esp_timer_get_time_wrapper,
    ._nvs_set_i8 = nvs_stub_set_i8,
    ._nvs_get_i8 = nvs_stub_get_i8,
    ._nvs_set_u8 = nvs_stub_set_u8,
    ._nvs_get_u8 = nvs_stub_get_u8,
    ._nvs_set_u16 = nvs_stub_set_u16,
    ._nvs_get_u16 = nvs_stub_get_u16,
    ._nvs_open = nvs_open_stub,
    ._nvs_close = nvs_stub_void,
    ._nvs_commit = nvs_stub_commit,
    ._nvs_set_blob = nvs_stub_set_blob,
    ._nvs_get_blob = nvs_stub_get_blob,
    ._nvs_erase_key = nvs_stub_erase_key,
    ._get_random = get_random_wrapper,
    ._get_time = get_time_wrapper,
    ._random = random_wrapper,
    ._slowclk_cal_get = slowclk_cal_get_wrapper,
    ._log_write = log_write_wrapper,
    ._log_writev = log_writev_wrapper,
    ._log_timestamp = log_timestamp_wrapper,
    ._malloc_internal = wifi_osi_malloc,
    ._realloc_internal = wifi_osi_realloc,
    ._calloc_internal = wifi_osi_calloc,
    ._zalloc_internal = wifi_osi_zalloc,
    ._wifi_malloc = wifi_osi_malloc,
    ._wifi_realloc = wifi_osi_realloc,
    ._wifi_calloc = wifi_osi_calloc,
    ._wifi_zalloc = wifi_osi_zalloc,
    ._wifi_create_queue = wifi_create_queue_typed,
    ._wifi_delete_queue = wifi_delete_queue_typed,
    ._coex_init = coex_init_wrapper,
    ._coex_deinit = coex_deinit_wrapper,
    ._coex_enable = coex_enable_wrapper,
    ._coex_disable = coex_disable_wrapper,
    ._coex_status_get = coex_status_get_wrapper,
    ._coex_condition_set = coex_condition_set_wrapper,
    ._coex_wifi_request = coex_wifi_request_wrapper,
    ._coex_wifi_release = coex_wifi_release_wrapper,
    ._coex_wifi_channel_set = coex_wifi_channel_set_wrapper,
    ._coex_event_duration_get = coex_event_duration_get_wrapper,
    ._coex_pti_get = coex_pti_get_wrapper,
    ._coex_schm_status_bit_clear = coex_schm_status_bit_clear_wrapper,
    ._coex_schm_status_bit_set = coex_schm_status_bit_set_wrapper,
    ._coex_schm_interval_set = coex_schm_interval_set_wrapper,
    ._coex_schm_interval_get = coex_schm_interval_get_wrapper,
    ._coex_schm_curr_period_get = coex_schm_curr_period_get_wrapper,
    ._coex_schm_curr_phase_get = coex_schm_curr_phase_get_wrapper,
    ._coex_schm_process_restart = coex_schm_process_restart_wrapper,
    ._coex_schm_register_cb = coex_schm_register_cb_wrapper,
    ._coex_register_start_cb = coex_register_start_cb_wrapper,
    ._regdma_link_set_write_wait_content = regdma_link_set_write_wait_content_wrapper,
    ._sleep_retention_find_link_by_id = sleep_retention_find_link_by_id_wrapper,
    ._coex_schm_flexible_period_set = coex_schm_flexible_period_set_wrapper,
    ._coex_schm_flexible_period_get = coex_schm_flexible_period_get_wrapper,
    ._coex_schm_get_phase_by_idx = coex_schm_get_phase_by_idx_wrapper,
    ._wifi_disable_ac_ax = wifi_disable_ac_ax_wrapper,
    ._wifi_bb_sleep_retention_attach = dummy_success,
    ._wifi_bb_sleep_retention_detach = dummy_success,
    ._wifi_mac_sleep_retention_attach = dummy_success,
    ._wifi_mac_sleep_retention_detach = dummy_success,
    ._magic = ESP_WIFI_OS_ADAPTER_MAGIC,
};

const wpa_crypto_funcs_t g_wifi_default_wpa_crypto_funcs = {
    .size = sizeof(wpa_crypto_funcs_t),
    .version = ESP_WIFI_CRYPTO_VERSION
};

double floor(double x)
{
    long long i = (long long)x;
    if (x < 0.0 && x != (double)i) return (double)(i - 1);
    return (double)i;
}

int net80211_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    console_puts(buf);
    va_end(ap);
    return 0;
}

int phy_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    console_puts(buf);
    va_end(ap);
    return 0;
}

int pp_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[256];
    mini_vsnprintf(buf, sizeof(buf), fmt, ap);
    console_puts(buf);
    va_end(ap);
    return 0;
}

void free(void *ptr)
{
    wifi_osi_free(ptr);
}

const char *WIFI_EVENT = "WIFI_EVENT";
uint32_t rtc_clk_xtal_freq_get(void) { return 40U; }
void rfpll_cap_track(void) {}

/* Mesh & FTM Stubs */
uint32_t g_espnow_user_oui = 0;
uint32_t g_mt = 0;
void hexstr2bin(void) {}
void ieee80211_init_mesh_assoc_ie(void) {}
void ieee80211_vnd_mesh_quick_get(void) {}
void ieee80211_vnd_mesh_quick_set(void) {}
void ieee80211_vnd_mesh_roots_get(void) {}
void ieee80211_vnd_mesh_roots_set(void) {}
void mesh_clear_parent_candidate(void) {}
void mesh_get_parent_candidate(void) {}
void mesh_get_parent_monitor_config(void) {}
void mesh_get_rssi_threshold(void) {}
void mesh_set_ie_crypto_config(void) {}
void mesh_set_parent_candidate(void) {}
void mesh_set_parent_monitor_config(void) {}
void mesh_set_rssi_threshold(void) {}
void mesh_sta_auth_expire_time(void) {}
void mt_get_peer_info(void) {}

/* Legacy event sender */
esp_err_t esp_event_send_internal(esp_event_base_t event_base,
                                  int32_t event_id,
                                  void* event_data,
                                  size_t event_data_size,
                                  TickType_t ticks_to_wait)
{
    (void)event_data_size;
    (void)ticks_to_wait;
    if (event_base == WIFI_EVENT || (event_base != NULL && strcmp(event_base, "WIFI_EVENT") == 0))
    {
        wifi_handle_vendor_event(event_id, event_data);
    }
    return 0;
}

extern esp_err_t esp_wifi_connect_internal(void);
esp_err_t esp_wifi_connect(void)
{
    return esp_wifi_connect_internal();
}

extern esp_err_t esp_wifi_disconnect_internal(void);
esp_err_t esp_wifi_disconnect(void)
{
    return esp_wifi_disconnect_internal();
}

/* ========================================================================= */
/* 12. Synthesizer Channel Frequency Lock Guard Shim                         */
/* ========================================================================= */
extern void freq_chan_en_sw(uint32_t chan);
extern void ets_delay_us(uint32_t us);

void __wrap_ram_set_chan_freq_sw_start(uint32_t chan)
{
    freq_chan_en_sw(chan);
    ets_delay_us(10U);

    /* Bounded hardware frequency synthesizer lock loop */
    bool locked = false;
    for (uint32_t i = 0U; i < MODEM_FE_FREQ_LOCK_TIMEOUT_US; i++)
    {
        if ((*MODEM_FE_FREQ_STATUS_REG & MODEM_FE_FREQ_LOCK_BIT) != 0U)
        {
            locked = true;
            break;
        }
        ets_delay_us(1U);
    }
    if (!locked)
    {
        console_puts("[wifi_os] WARN: synth lock timeout, stat=0x");
        put_hex(*MODEM_FE_FREQ_STATUS_REG);
        console_puts("\r\n");
    }
}

/* ========================================================================= */
/* 13. Bare-Metal WPA Supplicant Callbacks Registration                      */
/* Task 5.7.1 provides safe baseline callbacks; Task 5.7.2 implements WPA2   */
/* ========================================================================= */
typedef struct {
    int proto;
    int pairwise_cipher;
    int group_cipher;
    int key_mgmt;
    int capabilities;
    size_t num_pmkid;
    const uint8_t *pmkid;
    int mgmt_group_cipher;
    uint8_t rsnxe_capa;
} wifi_wpa_ie_t;

typedef struct {
    void **sm;
    uint8_t *bssid;
    uint8_t *wpa_ie;
    uint8_t *wpa_ie_len;
} wpa_station_join_param_t;

struct wpa_funcs {

    bool (*wpa_sta_init)(void);
    bool (*wpa_sta_deinit)(void);
    int (*wpa_sta_connect)(uint8_t *bssid);
    void (*wpa_sta_connected_cb)(uint8_t *bssid);
    void (*wpa_sta_disconnected_cb)(uint8_t reason);
    int (*wpa_sta_rx_eapol)(uint8_t *src_addr, uint8_t *buf, uint32_t len);
    bool (*wpa_sta_in_4way_handshake)(void);
    void *(*wpa_ap_init)(void);
    bool (*wpa_ap_deinit)(void *data);
    bool (*wpa_ap_join)(wpa_station_join_param_t *join_param);
    bool (*wpa_ap_remove)(uint8_t *bssid);
    uint8_t *(*wpa_ap_get_wpa_ie)(size_t *len);
    bool (*wpa_ap_rx_eapol)(void *hapd, void *sm, uint8_t *data, size_t data_len);
    void (*wpa_ap_get_peer_spp_msg)(void *sm, bool *spp_cap, bool *spp_req);
    char *(*wpa_config_parse_string)(const char *value, size_t *len);
    int (*wpa_parse_wpa_ie)(const uint8_t *wpa_ie, size_t wpa_ie_len, wifi_wpa_ie_t *data);
    int (*wpa_config_bss)(uint8_t *bssid);
    int (*wpa_michael_mic_failure)(uint16_t is_unicast);
    uint8_t *(*wpa3_build_sae_msg)(uint8_t *bssid, uint32_t type, size_t *len);
    int (*wpa3_parse_sae_msg)(uint8_t *buf, size_t len, uint32_t type, uint16_t status);
    int (*wpa3_hostap_handle_auth)(uint8_t *buf, size_t len, uint32_t type, uint16_t status, uint8_t *bssid);
    int (*wpa_sta_rx_mgmt)(uint8_t type, uint8_t *frame, size_t len, uint8_t *sender, int8_t rssi, uint8_t channel, uint64_t current_tsf);
    void (*wpa_config_done)(void);
    uint8_t *(*owe_build_dhie)(uint16_t group);
    int (*owe_process_assoc_resp)(const uint8_t *rsn_ie, size_t rsn_len, const uint8_t *dh_ie, size_t dh_len);
    void (*wpa_sta_clear_curr_pmksa)(void);
    void (*wpa_config_reload)(void);
    int (*wpa_parse_wpa_ie_scan_only)(const uint8_t *wpa_ie, size_t wpa_ie_len, wifi_wpa_ie_t *data);
};
#if defined(__riscv)
/* Same 28 slots in the same order as ESP-IDF 66ab063a9a7f esp_wifi_driver.h */
_Static_assert(sizeof(struct wpa_funcs) == 112U, "struct wpa_funcs: 28 callbacks");
#endif

extern int esp_wifi_register_wpa_cb_internal(struct wpa_funcs *cb);

static bool s_wpa_sta_init(void)
{
    /* Station WPA context initialized at kernel boot; do not wipe configured credentials */
    return true;
}
static bool s_wpa_sta_deinit(void)
{
    wpa2_client_stop();
    return true;
}
static int s_wpa_sta_connect(uint8_t *bssid)
{
    wpa2_client_on_connected(bssid);
    return 0;
}
static void s_wpa_sta_connected_cb(uint8_t *bssid)
{
    console_puts("[WPA] Connected CB\r\n");
    wpa2_client_on_connected(bssid);
}
static void s_wpa_sta_disconnected_cb(uint8_t reason)
{
    console_puts("[WPA] Disconnected CB, reason=");
    put_dec((uint32_t)reason);
    console_puts("\r\n");
    wpa2_client_on_disconnected(reason);
}
static int s_wpa_sta_rx_eapol(uint8_t *src_addr, uint8_t *buf, uint32_t len)
{
    console_puts("[WPA] RX EAPOL frame len=");
    put_dec(len);
    console_puts("\r\n");
    return (int)wpa2_client_rx_eapol(src_addr, buf, (uint16_t)len);
}
static bool s_wpa_sta_in_4way(void)
{
    return wpa2_client_is_in_4way();
}

static void *s_wpa_ap_init(void) { return NULL; }
static bool s_wpa_ap_deinit(void *data) { (void)data; return true; }
static bool s_wpa_ap_join(wpa_station_join_param_t *j) { (void)j; return true; }
static bool s_wpa_ap_remove(uint8_t *b) { (void)b; return false; }
static uint8_t *s_wpa_ap_get_ie(size_t *len) { if (len) *len = 0; return NULL; }
static bool s_wpa_ap_rx_eapol(void *h, void *sm, uint8_t *d, size_t l) { (void)h; (void)sm; (void)d; (void)l; return false; }
static void s_wpa_ap_get_peer_spp(void *sm, bool *cap, bool *req) { (void)sm; if (cap) *cap = false; if (req) *req = false; }
static char *s_wpa_config_parse_string(const char *v, size_t *len) { (void)v; if (len) *len = 0; return NULL; }
static int s_wpa_parse_wpa_ie(const uint8_t *wpa_ie, size_t wpa_ie_len, wifi_wpa_ie_t *data)
{
    if (data == NULL) return -1;
    memset(data, 0, sizeof(wifi_wpa_ie_t));
    if (wpa_ie != NULL && wpa_ie_len > 0U)
    {
        if (wpa_ie[0] == 0x30U) /* RSN IE */
        {
            data->proto = 2; /* RSN */
            data->pairwise_cipher = 4; /* CCMP */
            data->group_cipher = 4;
            data->key_mgmt = 2; /* PSK */
        }
        else if (wpa_ie[0] == 0xDDU) /* WPA IE */
        {
            data->proto = 1; /* WPA */
            data->pairwise_cipher = 3; /* TKIP */
            data->group_cipher = 3;
            data->key_mgmt = 2; /* PSK */
        }
        else
        {
            data->proto = 2;
            data->pairwise_cipher = 4;
            data->group_cipher = 4;
            data->key_mgmt = 2;
        }
    }
    return 0;
}
static int s_wpa_config_bss(uint8_t *bssid) { (void)bssid; return 0; }
static int s_wpa_michael_mic_failure(uint16_t u) { (void)u; return 0; }
static int s_wpa_sta_rx_mgmt(uint8_t t, uint8_t *f, size_t l, uint8_t *s, int8_t r, uint8_t c, uint64_t tsf)
{
    (void)t; (void)f; (void)l; (void)s; (void)r; (void)c; (void)tsf;
    return 0;
}
static void s_wpa_config_done(void) {}
static void s_wpa_sta_clear_curr_pmksa(void) {}
static void s_wpa_config_reload(void) {}
static int s_wpa_parse_wpa_ie_scan_only(const uint8_t *ie, size_t len, wifi_wpa_ie_t *d)
{
    return s_wpa_parse_wpa_ie(ie, len, d);
}

static struct wpa_funcs s_wpa_funcs = {
    .wpa_sta_init               = s_wpa_sta_init,
    .wpa_sta_deinit             = s_wpa_sta_deinit,
    .wpa_sta_connect            = s_wpa_sta_connect,
    .wpa_sta_connected_cb       = s_wpa_sta_connected_cb,
    .wpa_sta_disconnected_cb    = s_wpa_sta_disconnected_cb,
    .wpa_sta_rx_eapol           = s_wpa_sta_rx_eapol,
    .wpa_sta_in_4way_handshake  = s_wpa_sta_in_4way,
    .wpa_ap_init                = s_wpa_ap_init,
    .wpa_ap_deinit              = s_wpa_ap_deinit,
    .wpa_ap_join                = s_wpa_ap_join,
    .wpa_ap_remove              = s_wpa_ap_remove,
    .wpa_ap_get_wpa_ie          = s_wpa_ap_get_ie,
    .wpa_ap_rx_eapol            = s_wpa_ap_rx_eapol,
    .wpa_ap_get_peer_spp_msg    = s_wpa_ap_get_peer_spp,
    .wpa_config_parse_string    = s_wpa_config_parse_string,
    .wpa_parse_wpa_ie           = s_wpa_parse_wpa_ie,
    .wpa_config_bss             = s_wpa_config_bss,
    .wpa_michael_mic_failure    = s_wpa_michael_mic_failure,
    .wpa3_build_sae_msg         = NULL,
    .wpa3_parse_sae_msg         = NULL,
    .wpa3_hostap_handle_auth    = NULL,
    .wpa_sta_rx_mgmt            = s_wpa_sta_rx_mgmt,
    .wpa_config_done            = s_wpa_config_done,
    .owe_build_dhie             = NULL,
    .owe_process_assoc_resp     = NULL,
    .wpa_sta_clear_curr_pmksa   = s_wpa_sta_clear_curr_pmksa,
    .wpa_config_reload          = s_wpa_config_reload,
    .wpa_parse_wpa_ie_scan_only = s_wpa_parse_wpa_ie_scan_only,
};

void wifi_os_adapter_register_wpa_stubs(void)
{
    esp_wifi_register_wpa_cb_internal(&s_wpa_funcs);
}

#else

/* Host Test Emulation Stubs */
uint32_t interrupt_global_save_and_disable(void) { return 0; }
void interrupt_global_restore(uint32_t prev_mstatus) { (void)prev_mstatus; }
void console_puts(const char *s) { (void)s; }
void put_hex(uint32_t val) { (void)val; }
void put_dec(uint32_t val) { (void)val; }
void wifi_os_adapter_poll(void) {}
void wifi_os_adapter_register_wpa_stubs(void) {}
static void wifi_timer_invalidate_handle(void *ptr) { (void)ptr; }

#endif /* defined(__riscv) */


void wifi_os_adapter_init(void)
{
    /* 1. Initialize deterministic static heap for Wi-Fi allocations */
    wifi_heap_init();

#if defined(__riscv)
    /* 2. Clear timer tracker slots */
    for (uint32_t i = 0U; i < WIFI_MAX_ACTIVE_TIMERS; i++)
    {
        s_timer_trackers[i].timer_handle = NULL;
        s_timer_trackers[i].fn = NULL;
        s_timer_trackers[i].arg = NULL;
        s_timer_trackers[i].active = false;
        s_timer_trackers[i].expire_us = 0ULL;
        s_timer_trackers[i].period_us = 0U;
        s_timer_trackers[i].repeat = false;
    }

    /* 3. Zero-initialize the ROM Wi-Fi & Coexistence BSS pointers region
     * so stale power-on / reboot data in SRAM does not falsely indicate initialized locks or tasks.
     * Crucially clears g_osi_funcs_p at 0x4087FF6C, enabling wifi_osi_funcs_register() to proceed. */
    volatile uint32_t *p = (volatile uint32_t *)ESP32C6_WIFI_ROM_BSS_START;
    while (p < (volatile uint32_t *)ESP32C6_WIFI_ROM_BSS_END)
    {
        *p++ = 0U;
    }

    /* 4. Enable vendor Wi-Fi stack diagnostic logging */
    extern uint32_t g_log_level;
    extern uint32_t g_log_mod;
    g_log_level = 3U;
    g_log_mod = 0xFFFFFFFFU;
#endif
}
