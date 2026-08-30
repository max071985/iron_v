/*
 * HMAC.h
 * HMAC (Hash-based Message Authentication Code) Accelerator
 * Base Address: 0x6008D000
 */

#ifndef HMAC_H
#define HMAC_H

#include <stdint.h>

#define HMAC_BASE 0x6008D000

// Process control register 0.
#define HMAC_SET_START_REG ((volatile uint32_t *)(HMAC_BASE + 0x40))
#define HMAC_SET_START_SET_START_M (0x00000001U)
#define HMAC_SET_START_SET_START_S (0)
#define HMAC_SET_START_SET_START_V(v) (((v) << 0) & 0x00000001U)

// Configure purpose.
#define HMAC_SET_PARA_PURPOSE_REG ((volatile uint32_t *)(HMAC_BASE + 0x44))
#define HMAC_SET_PARA_PURPOSE_PURPOSE_SET_M (0x0000000FU)
#define HMAC_SET_PARA_PURPOSE_PURPOSE_SET_S (0)
#define HMAC_SET_PARA_PURPOSE_PURPOSE_SET_V(v) (((v) << 0) & 0x0000000FU)

// Configure key.
#define HMAC_SET_PARA_KEY_REG ((volatile uint32_t *)(HMAC_BASE + 0x48))
#define HMAC_SET_PARA_KEY_KEY_SET_M (0x00000007U)
#define HMAC_SET_PARA_KEY_KEY_SET_S (0)
#define HMAC_SET_PARA_KEY_KEY_SET_V(v) (((v) << 0) & 0x00000007U)

// Finish initial configuration.
#define HMAC_SET_PARA_FINISH_REG ((volatile uint32_t *)(HMAC_BASE + 0x4C))
#define HMAC_SET_PARA_FINISH_SET_PARA_END_M (0x00000001U)
#define HMAC_SET_PARA_FINISH_SET_PARA_END_S (0)
#define HMAC_SET_PARA_FINISH_SET_PARA_END_V(v) (((v) << 0) & 0x00000001U)

// Process control register 1.
#define HMAC_SET_MESSAGE_ONE_REG ((volatile uint32_t *)(HMAC_BASE + 0x50))
#define HMAC_SET_MESSAGE_ONE_SET_TEXT_ONE_M (0x00000001U)
#define HMAC_SET_MESSAGE_ONE_SET_TEXT_ONE_S (0)
#define HMAC_SET_MESSAGE_ONE_SET_TEXT_ONE_V(v) (((v) << 0) & 0x00000001U)

// Process control register 2.
#define HMAC_SET_MESSAGE_ING_REG ((volatile uint32_t *)(HMAC_BASE + 0x54))
#define HMAC_SET_MESSAGE_ING_SET_TEXT_ING_M (0x00000001U)
#define HMAC_SET_MESSAGE_ING_SET_TEXT_ING_S (0)
#define HMAC_SET_MESSAGE_ING_SET_TEXT_ING_V(v) (((v) << 0) & 0x00000001U)

// Process control register 3.
#define HMAC_SET_MESSAGE_END_REG ((volatile uint32_t *)(HMAC_BASE + 0x58))
#define HMAC_SET_MESSAGE_END_SET_TEXT_END_M (0x00000001U)
#define HMAC_SET_MESSAGE_END_SET_TEXT_END_S (0)
#define HMAC_SET_MESSAGE_END_SET_TEXT_END_V(v) (((v) << 0) & 0x00000001U)

// Process control register 4.
#define HMAC_SET_RESULT_FINISH_REG ((volatile uint32_t *)(HMAC_BASE + 0x5C))
#define HMAC_SET_RESULT_FINISH_SET_RESULT_END_M (0x00000001U)
#define HMAC_SET_RESULT_FINISH_SET_RESULT_END_S (0)
#define HMAC_SET_RESULT_FINISH_SET_RESULT_END_V(v) (((v) << 0) & 0x00000001U)

