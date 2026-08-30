/*
 * AES.h
 * AES (Advanced Encryption Standard) Accelerator
 * Base Address: 0x60088000
 */

#ifndef AES_H
#define AES_H

#include <stdint.h>

#define AES_BASE 0x60088000

// Key material key_0 configure register
#define AES_KEY_0_REG ((volatile uint32_t *)(AES_BASE + 0x0))
#define AES_KEY_0_KEY_0_M (0xFFFFFFFFU)
#define AES_KEY_0_KEY_0_S (0)
#define AES_KEY_0_KEY_0_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Key material key_1 configure register
#define AES_KEY_1_REG ((volatile uint32_t *)(AES_BASE + 0x4))
#define AES_KEY_1_KEY_1_M (0xFFFFFFFFU)
#define AES_KEY_1_KEY_1_S (0)
#define AES_KEY_1_KEY_1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Key material key_2 configure register
#define AES_KEY_2_REG ((volatile uint32_t *)(AES_BASE + 0x8))
#define AES_KEY_2_KEY_2_M (0xFFFFFFFFU)
#define AES_KEY_2_KEY_2_S (0)
#define AES_KEY_2_KEY_2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Key material key_3 configure register
#define AES_KEY_3_REG ((volatile uint32_t *)(AES_BASE + 0xC))
#define AES_KEY_3_KEY_3_M (0xFFFFFFFFU)
#define AES_KEY_3_KEY_3_S (0)
#define AES_KEY_3_KEY_3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Key material key_4 configure register
#define AES_KEY_4_REG ((volatile uint32_t *)(AES_BASE + 0x10))
#define AES_KEY_4_KEY_4_M (0xFFFFFFFFU)
#define AES_KEY_4_KEY_4_S (0)
#define AES_KEY_4_KEY_4_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Key material key_5 configure register
#define AES_KEY_5_REG ((volatile uint32_t *)(AES_BASE + 0x14))
#define AES_KEY_5_KEY_5_M (0xFFFFFFFFU)
#define AES_KEY_5_KEY_5_S (0)
#define AES_KEY_5_KEY_5_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Key material key_6 configure register
#define AES_KEY_6_REG ((volatile uint32_t *)(AES_BASE + 0x18))
#define AES_KEY_6_KEY_6_M (0xFFFFFFFFU)
#define AES_KEY_6_KEY_6_S (0)
#define AES_KEY_6_KEY_6_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Key material key_7 configure register
#define AES_KEY_7_REG ((volatile uint32_t *)(AES_BASE + 0x1C))
#define AES_KEY_7_KEY_7_M (0xFFFFFFFFU)
#define AES_KEY_7_KEY_7_S (0)
#define AES_KEY_7_KEY_7_V(v) (((v) << 0) & 0xFFFFFFFFU)

// source text material text_in_0 configure register
#define AES_TEXT_IN_0_REG ((volatile uint32_t *)(AES_BASE + 0x20))
#define AES_TEXT_IN_0_TEXT_IN_0_M (0xFFFFFFFFU)
#define AES_TEXT_IN_0_TEXT_IN_0_S (0)
#define AES_TEXT_IN_0_TEXT_IN_0_V(v) (((v) << 0) & 0xFFFFFFFFU)

// source text material text_in_1 configure register
#define AES_TEXT_IN_1_REG ((volatile uint32_t *)(AES_BASE + 0x24))
#define AES_TEXT_IN_1_TEXT_IN_1_M (0xFFFFFFFFU)
#define AES_TEXT_IN_1_TEXT_IN_1_S (0)
#define AES_TEXT_IN_1_TEXT_IN_1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// source text material text_in_2 configure register
#define AES_TEXT_IN_2_REG ((volatile uint32_t *)(AES_BASE + 0x28))
#define AES_TEXT_IN_2_TEXT_IN_2_M (0xFFFFFFFFU)
#define AES_TEXT_IN_2_TEXT_IN_2_S (0)
#define AES_TEXT_IN_2_TEXT_IN_2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// source text material text_in_3 configure register
#define AES_TEXT_IN_3_REG ((volatile uint32_t *)(AES_BASE + 0x2C))
#define AES_TEXT_IN_3_TEXT_IN_3_M (0xFFFFFFFFU)
#define AES_TEXT_IN_3_TEXT_IN_3_S (0)
#define AES_TEXT_IN_3_TEXT_IN_3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// result text material text_out_0 configure register
#define AES_TEXT_OUT_0_REG ((volatile uint32_t *)(AES_BASE + 0x30))
#define AES_TEXT_OUT_0_TEXT_OUT_0_M (0xFFFFFFFFU)
#define AES_TEXT_OUT_0_TEXT_OUT_0_S (0)
#define AES_TEXT_OUT_0_TEXT_OUT_0_V(v) (((v) << 0) & 0xFFFFFFFFU)

