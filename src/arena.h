/*
 * src/arena.h
 *
 * Deterministic Static Arena Memory Allocator
 * Authoritative Architecture: ESP32-C6 RISC-V Bare-Metal Runtime
 *
 * Provides O(1) deterministic fixed-size static block pools (Small: 32x64B, Medium: 16x256B)
 * with intrusive singly linked free-lists and bitmask state tracking, plus an 8 KB linear
 * scratch arena with O(1) stack-like mark and reset semantics.
 * Prohibits dynamic heap allocation (malloc/free) to guarantee zero memory fragmentation.
 */

#ifndef ARENA_H
#define ARENA_H

#include <stdint.h>
#include <stddef.h>

/* Alignment Constraints */
#define ARENA_ALIGN_BYTES               4U
#define ARENA_ALIGN_MASK                (ARENA_ALIGN_BYTES - 1U)

/* Small Block Pool Dimensions: 32 blocks of 64 bytes (2048 bytes) */
#define ARENA_POOL_BLOCK_SIZE_SMALL     64U
#define ARENA_POOL_BLOCK_COUNT_SMALL    32U
#define ARENA_POOL_TOTAL_SIZE_SMALL     (ARENA_POOL_BLOCK_SIZE_SMALL * ARENA_POOL_BLOCK_COUNT_SMALL)

/* Medium Block Pool Dimensions: 16 blocks of 256 bytes (4096 bytes) */
#define ARENA_POOL_BLOCK_SIZE_MEDIUM    256U
#define ARENA_POOL_BLOCK_COUNT_MEDIUM   16U
#define ARENA_POOL_TOTAL_SIZE_MEDIUM    (ARENA_POOL_BLOCK_SIZE_MEDIUM * ARENA_POOL_BLOCK_COUNT_MEDIUM)

/* Linear Scratch Arena Dimensions: 8 KB (8192 bytes) */
#define ARENA_SCRATCH_TOTAL_SIZE        8192U

/* Bitmask Tracking Constants */
#define ARENA_BITMASK_EMPTY             0x00000000U
#define ARENA_BITMASK_FULL_SMALL        0xFFFFFFFFU
#define ARENA_BITMASK_FULL_MEDIUM       0x0000FFFFU

/* Status / Return Codes */
#define ARENA_FREE_SUCCESS              1
#define ARENA_FREE_FAIL                 0

/* Fixed-Size Block Pool Identifiers */
typedef enum {
    ARENA_POOL_SMALL  = 0,
    ARENA_POOL_MEDIUM = 1,
    ARENA_POOL_COUNT  = 2
} arena_pool_id_t;

/* Node for Intrusive Singly Linked Free-List */
typedef struct arena_free_node {
    struct arena_free_node *next;
} arena_free_node_t;

/* Static Block Pool Descriptor */
typedef struct {
    uint8_t            *memory;
    arena_free_node_t  *free_head;
    uint32_t            block_size;
    uint32_t            block_count;
    uint32_t            allocated_mask;
    uint32_t            active_count;
    uint32_t            high_watermark;
    uint32_t            total_alloc_count;
    uint32_t            total_free_count;
} arena_pool_t;

/* O(1) Stack Frame Marker for Scratch Arena */
typedef uint32_t arena_scratch_mark_t;

/* Linear Scratch Arena Descriptor */
typedef struct {
    uint8_t *buffer;
    size_t   capacity;
    size_t   offset;
    size_t   high_watermark;
    uint32_t total_alloc_count;
    uint32_t total_reset_count;
} arena_scratch_t;

/* Block Pool Telemetry Snapshot */
typedef struct {
    uint32_t block_size;
    uint32_t block_count;
    uint32_t active_count;
    uint32_t high_watermark;
    uint32_t allocated_mask;
    uint32_t total_alloc_count;
    uint32_t total_free_count;
} arena_pool_stats_t;

/* Linear Scratch Arena Telemetry Snapshot */
typedef struct {
    size_t   capacity;
    size_t   current_offset;
    size_t   high_watermark;
    uint32_t total_alloc_count;
    uint32_t total_reset_count;
} arena_scratch_stats_t;

/* System-Wide Arena Telemetry Snapshot */
typedef struct {
    arena_pool_stats_t    small_pool;
    arena_pool_stats_t    medium_pool;
    arena_scratch_stats_t scratch;
} arena_telemetry_t;

/* Subsystem Lifecycle */
void arena_init(void);

/* Fixed-Size Block Allocation & Deallocation */
void *arena_alloc(size_t size);
void *arena_alloc_pool(arena_pool_id_t pool_id);
int arena_free(void *ptr);

/* Linear Scratch Arena Primitives */
void *arena_scratch_alloc(size_t size);
arena_scratch_mark_t arena_scratch_mark(void);
void arena_scratch_reset(arena_scratch_mark_t mark);

/* Telemetry & Inspection Queries */
void arena_get_stats(arena_telemetry_t *out_stats);
void arena_get_pool_stats(arena_pool_id_t pool_id, arena_pool_stats_t *out_stats);
void arena_get_scratch_stats(arena_scratch_stats_t *out_stats);

#endif /* ARENA_H */
