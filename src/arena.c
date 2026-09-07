/*
 * src/arena.c
 *
 * Deterministic Static Arena Memory Allocator Implementation
 * Authoritative Architecture: ESP32-C6 RISC-V Bare-Metal Runtime
 *
 * Enforces zero dynamic heap fragmentation via fixed-size static block pools
 * in HP SRAM DRAM (.bss) and an 8 KB linear scratch arena with mark/reset stack semantics.
 */

#include "arena.h"
#include "string.h"

/* Static Memory Pools Residing in HP SRAM DRAM (.bss) */
static uint8_t s_small_pool_mem[ARENA_POOL_TOTAL_SIZE_SMALL] __attribute__((aligned(ARENA_ALIGN_BYTES)));
static uint8_t s_medium_pool_mem[ARENA_POOL_TOTAL_SIZE_MEDIUM] __attribute__((aligned(ARENA_ALIGN_BYTES)));
static uint8_t s_scratch_mem[ARENA_SCRATCH_TOTAL_SIZE] __attribute__((aligned(ARENA_ALIGN_BYTES)));

/* Static Descriptors */
static arena_pool_t    s_small_pool;
static arena_pool_t    s_medium_pool;
static arena_scratch_t s_scratch;

/* Internal Helper: Initialize a Block Pool Instance */
static void arena_init_pool_instance(arena_pool_t *pool,
                                     uint8_t      *mem,
                                     uint32_t      block_size,
                                     uint32_t      block_count)
{
    pool->memory            = mem;
    pool->block_size        = block_size;
    pool->block_count       = block_count;
    pool->allocated_mask    = ARENA_BITMASK_EMPTY;
    pool->active_count      = 0U;
    pool->high_watermark    = 0U;
    pool->total_alloc_count = 0U;
    pool->total_free_count  = 0U;

    /* Build intrusive singly linked free-list linking each block to the next */
    for (uint32_t i = 0U; i < (block_count - 1U); i++)
    {
        arena_free_node_t *curr = (arena_free_node_t *)(mem + (i * block_size));
        arena_free_node_t *next = (arena_free_node_t *)(mem + ((i + 1U) * block_size));
        curr->next = next;
    }
    arena_free_node_t *last = (arena_free_node_t *)(mem + ((block_count - 1U) * block_size));
    last->next = NULL;

    pool->free_head = (arena_free_node_t *)mem;
}

void arena_init(void)
{
    arena_init_pool_instance(&s_small_pool,
                             s_small_pool_mem,
                             ARENA_POOL_BLOCK_SIZE_SMALL,
                             ARENA_POOL_BLOCK_COUNT_SMALL);

    arena_init_pool_instance(&s_medium_pool,
                             s_medium_pool_mem,
                             ARENA_POOL_BLOCK_SIZE_MEDIUM,
                             ARENA_POOL_BLOCK_COUNT_MEDIUM);

    s_scratch.buffer            = s_scratch_mem;
    s_scratch.capacity          = ARENA_SCRATCH_TOTAL_SIZE;
    s_scratch.offset            = 0U;
    s_scratch.high_watermark    = 0U;
    s_scratch.total_alloc_count = 0U;
    s_scratch.total_reset_count = 0U;
}

void *arena_alloc_pool(arena_pool_id_t pool_id)
{
    arena_pool_t *pool = NULL;
    if (pool_id == ARENA_POOL_SMALL)
    {
        pool = &s_small_pool;
    }
    else if (pool_id == ARENA_POOL_MEDIUM)
    {
        pool = &s_medium_pool;
    }
    else
    {
        return NULL;
    }

    if (pool->memory == NULL || pool->free_head == NULL || pool->block_size == 0U)
    {
        return NULL;
    }

    /* Pop head from intrusive free list in O(1) time */
    arena_free_node_t *node = pool->free_head;

    /* Validate free-list node bounds and alignment to guard against heap corruption */
    uint8_t *node_bytes = (uint8_t *)node;
    if ((node_bytes < pool->memory) ||
        (node_bytes >= (pool->memory + (pool->block_size * pool->block_count))))
    {
        pool->free_head = NULL;
        return NULL;
    }

    uint32_t offset = (uint32_t)(node_bytes - pool->memory);
    if ((offset % pool->block_size) != 0U)
    {
        pool->free_head = NULL;
        return NULL;
    }

    uint32_t idx = offset / pool->block_size;
    if (idx >= pool->block_count)
    {
        pool->free_head = NULL;
        return NULL;
    }

    uint32_t bit = (1U << idx);
    if ((pool->allocated_mask & bit) != 0U)
    {
        /* Corrupted free list contains already allocated node */
        pool->free_head = NULL;
        return NULL;
    }

    pool->free_head = node->next;

    pool->allocated_mask |= bit;
    pool->active_count++;
    if (pool->active_count > pool->high_watermark)
    {
        pool->high_watermark = pool->active_count;
    }
    pool->total_alloc_count++;

    /* Clear block memory to prevent caller reading stale contents */
    memset(node, 0, pool->block_size);
    return (void *)node;
}

void *arena_alloc(size_t size)
{
    if (size == 0U)
    {
        return NULL;
    }

    if (size <= ARENA_POOL_BLOCK_SIZE_SMALL)
    {
        return arena_alloc_pool(ARENA_POOL_SMALL);
    }
    else if (size <= ARENA_POOL_BLOCK_SIZE_MEDIUM)
    {
        return arena_alloc_pool(ARENA_POOL_MEDIUM);
    }

    return NULL;
}

