/*
 * GPIO.h
 * General Purpose Input/Output
 * Base Address: 0x60091000
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

#define GPIO_BASE 0x60091000

// GPIO bit select register
#define GPIO_BT_SELECT_REG ((volatile uint32_t *)(GPIO_BASE + 0x0))
#define GPIO_BT_SELECT_BT_SEL_M (0xFFFFFFFFU)
#define GPIO_BT_SELECT_BT_SEL_S (0)
#define GPIO_BT_SELECT_BT_SEL_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO output register for GPIO0-31
#define GPIO_OUT_REG ((volatile uint32_t *)(GPIO_BASE + 0x4))
#define GPIO_OUT_DATA_ORIG_M (0xFFFFFFFFU)
#define GPIO_OUT_DATA_ORIG_S (0)
#define GPIO_OUT_DATA_ORIG_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO output set register for GPIO0-31
#define GPIO_OUT_W1TS_REG ((volatile uint32_t *)(GPIO_BASE + 0x8))
#define GPIO_OUT_W1TS_OUT_W1TS_M (0xFFFFFFFFU)
#define GPIO_OUT_W1TS_OUT_W1TS_S (0)
#define GPIO_OUT_W1TS_OUT_W1TS_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO output clear register for GPIO0-31
#define GPIO_OUT_W1TC_REG ((volatile uint32_t *)(GPIO_BASE + 0xC))
#define GPIO_OUT_W1TC_OUT_W1TC_M (0xFFFFFFFFU)
#define GPIO_OUT_W1TC_OUT_W1TC_S (0)
#define GPIO_OUT_W1TC_OUT_W1TC_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO output register for GPIO32-34
#define GPIO_OUT1_REG ((volatile uint32_t *)(GPIO_BASE + 0x10))
#define GPIO_OUT1_DATA_ORIG_M (0x00000007U)
#define GPIO_OUT1_DATA_ORIG_S (0)
#define GPIO_OUT1_DATA_ORIG_V(v) (((v) << 0) & 0x00000007U)

// GPIO output set register for GPIO32-34
#define GPIO_OUT1_W1TS_REG ((volatile uint32_t *)(GPIO_BASE + 0x14))
#define GPIO_OUT1_W1TS_OUT1_W1TS_M (0x00000007U)
#define GPIO_OUT1_W1TS_OUT1_W1TS_S (0)
#define GPIO_OUT1_W1TS_OUT1_W1TS_V(v) (((v) << 0) & 0x00000007U)

// GPIO output clear register for GPIO32-34
#define GPIO_OUT1_W1TC_REG ((volatile uint32_t *)(GPIO_BASE + 0x18))
#define GPIO_OUT1_W1TC_OUT1_W1TC_M (0x00000007U)
#define GPIO_OUT1_W1TC_OUT1_W1TC_S (0)
#define GPIO_OUT1_W1TC_OUT1_W1TC_V(v) (((v) << 0) & 0x00000007U)

// GPIO sdio select register
#define GPIO_SDIO_SELECT_REG ((volatile uint32_t *)(GPIO_BASE + 0x1C))
#define GPIO_SDIO_SELECT_SDIO_SEL_M (0x000000FFU)
#define GPIO_SDIO_SELECT_SDIO_SEL_S (0)
#define GPIO_SDIO_SELECT_SDIO_SEL_V(v) (((v) << 0) & 0x000000FFU)

// GPIO output enable register for GPIO0-31
#define GPIO_ENABLE_REG ((volatile uint32_t *)(GPIO_BASE + 0x20))
#define GPIO_ENABLE_DATA_M (0xFFFFFFFFU)
#define GPIO_ENABLE_DATA_S (0)
#define GPIO_ENABLE_DATA_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO output enable set register for GPIO0-31
#define GPIO_ENABLE_W1TS_REG ((volatile uint32_t *)(GPIO_BASE + 0x24))
#define GPIO_ENABLE_W1TS_ENABLE_W1TS_M (0xFFFFFFFFU)
#define GPIO_ENABLE_W1TS_ENABLE_W1TS_S (0)
#define GPIO_ENABLE_W1TS_ENABLE_W1TS_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO output enable clear register for GPIO0-31
#define GPIO_ENABLE_W1TC_REG ((volatile uint32_t *)(GPIO_BASE + 0x28))
#define GPIO_ENABLE_W1TC_ENABLE_W1TC_M (0xFFFFFFFFU)
#define GPIO_ENABLE_W1TC_ENABLE_W1TC_S (0)
#define GPIO_ENABLE_W1TC_ENABLE_W1TC_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO output enable register for GPIO32-34
#define GPIO_ENABLE1_REG ((volatile uint32_t *)(GPIO_BASE + 0x2C))
#define GPIO_ENABLE1_DATA_M (0x00000007U)
#define GPIO_ENABLE1_DATA_S (0)
#define GPIO_ENABLE1_DATA_V(v) (((v) << 0) & 0x00000007U)

// GPIO output enable set register for GPIO32-34
#define GPIO_ENABLE1_W1TS_REG ((volatile uint32_t *)(GPIO_BASE + 0x30))
#define GPIO_ENABLE1_W1TS_ENABLE1_W1TS_M (0x00000007U)
#define GPIO_ENABLE1_W1TS_ENABLE1_W1TS_S (0)
#define GPIO_ENABLE1_W1TS_ENABLE1_W1TS_V(v) (((v) << 0) & 0x00000007U)

// GPIO output enable clear register for GPIO32-34
#define GPIO_ENABLE1_W1TC_REG ((volatile uint32_t *)(GPIO_BASE + 0x34))
#define GPIO_ENABLE1_W1TC_ENABLE1_W1TC_M (0x00000007U)
#define GPIO_ENABLE1_W1TC_ENABLE1_W1TC_S (0)
#define GPIO_ENABLE1_W1TC_ENABLE1_W1TC_V(v) (((v) << 0) & 0x00000007U)

// pad strapping register
#define GPIO_STRAP_REG ((volatile uint32_t *)(GPIO_BASE + 0x38))
#define GPIO_STRAP_STRAPPING_M (0x0000FFFFU)
#define GPIO_STRAP_STRAPPING_S (0)
#define GPIO_STRAP_STRAPPING_V(v) (((v) << 0) & 0x0000FFFFU)

// GPIO input register for GPIO0-31
#define GPIO_IN_REG ((volatile uint32_t *)(GPIO_BASE + 0x3C))
#define GPIO_IN_DATA_NEXT_M (0xFFFFFFFFU)
#define GPIO_IN_DATA_NEXT_S (0)
#define GPIO_IN_DATA_NEXT_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO input register for GPIO32-34
#define GPIO_IN1_REG ((volatile uint32_t *)(GPIO_BASE + 0x40))
#define GPIO_IN1_DATA_NEXT_M (0x00000007U)
#define GPIO_IN1_DATA_NEXT_S (0)
#define GPIO_IN1_DATA_NEXT_V(v) (((v) << 0) & 0x00000007U)

// GPIO interrupt status register for GPIO0-31
#define GPIO_STATUS_REG ((volatile uint32_t *)(GPIO_BASE + 0x44))
#define GPIO_STATUS_INTERRUPT_M (0xFFFFFFFFU)
#define GPIO_STATUS_INTERRUPT_S (0)
#define GPIO_STATUS_INTERRUPT_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO interrupt status set register for GPIO0-31
#define GPIO_STATUS_W1TS_REG ((volatile uint32_t *)(GPIO_BASE + 0x48))
#define GPIO_STATUS_W1TS_STATUS_W1TS_M (0xFFFFFFFFU)
#define GPIO_STATUS_W1TS_STATUS_W1TS_S (0)
#define GPIO_STATUS_W1TS_STATUS_W1TS_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO interrupt status clear register for GPIO0-31
#define GPIO_STATUS_W1TC_REG ((volatile uint32_t *)(GPIO_BASE + 0x4C))
#define GPIO_STATUS_W1TC_STATUS_W1TC_M (0xFFFFFFFFU)
#define GPIO_STATUS_W1TC_STATUS_W1TC_S (0)
#define GPIO_STATUS_W1TC_STATUS_W1TC_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO interrupt status register for GPIO32-34
#define GPIO_STATUS1_REG ((volatile uint32_t *)(GPIO_BASE + 0x50))
#define GPIO_STATUS1_INTERRUPT_M (0x00000007U)
#define GPIO_STATUS1_INTERRUPT_S (0)
#define GPIO_STATUS1_INTERRUPT_V(v) (((v) << 0) & 0x00000007U)

// GPIO interrupt status set register for GPIO32-34
#define GPIO_STATUS1_W1TS_REG ((volatile uint32_t *)(GPIO_BASE + 0x54))
#define GPIO_STATUS1_W1TS_STATUS1_W1TS_M (0x00000007U)
#define GPIO_STATUS1_W1TS_STATUS1_W1TS_S (0)
#define GPIO_STATUS1_W1TS_STATUS1_W1TS_V(v) (((v) << 0) & 0x00000007U)

// GPIO interrupt status clear register for GPIO32-34
#define GPIO_STATUS1_W1TC_REG ((volatile uint32_t *)(GPIO_BASE + 0x58))
#define GPIO_STATUS1_W1TC_STATUS1_W1TC_M (0x00000007U)
#define GPIO_STATUS1_W1TC_STATUS1_W1TC_S (0)
#define GPIO_STATUS1_W1TC_STATUS1_W1TC_V(v) (((v) << 0) & 0x00000007U)

// GPIO PRO_CPU interrupt status register for GPIO0-31
#define GPIO_PCPU_INT_REG ((volatile uint32_t *)(GPIO_BASE + 0x5C))
#define GPIO_PCPU_INT_PROCPU_INT_M (0xFFFFFFFFU)
#define GPIO_PCPU_INT_PROCPU_INT_S (0)
#define GPIO_PCPU_INT_PROCPU_INT_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO PRO_CPU(not shielded) interrupt status register for GPIO0-31
#define GPIO_PCPU_NMI_INT_REG ((volatile uint32_t *)(GPIO_BASE + 0x60))
#define GPIO_PCPU_NMI_INT_PROCPU_NMI_INT_M (0xFFFFFFFFU)
#define GPIO_PCPU_NMI_INT_PROCPU_NMI_INT_S (0)
#define GPIO_PCPU_NMI_INT_PROCPU_NMI_INT_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO CPUSDIO interrupt status register for GPIO0-31
#define GPIO_CPUSDIO_INT_REG ((volatile uint32_t *)(GPIO_BASE + 0x64))
#define GPIO_CPUSDIO_INT_SDIO_INT_M (0xFFFFFFFFU)
#define GPIO_CPUSDIO_INT_SDIO_INT_S (0)
#define GPIO_CPUSDIO_INT_SDIO_INT_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO PRO_CPU interrupt status register for GPIO32-34
#define GPIO_PCPU_INT1_REG ((volatile uint32_t *)(GPIO_BASE + 0x68))
#define GPIO_PCPU_INT1_PROCPU_INT1_M (0x00000007U)
#define GPIO_PCPU_INT1_PROCPU_INT1_S (0)
#define GPIO_PCPU_INT1_PROCPU_INT1_V(v) (((v) << 0) & 0x00000007U)

// GPIO PRO_CPU(not shielded) interrupt status register for GPIO32-34
#define GPIO_PCPU_NMI_INT1_REG ((volatile uint32_t *)(GPIO_BASE + 0x6C))
#define GPIO_PCPU_NMI_INT1_PROCPU_NMI_INT1_M (0x00000007U)
#define GPIO_PCPU_NMI_INT1_PROCPU_NMI_INT1_S (0)
#define GPIO_PCPU_NMI_INT1_PROCPU_NMI_INT1_V(v) (((v) << 0) & 0x00000007U)

// GPIO CPUSDIO interrupt status register for GPIO32-34
#define GPIO_CPUSDIO_INT1_REG ((volatile uint32_t *)(GPIO_BASE + 0x70))
#define GPIO_CPUSDIO_INT1_SDIO_INT1_M (0x00000007U)
#define GPIO_CPUSDIO_INT1_SDIO_INT1_S (0)
#define GPIO_CPUSDIO_INT1_SDIO_INT1_V(v) (((v) << 0) & 0x00000007U)

// GPIO pin configuration register
#define GPIO_PIN_REG ((volatile uint32_t *)(GPIO_BASE + 0x74))
#define GPIO_PIN_SYNC2_BYPASS_M (0x00000003U)
#define GPIO_PIN_SYNC2_BYPASS_S (0)
#define GPIO_PIN_SYNC2_BYPASS_V(v) (((v) << 0) & 0x00000003U)
#define GPIO_PIN_PAD_DRIVER_M (0x00000004U)
#define GPIO_PIN_PAD_DRIVER_S (2)
#define GPIO_PIN_PAD_DRIVER_V(v) (((v) << 2) & 0x00000004U)
#define GPIO_PIN_SYNC1_BYPASS_M (0x00000018U)
#define GPIO_PIN_SYNC1_BYPASS_S (3)
#define GPIO_PIN_SYNC1_BYPASS_V(v) (((v) << 3) & 0x00000018U)
#define GPIO_PIN_INT_TYPE_M (0x00000380U)
#define GPIO_PIN_INT_TYPE_S (7)
#define GPIO_PIN_INT_TYPE_V(v) (((v) << 7) & 0x00000380U)
#define GPIO_PIN_WAKEUP_ENABLE_M (0x00000400U)
#define GPIO_PIN_WAKEUP_ENABLE_S (10)
#define GPIO_PIN_WAKEUP_ENABLE_V(v) (((v) << 10) & 0x00000400U)
#define GPIO_PIN_CONFIG_M (0x00001800U)
#define GPIO_PIN_CONFIG_S (11)
#define GPIO_PIN_CONFIG_V(v) (((v) << 11) & 0x00001800U)
#define GPIO_PIN_INT_ENA_M (0x0003E000U)
#define GPIO_PIN_INT_ENA_S (13)
#define GPIO_PIN_INT_ENA_V(v) (((v) << 13) & 0x0003E000U)

// GPIO interrupt source register for GPIO0-31
#define GPIO_STATUS_NEXT_REG ((volatile uint32_t *)(GPIO_BASE + 0x14C))
#define GPIO_STATUS_NEXT_STATUS_INTERRUPT_NEXT_M (0xFFFFFFFFU)
#define GPIO_STATUS_NEXT_STATUS_INTERRUPT_NEXT_S (0)
#define GPIO_STATUS_NEXT_STATUS_INTERRUPT_NEXT_V(v) (((v) << 0) & 0xFFFFFFFFU)

// GPIO interrupt source register for GPIO32-34
#define GPIO_STATUS_NEXT1_REG ((volatile uint32_t *)(GPIO_BASE + 0x150))
#define GPIO_STATUS_NEXT1_STATUS_INTERRUPT_NEXT1_M (0x00000007U)
#define GPIO_STATUS_NEXT1_STATUS_INTERRUPT_NEXT1_S (0)
#define GPIO_STATUS_NEXT1_STATUS_INTERRUPT_NEXT1_V(v) (((v) << 0) & 0x00000007U)

// GPIO input function configuration register
#define GPIO_FUNC_IN_SEL_CFG_REG ((volatile uint32_t *)(GPIO_BASE + 0x154))
#define GPIO_FUNC_IN_SEL_CFG_IN_SEL_M (0x0000003FU)
#define GPIO_FUNC_IN_SEL_CFG_IN_SEL_S (0)
#define GPIO_FUNC_IN_SEL_CFG_IN_SEL_V(v) (((v) << 0) & 0x0000003FU)
#define GPIO_FUNC_IN_SEL_CFG_IN_INV_SEL_M (0x00000040U)
#define GPIO_FUNC_IN_SEL_CFG_IN_INV_SEL_S (6)
#define GPIO_FUNC_IN_SEL_CFG_IN_INV_SEL_V(v) (((v) << 6) & 0x00000040U)
#define GPIO_FUNC_IN_SEL_CFG_SEL_M (0x00000080U)
#define GPIO_FUNC_IN_SEL_CFG_SEL_S (7)
#define GPIO_FUNC_IN_SEL_CFG_SEL_V(v) (((v) << 7) & 0x00000080U)

// GPIO output function select register
#define GPIO_FUNC_OUT_SEL_CFG_REG ((volatile uint32_t *)(GPIO_BASE + 0x554))
#define GPIO_FUNC_OUT_SEL_CFG_OUT_SEL_M (0x000000FFU)
#define GPIO_FUNC_OUT_SEL_CFG_OUT_SEL_S (0)
#define GPIO_FUNC_OUT_SEL_CFG_OUT_SEL_V(v) (((v) << 0) & 0x000000FFU)
#define GPIO_FUNC_OUT_SEL_CFG_INV_SEL_M (0x00000100U)
#define GPIO_FUNC_OUT_SEL_CFG_INV_SEL_S (8)
#define GPIO_FUNC_OUT_SEL_CFG_INV_SEL_V(v) (((v) << 8) & 0x00000100U)
#define GPIO_FUNC_OUT_SEL_CFG_OEN_SEL_M (0x00000200U)
#define GPIO_FUNC_OUT_SEL_CFG_OEN_SEL_S (9)
#define GPIO_FUNC_OUT_SEL_CFG_OEN_SEL_V(v) (((v) << 9) & 0x00000200U)
#define GPIO_FUNC_OUT_SEL_CFG_OEN_INV_SEL_M (0x00000400U)
#define GPIO_FUNC_OUT_SEL_CFG_OEN_INV_SEL_S (10)
#define GPIO_FUNC_OUT_SEL_CFG_OEN_INV_SEL_V(v) (((v) << 10) & 0x00000400U)

// GPIO clock gate register
#define GPIO_CLOCK_GATE_REG ((volatile uint32_t *)(GPIO_BASE + 0x62C))
#define GPIO_CLOCK_GATE_CLK_EN_M (0x00000001U)
#define GPIO_CLOCK_GATE_CLK_EN_S (0)
#define GPIO_CLOCK_GATE_CLK_EN_V(v) (((v) << 0) & 0x00000001U)

// GPIO version register
#define GPIO_DATE_REG ((volatile uint32_t *)(GPIO_BASE + 0x6FC))
#define GPIO_DATE_DATE_M (0x0FFFFFFFU)
#define GPIO_DATE_DATE_S (0)
#define GPIO_DATE_DATE_V(v) (((v) << 0) & 0x0FFFFFFFU)

#endif // GPIO_H
