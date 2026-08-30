/*
 * DS.h
 * Digital Signature
 * Base Address: 0x6008C000
 */

#ifndef DS_H
#define DS_H

#include <stdint.h>

#define DS_BASE 0x6008C000

// memory that stores Y
#define DS_Y_MEM_REG ((volatile uint32_t *)(DS_BASE + 0x0))

// memory that stores M
#define DS_M_MEM_REG ((volatile uint32_t *)(DS_BASE + 0x200))

// memory that stores Rb
#define DS_RB_MEM_REG ((volatile uint32_t *)(DS_BASE + 0x400))

// memory that stores BOX
#define DS_BOX_MEM_REG ((volatile uint32_t *)(DS_BASE + 0x600))

// memory that stores IV
#define DS_IV_MEM_REG ((volatile uint32_t *)(DS_BASE + 0x630))

// memory that stores X
#define DS_X_MEM_REG ((volatile uint32_t *)(DS_BASE + 0x800))

// memory that stores Z
#define DS_Z_MEM_REG ((volatile uint32_t *)(DS_BASE + 0xA00))

// DS start control register
#define DS_SET_START_REG ((volatile uint32_t *)(DS_BASE + 0xE00))
#define DS_SET_START_SET_START_M (0x00000001U)
#define DS_SET_START_SET_START_S (0)
#define DS_SET_START_SET_START_V(v) (((v) << 0) & 0x00000001U)

// DS continue control register
#define DS_SET_CONTINUE_REG ((volatile uint32_t *)(DS_BASE + 0xE04))
#define DS_SET_CONTINUE_SET_CONTINUE_M (0x00000001U)
#define DS_SET_CONTINUE_SET_CONTINUE_S (0)
#define DS_SET_CONTINUE_SET_CONTINUE_V(v) (((v) << 0) & 0x00000001U)

// DS finish control register
#define DS_SET_FINISH_REG ((volatile uint32_t *)(DS_BASE + 0xE08))
#define DS_SET_FINISH_SET_FINISH_M (0x00000001U)
#define DS_SET_FINISH_SET_FINISH_S (0)
#define DS_SET_FINISH_SET_FINISH_V(v) (((v) << 0) & 0x00000001U)

// DS query busy register
#define DS_QUERY_BUSY_REG ((volatile uint32_t *)(DS_BASE + 0xE0C))
#define DS_QUERY_BUSY_QUERY_BUSY_M (0x00000001U)
#define DS_QUERY_BUSY_QUERY_BUSY_S (0)
#define DS_QUERY_BUSY_QUERY_BUSY_V(v) (((v) << 0) & 0x00000001U)

// DS query key-wrong counter register
#define DS_QUERY_KEY_WRONG_REG ((volatile uint32_t *)(DS_BASE + 0xE10))
#define DS_QUERY_KEY_WRONG_QUERY_KEY_WRONG_M (0x0000000FU)
#define DS_QUERY_KEY_WRONG_QUERY_KEY_WRONG_S (0)
#define DS_QUERY_KEY_WRONG_QUERY_KEY_WRONG_V(v) (((v) << 0) & 0x0000000FU)

// DS query check result register
#define DS_QUERY_CHECK_REG ((volatile uint32_t *)(DS_BASE + 0xE14))
#define DS_QUERY_CHECK_MD_ERROR_M (0x00000001U)
#define DS_QUERY_CHECK_MD_ERROR_S (0)
#define DS_QUERY_CHECK_MD_ERROR_V(v) (((v) << 0) & 0x00000001U)
#define DS_QUERY_CHECK_PADDING_BAD_M (0x00000002U)
#define DS_QUERY_CHECK_PADDING_BAD_S (1)
#define DS_QUERY_CHECK_PADDING_BAD_V(v) (((v) << 1) & 0x00000002U)

// DS version control register
#define DS_DATE_REG ((volatile uint32_t *)(DS_BASE + 0xE20))
#define DS_DATE_DATE_M (0x3FFFFFFFU)
#define DS_DATE_DATE_S (0)
#define DS_DATE_DATE_V(v) (((v) << 0) & 0x3FFFFFFFU)

#endif // DS_H