// result text material text_out_1 configure register
#define AES_TEXT_OUT_1_REG ((volatile uint32_t *)(AES_BASE + 0x34))
#define AES_TEXT_OUT_1_TEXT_OUT_1_M (0xFFFFFFFFU)
#define AES_TEXT_OUT_1_TEXT_OUT_1_S (0)
#define AES_TEXT_OUT_1_TEXT_OUT_1_V(v) (((v) << 0) & 0xFFFFFFFFU)

// result text material text_out_2 configure register
#define AES_TEXT_OUT_2_REG ((volatile uint32_t *)(AES_BASE + 0x38))
#define AES_TEXT_OUT_2_TEXT_OUT_2_M (0xFFFFFFFFU)
#define AES_TEXT_OUT_2_TEXT_OUT_2_S (0)
#define AES_TEXT_OUT_2_TEXT_OUT_2_V(v) (((v) << 0) & 0xFFFFFFFFU)

// result text material text_out_3 configure register
#define AES_TEXT_OUT_3_REG ((volatile uint32_t *)(AES_BASE + 0x3C))
#define AES_TEXT_OUT_3_TEXT_OUT_3_M (0xFFFFFFFFU)
#define AES_TEXT_OUT_3_TEXT_OUT_3_S (0)
#define AES_TEXT_OUT_3_TEXT_OUT_3_V(v) (((v) << 0) & 0xFFFFFFFFU)

// AES Mode register
#define AES_MODE_REG ((volatile uint32_t *)(AES_BASE + 0x40))
#define AES_MODE_MODE_M (0x00000007U)
#define AES_MODE_MODE_S (0)
#define AES_MODE_MODE_V(v) (((v) << 0) & 0x00000007U)

// AES Endian configure register
#define AES_ENDIAN_REG ((volatile uint32_t *)(AES_BASE + 0x44))
#define AES_ENDIAN_ENDIAN_M (0x0000003FU)
#define AES_ENDIAN_ENDIAN_S (0)
#define AES_ENDIAN_ENDIAN_V(v) (((v) << 0) & 0x0000003FU)

// AES trigger register
#define AES_TRIGGER_REG ((volatile uint32_t *)(AES_BASE + 0x48))
#define AES_TRIGGER_TRIGGER_M (0x00000001U)
#define AES_TRIGGER_TRIGGER_S (0)
#define AES_TRIGGER_TRIGGER_V(v) (((v) << 0) & 0x00000001U)

// AES state register
#define AES_STATE_REG ((volatile uint32_t *)(AES_BASE + 0x4C))
#define AES_STATE_STATE_M (0x00000003U)
#define AES_STATE_STATE_S (0)
#define AES_STATE_STATE_V(v) (((v) << 0) & 0x00000003U)

// The memory that stores initialization vector
#define AES_IV_MEM_REG ((volatile uint32_t *)(AES_BASE + 0x50))

// The memory that stores GCM hash subkey
#define AES_H_MEM_REG ((volatile uint32_t *)(AES_BASE + 0x60))

// The memory that stores J0
#define AES_J0_MEM_REG ((volatile uint32_t *)(AES_BASE + 0x70))

// The memory that stores T0
#define AES_T0_MEM_REG ((volatile uint32_t *)(AES_BASE + 0x80))

// DMA-AES working mode register
#define AES_DMA_ENABLE_REG ((volatile uint32_t *)(AES_BASE + 0x90))
#define AES_DMA_ENABLE_DMA_ENABLE_M (0x00000001U)
#define AES_DMA_ENABLE_DMA_ENABLE_S (0)
#define AES_DMA_ENABLE_DMA_ENABLE_V(v) (((v) << 0) & 0x00000001U)

