/*
 * RSA.h
 * RSA (Rivest Shamir Adleman) Accelerator
 * Base Address: 0x6008A000
 */

#ifndef RSA_H
#define RSA_H

#include <stdint.h>

#define RSA_BASE 0x6008A000

// The memory that stores M
#define RSA_M_MEM_REG ((volatile uint32_t *)(RSA_BASE + 0x0))

// The memory that stores Z
#define RSA_Z_MEM_REG ((volatile uint32_t *)(RSA_BASE + 0x200))

// The memory that stores Y
#define RSA_Y_MEM_REG ((volatile uint32_t *)(RSA_BASE + 0x400))

// The memory that stores X
#define RSA_X_MEM_REG ((volatile uint32_t *)(RSA_BASE + 0x600))

// RSA M_prime register
#define RSA_M_PRIME_REG ((volatile uint32_t *)(RSA_BASE + 0x800))
#define RSA_M_PRIME_M_PRIME_M (0xFFFFFFFFU)
#define RSA_M_PRIME_M_PRIME_S (0)
#define RSA_M_PRIME_M_PRIME_V(v) (((v) << 0) & 0xFFFFFFFFU)

// RSA mode register
#define RSA_MODE_REG ((volatile uint32_t *)(RSA_BASE + 0x804))
#define RSA_MODE_MODE_M (0x0000007FU)
#define RSA_MODE_MODE_S (0)
#define RSA_MODE_MODE_V(v) (((v) << 0) & 0x0000007FU)

// RSA query clean register
#define RSA_QUERY_CLEAN_REG ((volatile uint32_t *)(RSA_BASE + 0x808))
#define RSA_QUERY_CLEAN_QUERY_CLEAN_M (0x00000001U)
#define RSA_QUERY_CLEAN_QUERY_CLEAN_S (0)
#define RSA_QUERY_CLEAN_QUERY_CLEAN_V(v) (((v) << 0) & 0x00000001U)

// RSA modular exponentiation trigger register.
#define RSA_SET_START_MODEXP_REG ((volatile uint32_t *)(RSA_BASE + 0x80C))
#define RSA_SET_START_MODEXP_SET_START_MODEXP_M (0x00000001U)
#define RSA_SET_START_MODEXP_SET_START_MODEXP_S (0)
#define RSA_SET_START_MODEXP_SET_START_MODEXP_V(v) (((v) << 0) & 0x00000001U)

// RSA modular multiplication trigger register.
#define RSA_SET_START_MODMULT_REG ((volatile uint32_t *)(RSA_BASE + 0x810))
#define RSA_SET_START_MODMULT_SET_START_MODMULT_M (0x00000001U)
#define RSA_SET_START_MODMULT_SET_START_MODMULT_S (0)
#define RSA_SET_START_MODMULT_SET_START_MODMULT_V(v) (((v) << 0) & 0x00000001U)

// RSA normal multiplication trigger register.
#define RSA_SET_START_MULT_REG ((volatile uint32_t *)(RSA_BASE + 0x814))
#define RSA_SET_START_MULT_SET_START_MULT_M (0x00000001U)
#define RSA_SET_START_MULT_SET_START_MULT_S (0)
#define RSA_SET_START_MULT_SET_START_MULT_V(v) (((v) << 0) & 0x00000001U)

// RSA query idle register
#define RSA_QUERY_IDLE_REG ((volatile uint32_t *)(RSA_BASE + 0x818))
#define RSA_QUERY_IDLE_QUERY_IDLE_M (0x00000001U)
#define RSA_QUERY_IDLE_QUERY_IDLE_S (0)
#define RSA_QUERY_IDLE_QUERY_IDLE_V(v) (((v) << 0) & 0x00000001U)

// RSA interrupt clear register
#define RSA_INT_CLR_REG ((volatile uint32_t *)(RSA_BASE + 0x81C))
#define RSA_INT_CLR_CLEAR_INTERRUPT_M (0x00000001U)
#define RSA_INT_CLR_CLEAR_INTERRUPT_S (0)
#define RSA_INT_CLR_CLEAR_INTERRUPT_V(v) (((v) << 0) & 0x00000001U)

// RSA constant time option register
#define RSA_CONSTANT_TIME_REG ((volatile uint32_t *)(RSA_BASE + 0x820))
#define RSA_CONSTANT_TIME_CONSTANT_TIME_M (0x00000001U)
#define RSA_CONSTANT_TIME_CONSTANT_TIME_S (0)
#define RSA_CONSTANT_TIME_CONSTANT_TIME_V(v) (((v) << 0) & 0x00000001U)

// RSA search option
#define RSA_SEARCH_ENABLE_REG ((volatile uint32_t *)(RSA_BASE + 0x824))
#define RSA_SEARCH_ENABLE_SEARCH_ENABLE_M (0x00000001U)
#define RSA_SEARCH_ENABLE_SEARCH_ENABLE_S (0)
#define RSA_SEARCH_ENABLE_SEARCH_ENABLE_V(v) (((v) << 0) & 0x00000001U)

// RSA search position configure register
#define RSA_SEARCH_POS_REG ((volatile uint32_t *)(RSA_BASE + 0x828))
#define RSA_SEARCH_POS_SEARCH_POS_M (0x00000FFFU)
#define RSA_SEARCH_POS_SEARCH_POS_S (0)
#define RSA_SEARCH_POS_SEARCH_POS_V(v) (((v) << 0) & 0x00000FFFU)

// RSA interrupt enable register
#define RSA_INT_ENA_REG ((volatile uint32_t *)(RSA_BASE + 0x82C))
#define RSA_INT_ENA_INT_ENA_M (0x00000001U)
#define RSA_INT_ENA_INT_ENA_S (0)
#define RSA_INT_ENA_INT_ENA_V(v) (((v) << 0) & 0x00000001U)

// RSA version control register
#define RSA_DATE_REG ((volatile uint32_t *)(RSA_BASE + 0x830))
#define RSA_DATE_DATE_M (0x3FFFFFFFU)
#define RSA_DATE_DATE_S (0)
#define RSA_DATE_DATE_V(v) (((v) << 0) & 0x3FFFFFFFU)

#endif // RSA_H
