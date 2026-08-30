/*
 * ATOMIC.h
 * Atomic Locker
 * Base Address: 0x60011000
 */

#ifndef ATOMIC_H
#define ATOMIC_H

#include <stdint.h>

#define ATOMIC_BASE 0x60011000

// hardware lock regsiter
#define ATOMIC_ADDR_LOCK_REG ((volatile uint32_t *)(ATOMIC_BASE + 0x0))
#define ATOMIC_ADDR_LOCK_LOCK_M (0x00000003U)
#define ATOMIC_ADDR_LOCK_LOCK_S (0)
#define ATOMIC_ADDR_LOCK_LOCK_V(v) (((v) << 0) & 0x00000003U)

// gloable lr address regsiter
#define ATOMIC_LR_ADDR_REG ((volatile uint32_t *)(ATOMIC_BASE + 0x4))
#define ATOMIC_LR_ADDR_GLOABLE_LR_ADDR_M (0xFFFFFFFFU)
#define ATOMIC_LR_ADDR_GLOABLE_LR_ADDR_S (0)
#define ATOMIC_LR_ADDR_GLOABLE_LR_ADDR_V(v) (((v) << 0) & 0xFFFFFFFFU)

// gloable lr value regsiter
#define ATOMIC_LR_VALUE_REG ((volatile uint32_t *)(ATOMIC_BASE + 0x8))
#define ATOMIC_LR_VALUE_GLOABLE_LR_VALUE_M (0xFFFFFFFFU)
#define ATOMIC_LR_VALUE_GLOABLE_LR_VALUE_S (0)
#define ATOMIC_LR_VALUE_GLOABLE_LR_VALUE_V(v) (((v) << 0) & 0xFFFFFFFFU)

// lock status regsiter
#define ATOMIC_LOCK_STATUS_REG ((volatile uint32_t *)(ATOMIC_BASE + 0xC))
#define ATOMIC_LOCK_STATUS_LOCK_STATUS_M (0x00000003U)
#define ATOMIC_LOCK_STATUS_LOCK_STATUS_S (0)
#define ATOMIC_LOCK_STATUS_LOCK_STATUS_V(v) (((v) << 0) & 0x00000003U)

// wait counter register
#define ATOMIC_COUNTER_REG ((volatile uint32_t *)(ATOMIC_BASE + 0x10))
#define ATOMIC_COUNTER_WAIT_COUNTER_M (0x0000FFFFU)
#define ATOMIC_COUNTER_WAIT_COUNTER_S (0)
#define ATOMIC_COUNTER_WAIT_COUNTER_V(v) (((v) << 0) & 0x0000FFFFU)

#endif // ATOMIC_H