// AES cipher block mode register
#define AES_BLOCK_MODE_REG ((volatile uint32_t *)(AES_BASE + 0x94))
#define AES_BLOCK_MODE_BLOCK_MODE_M (0x00000007U)
#define AES_BLOCK_MODE_BLOCK_MODE_S (0)
#define AES_BLOCK_MODE_BLOCK_MODE_V(v) (((v) << 0) & 0x00000007U)

// AES block number register
#define AES_BLOCK_NUM_REG ((volatile uint32_t *)(AES_BASE + 0x98))
#define AES_BLOCK_NUM_BLOCK_NUM_M (0xFFFFFFFFU)
#define AES_BLOCK_NUM_BLOCK_NUM_S (0)
#define AES_BLOCK_NUM_BLOCK_NUM_V(v) (((v) << 0) & 0xFFFFFFFFU)

// Standard incrementing function configure register
#define AES_INC_SEL_REG ((volatile uint32_t *)(AES_BASE + 0x9C))
#define AES_INC_SEL_INC_SEL_M (0x00000001U)
#define AES_INC_SEL_INC_SEL_S (0)
#define AES_INC_SEL_INC_SEL_V(v) (((v) << 0) & 0x00000001U)

// Additional Authential Data block number register
#define AES_AAD_BLOCK_NUM_REG ((volatile uint32_t *)(AES_BASE + 0xA0))
#define AES_AAD_BLOCK_NUM_AAD_BLOCK_NUM_M (0xFFFFFFFFU)
#define AES_AAD_BLOCK_NUM_AAD_BLOCK_NUM_S (0)
#define AES_AAD_BLOCK_NUM_AAD_BLOCK_NUM_V(v) (((v) << 0) & 0xFFFFFFFFU)

// AES remainder bit number register
#define AES_REMAINDER_BIT_NUM_REG ((volatile uint32_t *)(AES_BASE + 0xA4))
#define AES_REMAINDER_BIT_NUM_REMAINDER_BIT_NUM_M (0x0000007FU)
#define AES_REMAINDER_BIT_NUM_REMAINDER_BIT_NUM_S (0)
#define AES_REMAINDER_BIT_NUM_REMAINDER_BIT_NUM_V(v) (((v) << 0) & 0x0000007FU)

// AES continue register
#define AES_CONTINUE_REG ((volatile uint32_t *)(AES_BASE + 0xA8))
#define AES_CONTINUE_CONTINUE_M (0x00000001U)
#define AES_CONTINUE_CONTINUE_S (0)
#define AES_CONTINUE_CONTINUE_V(v) (((v) << 0) & 0x00000001U)

// AES Interrupt clear register
#define AES_INT_CLEAR_REG ((volatile uint32_t *)(AES_BASE + 0xAC))
#define AES_INT_CLEAR_INT_CLEAR_M (0x00000001U)
#define AES_INT_CLEAR_INT_CLEAR_S (0)
#define AES_INT_CLEAR_INT_CLEAR_V(v) (((v) << 0) & 0x00000001U)

// AES Interrupt enable register
#define AES_INT_ENA_REG ((volatile uint32_t *)(AES_BASE + 0xB0))
#define AES_INT_ENA_INT_ENA_M (0x00000001U)
#define AES_INT_ENA_INT_ENA_S (0)
#define AES_INT_ENA_INT_ENA_V(v) (((v) << 0) & 0x00000001U)

// AES version control register
#define AES_DATE_REG ((volatile uint32_t *)(AES_BASE + 0xB4))
#define AES_DATE_DATE_M (0x3FFFFFFFU)
#define AES_DATE_DATE_S (0)
#define AES_DATE_DATE_V(v) (((v) << 0) & 0x3FFFFFFFU)

// AES-DMA exit config
#define AES_DMA_EXIT_REG ((volatile uint32_t *)(AES_BASE + 0xB8))
#define AES_DMA_EXIT_DMA_EXIT_M (0x00000001U)
#define AES_DMA_EXIT_DMA_EXIT_S (0)
#define AES_DMA_EXIT_DMA_EXIT_V(v) (((v) << 0) & 0x00000001U)

#endif // AES_H
