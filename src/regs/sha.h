/*
 * SHA.h
 * SHA (Secure Hash Algorithm) Accelerator
 * Base Address: 0x60089000
 */

#ifndef SHA_H
#define SHA_H

#include <stdint.h>

#define SHA_BASE 0x60089000

// Initial configuration register.
#define SHA_MODE_REG ((volatile uint32_t *)(SHA_BASE + 0x0))
#define SHA_MODE_MODE_M (0x00000007U)
#define SHA_MODE_MODE_S (0)
#define SHA_MODE_MODE_V(v) (((v) << 0) & 0x00000007U)

// SHA 512/t configuration register 0.
#define SHA_T_STRING_REG ((volatile uint32_t *)(SHA_BASE + 0x4))
#define SHA_T_STRING_T_STRING_M (0xFFFFFFFFU)
#define SHA_T_STRING_T_STRING_S (0)
#define SHA_T_STRING_T_STRING_V(v) (((v) << 0) & 0xFFFFFFFFU)

// SHA 512/t configuration register 1.
#define SHA_T_LENGTH_REG ((volatile uint32_t *)(SHA_BASE + 0x8))
#define SHA_T_LENGTH_T_LENGTH_M (0x0000003FU)
#define SHA_T_LENGTH_T_LENGTH_S (0)
#define SHA_T_LENGTH_T_LENGTH_V(v) (((v) << 0) & 0x0000003FU)

// DMA configuration register 0.
#define SHA_DMA_BLOCK_NUM_REG ((volatile uint32_t *)(SHA_BASE + 0xC))
#define SHA_DMA_BLOCK_NUM_DMA_BLOCK_NUM_M (0x0000003FU)
#define SHA_DMA_BLOCK_NUM_DMA_BLOCK_NUM_S (0)
#define SHA_DMA_BLOCK_NUM_DMA_BLOCK_NUM_V(v) (((v) << 0) & 0x0000003FU)

// Typical SHA configuration register 0.
#define SHA_START_REG ((volatile uint32_t *)(SHA_BASE + 0x10))
#define SHA_START_START_M (0xFFFFFFFEU)
#define SHA_START_START_S (1)
#define SHA_START_START_V(v) (((v) << 1) & 0xFFFFFFFEU)

// Typical SHA configuration register 1.
#define SHA_CONTINUE_REG ((volatile uint32_t *)(SHA_BASE + 0x14))
#define SHA_CONTINUE_CONTINUE_M (0xFFFFFFFEU)
#define SHA_CONTINUE_CONTINUE_S (1)
#define SHA_CONTINUE_CONTINUE_V(v) (((v) << 1) & 0xFFFFFFFEU)

// Busy register.
#define SHA_BUSY_REG ((volatile uint32_t *)(SHA_BASE + 0x18))
#define SHA_BUSY_STATE_M (0x00000001U)
#define SHA_BUSY_STATE_S (0)
#define SHA_BUSY_STATE_V(v) (((v) << 0) & 0x00000001U)

// DMA configuration register 1.
#define SHA_DMA_START_REG ((volatile uint32_t *)(SHA_BASE + 0x1C))
#define SHA_DMA_START_DMA_START_M (0x00000001U)
#define SHA_DMA_START_DMA_START_S (0)
#define SHA_DMA_START_DMA_START_V(v) (((v) << 0) & 0x00000001U)

// DMA configuration register 2.
#define SHA_DMA_CONTINUE_REG ((volatile uint32_t *)(SHA_BASE + 0x20))
#define SHA_DMA_CONTINUE_DMA_CONTINUE_M (0x00000001U)
#define SHA_DMA_CONTINUE_DMA_CONTINUE_S (0)
#define SHA_DMA_CONTINUE_DMA_CONTINUE_V(v) (((v) << 0) & 0x00000001U)

// Interrupt clear register.
#define SHA_CLEAR_IRQ_REG ((volatile uint32_t *)(SHA_BASE + 0x24))
#define SHA_CLEAR_IRQ_CLEAR_INTERRUPT_M (0x00000001U)
#define SHA_CLEAR_IRQ_CLEAR_INTERRUPT_S (0)
#define SHA_CLEAR_IRQ_CLEAR_INTERRUPT_V(v) (((v) << 0) & 0x00000001U)

// Interrupt enable register.
#define SHA_IRQ_ENA_REG ((volatile uint32_t *)(SHA_BASE + 0x28))
#define SHA_IRQ_ENA_INTERRUPT_ENA_M (0x00000001U)
#define SHA_IRQ_ENA_INTERRUPT_ENA_S (0)
#define SHA_IRQ_ENA_INTERRUPT_ENA_V(v) (((v) << 0) & 0x00000001U)

// Date register.
#define SHA_DATE_REG ((volatile uint32_t *)(SHA_BASE + 0x2C))
#define SHA_DATE_DATE_M (0x3FFFFFFFU)
#define SHA_DATE_DATE_S (0)
#define SHA_DATE_DATE_V(v) (((v) << 0) & 0x3FFFFFFFU)

// Sha H memory which contains intermediate hash or finial hash.
#define SHA_H_MEM_REG ((volatile uint32_t *)(SHA_BASE + 0x40))

// Sha M memory which contains message.
#define SHA_M_MEM_REG ((volatile uint32_t *)(SHA_BASE + 0x80))

#endif // SHA_H
