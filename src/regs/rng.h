/*
 * RNG.h
 * Hardware Random Number Generator
 * Base Address: 0x600B2800
 */

#ifndef RNG_H
#define RNG_H

#include <stdint.h>

#define RNG_BASE 0x600B2800

// Random number data
#define RNG_DATA_REG ((volatile uint32_t *)(RNG_BASE + 0x8))

#endif // RNG_H
