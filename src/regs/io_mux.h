/*
 * IO_MUX.h
 * Input/Output Multiplexer
 * Base Address: 0x60090000
 */

#ifndef IO_MUX_H
#define IO_MUX_H

#include <stdint.h>

#define IO_MUX_BASE 0x60090000

// Clock Output Configuration Register
#define IO_MUX_PIN_CTRL_REG ((volatile uint32_t *)(IO_MUX_BASE + 0x0))
#define IO_MUX_PIN_CTRL_CLK_OUT1_M (0x0000001FU)
#define IO_MUX_PIN_CTRL_CLK_OUT1_S (0)
#define IO_MUX_PIN_CTRL_CLK_OUT1_V(v) (((v) << 0) & 0x0000001FU)
#define IO_MUX_PIN_CTRL_CLK_OUT2_M (0x000003E0U)
#define IO_MUX_PIN_CTRL_CLK_OUT2_S (5)
#define IO_MUX_PIN_CTRL_CLK_OUT2_V(v) (((v) << 5) & 0x000003E0U)
#define IO_MUX_PIN_CTRL_CLK_OUT3_M (0x00007C00U)
#define IO_MUX_PIN_CTRL_CLK_OUT3_S (10)
#define IO_MUX_PIN_CTRL_CLK_OUT3_V(v) (((v) << 10) & 0x00007C00U)

// IO MUX Configure Register for pad XTAL_32K_P
#define IO_MUX_GPIO_REG ((volatile uint32_t *)(IO_MUX_BASE + 0x4))
#define IO_MUX_GPIO_MCU_OE_M (0x00000001U)
#define IO_MUX_GPIO_MCU_OE_S (0)
#define IO_MUX_GPIO_MCU_OE_V(v) (((v) << 0) & 0x00000001U)
#define IO_MUX_GPIO_SLP_SEL_M (0x00000002U)
#define IO_MUX_GPIO_SLP_SEL_S (1)
#define IO_MUX_GPIO_SLP_SEL_V(v) (((v) << 1) & 0x00000002U)
#define IO_MUX_GPIO_MCU_WPD_M (0x00000004U)
#define IO_MUX_GPIO_MCU_WPD_S (2)
#define IO_MUX_GPIO_MCU_WPD_V(v) (((v) << 2) & 0x00000004U)
#define IO_MUX_GPIO_MCU_WPU_M (0x00000008U)
#define IO_MUX_GPIO_MCU_WPU_S (3)
#define IO_MUX_GPIO_MCU_WPU_V(v) (((v) << 3) & 0x00000008U)
#define IO_MUX_GPIO_MCU_IE_M (0x00000010U)
#define IO_MUX_GPIO_MCU_IE_S (4)
#define IO_MUX_GPIO_MCU_IE_V(v) (((v) << 4) & 0x00000010U)
#define IO_MUX_GPIO_MCU_DRV_M (0x00000060U)
#define IO_MUX_GPIO_MCU_DRV_S (5)
#define IO_MUX_GPIO_MCU_DRV_V(v) (((v) << 5) & 0x00000060U)
#define IO_MUX_GPIO_FUN_WPD_M (0x00000080U)
#define IO_MUX_GPIO_FUN_WPD_S (7)
#define IO_MUX_GPIO_FUN_WPD_V(v) (((v) << 7) & 0x00000080U)
#define IO_MUX_GPIO_FUN_WPU_M (0x00000100U)
#define IO_MUX_GPIO_FUN_WPU_S (8)
#define IO_MUX_GPIO_FUN_WPU_V(v) (((v) << 8) & 0x00000100U)
#define IO_MUX_GPIO_FUN_IE_M (0x00000200U)
#define IO_MUX_GPIO_FUN_IE_S (9)
#define IO_MUX_GPIO_FUN_IE_V(v) (((v) << 9) & 0x00000200U)
#define IO_MUX_GPIO_FUN_DRV_M (0x00000C00U)
#define IO_MUX_GPIO_FUN_DRV_S (10)
#define IO_MUX_GPIO_FUN_DRV_V(v) (((v) << 10) & 0x00000C00U)
#define IO_MUX_GPIO_MCU_SEL_M (0x00007000U)
#define IO_MUX_GPIO_MCU_SEL_S (12)
#define IO_MUX_GPIO_MCU_SEL_V(v) (((v) << 12) & 0x00007000U)
#define IO_MUX_GPIO_FILTER_EN_M (0x00008000U)
#define IO_MUX_GPIO_FILTER_EN_S (15)
#define IO_MUX_GPIO_FILTER_EN_V(v) (((v) << 15) & 0x00008000U)

// GPIO MATRIX Configure Register for modem diag
#define IO_MUX_MODEM_DIAG_EN_REG ((volatile uint32_t *)(IO_MUX_BASE + 0xBC))
#define IO_MUX_MODEM_DIAG_EN_MODEM_DIAG_EN_M (0xFFFFFFFFU)
#define IO_MUX_MODEM_DIAG_EN_MODEM_DIAG_EN_S (0)
#define IO_MUX_MODEM_DIAG_EN_MODEM_DIAG_EN_V(v) (((v) << 0) & 0xFFFFFFFFU)

// IO MUX Version Control Register
#define IO_MUX_DATE_REG ((volatile uint32_t *)(IO_MUX_BASE + 0xFC))
#define IO_MUX_DATE_REG_DATE_M (0x0FFFFFFFU)
#define IO_MUX_DATE_REG_DATE_S (0)
#define IO_MUX_DATE_REG_DATE_V(v) (((v) << 0) & 0x0FFFFFFFU)

#endif // IO_MUX_H