// Invalidate register 0.
#define HMAC_SET_INVALIDATE_JTAG_REG ((volatile uint32_t *)(HMAC_BASE + 0x60))
#define HMAC_SET_INVALIDATE_JTAG_SET_INVALIDATE_JTAG_M (0x00000001U)
#define HMAC_SET_INVALIDATE_JTAG_SET_INVALIDATE_JTAG_S (0)
#define HMAC_SET_INVALIDATE_JTAG_SET_INVALIDATE_JTAG_V(v) (((v) << 0) & 0x00000001U)

// Invalidate register 1.
#define HMAC_SET_INVALIDATE_DS_REG ((volatile uint32_t *)(HMAC_BASE + 0x64))
#define HMAC_SET_INVALIDATE_DS_SET_INVALIDATE_DS_M (0x00000001U)
#define HMAC_SET_INVALIDATE_DS_SET_INVALIDATE_DS_S (0)
#define HMAC_SET_INVALIDATE_DS_SET_INVALIDATE_DS_V(v) (((v) << 0) & 0x00000001U)

// Error register.
#define HMAC_QUERY_ERROR_REG ((volatile uint32_t *)(HMAC_BASE + 0x68))
#define HMAC_QUERY_ERROR_QUERY_CHECK_M (0x00000001U)
#define HMAC_QUERY_ERROR_QUERY_CHECK_S (0)
#define HMAC_QUERY_ERROR_QUERY_CHECK_V(v) (((v) << 0) & 0x00000001U)

// Busy register.
#define HMAC_QUERY_BUSY_REG ((volatile uint32_t *)(HMAC_BASE + 0x6C))
#define HMAC_QUERY_BUSY_BUSY_STATE_M (0x00000001U)
#define HMAC_QUERY_BUSY_BUSY_STATE_S (0)
#define HMAC_QUERY_BUSY_BUSY_STATE_V(v) (((v) << 0) & 0x00000001U)

// Message block memory.
#define HMAC_WR_MESSAGE_MEM_REG ((volatile uint32_t *)(HMAC_BASE + 0x80))

// Result from upstream.
#define HMAC_RD_RESULT_MEM_REG ((volatile uint32_t *)(HMAC_BASE + 0xC0))

// Process control register 5.
#define HMAC_SET_MESSAGE_PAD_REG ((volatile uint32_t *)(HMAC_BASE + 0xF0))
#define HMAC_SET_MESSAGE_PAD_SET_TEXT_PAD_M (0x00000001U)
#define HMAC_SET_MESSAGE_PAD_SET_TEXT_PAD_S (0)
#define HMAC_SET_MESSAGE_PAD_SET_TEXT_PAD_V(v) (((v) << 0) & 0x00000001U)

// Process control register 6.
#define HMAC_ONE_BLOCK_REG ((volatile uint32_t *)(HMAC_BASE + 0xF4))
#define HMAC_ONE_BLOCK_SET_ONE_BLOCK_M (0x00000001U)
#define HMAC_ONE_BLOCK_SET_ONE_BLOCK_S (0)
#define HMAC_ONE_BLOCK_SET_ONE_BLOCK_V(v) (((v) << 0) & 0x00000001U)

// Jtag register 0.
#define HMAC_SOFT_JTAG_CTRL_REG ((volatile uint32_t *)(HMAC_BASE + 0xF8))
#define HMAC_SOFT_JTAG_CTRL_SOFT_JTAG_CTRL_M (0x00000001U)
#define HMAC_SOFT_JTAG_CTRL_SOFT_JTAG_CTRL_S (0)
#define HMAC_SOFT_JTAG_CTRL_SOFT_JTAG_CTRL_V(v) (((v) << 0) & 0x00000001U)

// Jtag register 1.
#define HMAC_WR_JTAG_REG ((volatile uint32_t *)(HMAC_BASE + 0xFC))
#define HMAC_WR_JTAG_WR_JTAG_M (0xFFFFFFFFU)
#define HMAC_WR_JTAG_WR_JTAG_S (0)
#define HMAC_WR_JTAG_WR_JTAG_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Date register.
#define HMAC_DATE_REG ((volatile uint32_t *)(HMAC_BASE + 0x1FC))
#define HMAC_DATE_DATE_M (0x3FFFFFFFU)
#define HMAC_DATE_DATE_S (0)
#define HMAC_DATE_DATE_V(v) (((v) << 0) & 0x3FFFFFFFU)

#endif // HMAC_H