int arena_free(void *ptr)
{
    if (ptr == NULL || s_small_pool.memory == NULL || s_medium_pool.memory == NULL)
    {
        return ARENA_FREE_FAIL;
    }

    arena_pool_t *pool = NULL;
    uint8_t *p = (uint8_t *)ptr;

    if ((p >= s_small_pool.memory) &&
        (p < (s_small_pool.memory + ARENA_POOL_TOTAL_SIZE_SMALL)))
    {
        pool = &s_small_pool;
    }
    else if ((p >= s_medium_pool.memory) &&
             (p < (s_medium_pool.memory + ARENA_POOL_TOTAL_SIZE_MEDIUM)))
    {
        pool = &s_medium_pool;
    }
    else
    {
        /* Pointer does not originate from any managed static pool */
        return ARENA_FREE_FAIL;
    }

    if (pool->block_size == 0U || pool->block_count == 0U)
    {
        return ARENA_FREE_FAIL;
    }

    uint32_t offset = (uint32_t)(p - pool->memory);
    if ((offset % pool->block_size) != 0U)
    {
        /* Misaligned pointer: not pointing to a valid block boundary */
        return ARENA_FREE_FAIL;
    }

    uint32_t idx = offset / pool->block_size;
    if (idx >= pool->block_count)
    {
        return ARENA_FREE_FAIL;
    }

    uint32_t bit = (1U << idx);
    if ((pool->allocated_mask & bit) == 0U)
    {
        /* Double-free guard: block was not marked as allocated */
        return ARENA_FREE_FAIL;
    }

    /* Clear allocation tracking bit */
    pool->allocated_mask &= ~bit;

    /* Push block back onto intrusive singly linked free list in O(1) time */
    arena_free_node_t *node = (arena_free_node_t *)p;
    node->next = pool->free_head;
    pool->free_head = node;

    if (pool->active_count > 0U)
    {
        pool->active_count--;
    }
    pool->total_free_count++;

    return ARENA_FREE_SUCCESS;
}

void *arena_scratch_alloc(size_t size)
{
    if (s_scratch.buffer == NULL || size == 0U)
    {
        return NULL;
    }

    /* Guard against integer overflow and capacity overflow before alignment */
    if (size > (s_scratch.capacity - s_scratch.offset))
    {
        return NULL;
    }

    /* Align requested size to word boundary */
    size_t aligned_size = (size + ARENA_ALIGN_MASK) & ~((size_t)ARENA_ALIGN_MASK);

    if (aligned_size > (s_scratch.capacity - s_scratch.offset))
    {
        /* Scratch arena exhaustion guard */
        return NULL;
    }

    void *ptr = (void *)(s_scratch.buffer + s_scratch.offset);
    s_scratch.offset += aligned_size;

    if (s_scratch.offset > s_scratch.high_watermark)
    {
        s_scratch.high_watermark = s_scratch.offset;
    }
    s_scratch.total_alloc_count++;

    memset(ptr, 0, aligned_size);
    return ptr;
}

arena_scratch_mark_t arena_scratch_mark(void)
{
    if (s_scratch.buffer == NULL)
    {
        return 0U;
    }
    return (arena_scratch_mark_t)s_scratch.offset;
}

void arena_scratch_reset(arena_scratch_mark_t mark)
{
    if (s_scratch.buffer == NULL)
    {
        return;
    }

    /* In a stack arena, a valid reset must rewind (mark <= offset) and be word-aligned */
    if (((size_t)mark <= s_scratch.offset) && (((size_t)mark & (size_t)ARENA_ALIGN_MASK) == 0U))
    {
        s_scratch.offset = (size_t)mark;
        s_scratch.total_reset_count++;
    }
}

void arena_get_stats(arena_telemetry_t *out_stats)
{
    if (out_stats == NULL)
    {
        return;
    }

    memset(out_stats, 0, sizeof(*out_stats));
    arena_get_pool_stats(ARENA_POOL_SMALL, &out_stats->small_pool);
    arena_get_pool_stats(ARENA_POOL_MEDIUM, &out_stats->medium_pool);
    arena_get_scratch_stats(&out_stats->scratch);
}

void arena_get_pool_stats(arena_pool_id_t pool_id, arena_pool_stats_t *out_stats)
{
    if (out_stats == NULL)
    {
        return;
    }

    memset(out_stats, 0, sizeof(*out_stats));

    arena_pool_t *pool = NULL;
    if (pool_id == ARENA_POOL_SMALL)
    {
        pool = &s_small_pool;
    }
    else if (pool_id == ARENA_POOL_MEDIUM)
    {
        pool = &s_medium_pool;
    }
    else
    {
        return;
    }

    out_stats->block_size        = pool->block_size;
    out_stats->block_count       = pool->block_count;
    out_stats->active_count      = pool->active_count;
    out_stats->high_watermark    = pool->high_watermark;
    out_stats->allocated_mask    = pool->allocated_mask;
    out_stats->total_alloc_count = pool->total_alloc_count;
    out_stats->total_free_count  = pool->total_free_count;
}

void arena_get_scratch_stats(arena_scratch_stats_t *out_stats)
{
    if (out_stats == NULL)
    {
        return;
    }

    memset(out_stats, 0, sizeof(*out_stats));

    out_stats->capacity          = s_scratch.capacity;
    out_stats->current_offset    = s_scratch.offset;
    out_stats->high_watermark    = s_scratch.high_watermark;
    out_stats->total_alloc_count = s_scratch.total_alloc_count;
    out_stats->total_reset_count = s_scratch.total_reset_count;
}
