/*
 * src/gpio.h
 *
 * ESP32-C6 GPIO Matrix & IO_MUX Multi-Function Pin Routing Driver
 * TRM Chapter 7 (IO MUX and GPIO Matrix, §7.1-§7.9)
 *
 * Provides pin direction configuration, pull-up/pull-down resistor control,
 * drive strength selection, multi-function IO_MUX routing, atomic bit-set/clear
 * output control (W1TS/W1TC), and non-blocking input level polling.
 */

#ifndef IRON_V_GPIO_H
#define IRON_V_GPIO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================= */
/* Peripheral Base Addresses (TRM Table 3-3 Peripheral Memory Cartography)   */
/* ========================================================================= */
#define GPIO_BASE_ADDR                   0x60091000U
#define IO_MUX_BASE_ADDR                 0x60090000U
#define PCR_BASE_ADDR                    0x60096000U

/* ========================================================================= */
/* GPIO Pin Count & Geometry                                                 */
/* ========================================================================= */
#define GPIO_PIN_MIN                     0U
#define GPIO_PIN_MAX                     30U
#define GPIO_PIN_COUNT                   31U

/* ========================================================================= */
/* GPIO Register Offsets (TRM §7.9 Register Summary)                         */
/* ========================================================================= */
#define GPIO_BT_SELECT_OFFSET            0x000U
#define GPIO_OUT_OFFSET                  0x004U
#define GPIO_OUT_W1TS_OFFSET             0x008U
#define GPIO_OUT_W1TC_OFFSET             0x00CU
#define GPIO_OUT1_OFFSET                 0x010U
#define GPIO_OUT1_W1TS_OFFSET            0x014U
#define GPIO_OUT1_W1TC_OFFSET            0x018U
#define GPIO_ENABLE_OFFSET               0x020U
#define GPIO_ENABLE_W1TS_OFFSET          0x024U
#define GPIO_ENABLE_W1TC_OFFSET          0x028U
#define GPIO_ENABLE1_OFFSET              0x02CU
#define GPIO_ENABLE1_W1TS_OFFSET         0x030U
#define GPIO_ENABLE1_W1TC_OFFSET         0x034U
#define GPIO_STRAP_OFFSET                0x038U
#define GPIO_IN_OFFSET                   0x03CU
#define GPIO_IN1_OFFSET                  0x040U
#define GPIO_STATUS_OFFSET               0x044U
#define GPIO_STATUS_W1TS_OFFSET          0x048U
#define GPIO_STATUS_W1TC_OFFSET          0x04CU
#define GPIO_STATUS1_OFFSET              0x050U
#define GPIO_STATUS1_W1TS_OFFSET         0x054U
#define GPIO_STATUS1_W1TC_OFFSET         0x058U
#define GPIO_PIN0_CONF_OFFSET            0x074U
#define GPIO_FUNC_IN_SEL_OFFSET          0x154U
#define GPIO_FUNC_OUT_SEL_OFFSET         0x554U
#define GPIO_CLOCK_GATE_OFFSET           0x62CU
#define GPIO_DATE_OFFSET                 0x6FCU

/* ========================================================================= */
/* IO_MUX Register Offsets (TRM §7.9 IO_MUX Register Summary)                */
/* ========================================================================= */
#define IO_MUX_PIN_CTRL_OFFSET           0x000U
#define IO_MUX_GPIO0_OFFSET              0x004U
#define IO_MUX_MODEM_DIAG_OFFSET         0x0BCU
#define IO_MUX_DATE_OFFSET               0x0FCU

/* ========================================================================= */
/* PCR Peripheral Clock Control Offsets                                      */
/* ========================================================================= */
#define PCR_IOMUX_CONF_OFFSET            0x0E8U
#define PCR_IOMUX_CLK_CONF_OFFSET        0x0ECU

/* ========================================================================= */
/* Parameterized Register Accessor Macros (AGENTS.md Compliance)             */
/* ========================================================================= */
#undef GPIO_REG
#define GPIO_REG(offset)                 ((volatile uint32_t *)((uintptr_t)(GPIO_BASE_ADDR + (offset))))
#undef IO_MUX_REG
#define IO_MUX_REG(offset)               ((volatile uint32_t *)((uintptr_t)(IO_MUX_BASE_ADDR + (offset))))
#undef PCR_REG
#define PCR_REG(offset)                  ((volatile uint32_t *)((uintptr_t)(PCR_BASE_ADDR + (offset))))

#undef GPIO_OUT_REG
#define GPIO_OUT_REG                     GPIO_REG(GPIO_OUT_OFFSET)
#undef GPIO_OUT_W1TS_REG
#define GPIO_OUT_W1TS_REG                GPIO_REG(GPIO_OUT_W1TS_OFFSET)
#undef GPIO_OUT_W1TC_REG
#define GPIO_OUT_W1TC_REG                GPIO_REG(GPIO_OUT_W1TC_OFFSET)
#undef GPIO_ENABLE_REG
#define GPIO_ENABLE_REG                  GPIO_REG(GPIO_ENABLE_OFFSET)
#undef GPIO_ENABLE_W1TS_REG
#define GPIO_ENABLE_W1TS_REG             GPIO_REG(GPIO_ENABLE_W1TS_OFFSET)
#undef GPIO_ENABLE_W1TC_REG
#define GPIO_ENABLE_W1TC_REG             GPIO_REG(GPIO_ENABLE_W1TC_OFFSET)
#undef GPIO_IN_REG
#define GPIO_IN_REG                      GPIO_REG(GPIO_IN_OFFSET)
#undef GPIO_STATUS_REG
#define GPIO_STATUS_REG                  GPIO_REG(GPIO_STATUS_OFFSET)
#undef GPIO_STATUS_W1TS_REG
#define GPIO_STATUS_W1TS_REG             GPIO_REG(GPIO_STATUS_W1TS_OFFSET)
#undef GPIO_STATUS_W1TC_REG
#define GPIO_STATUS_W1TC_REG             GPIO_REG(GPIO_STATUS_W1TC_OFFSET)
#undef GPIO_CLOCK_GATE_REG
#define GPIO_CLOCK_GATE_REG              GPIO_REG(GPIO_CLOCK_GATE_OFFSET)

#undef IO_MUX_PIN_CTRL_REG
#define IO_MUX_PIN_CTRL_REG              IO_MUX_REG(IO_MUX_PIN_CTRL_OFFSET)
#undef IO_MUX_DATE_REG
#define IO_MUX_DATE_REG                  IO_MUX_REG(IO_MUX_DATE_OFFSET)

#undef PCR_IOMUX_CONF_REG
#define PCR_IOMUX_CONF_REG               PCR_REG(PCR_IOMUX_CONF_OFFSET)
#undef PCR_IOMUX_CLK_CONF_REG
#define PCR_IOMUX_CLK_CONF_REG           PCR_REG(PCR_IOMUX_CLK_CONF_OFFSET)

/* Per-Pin Indexed Accessor Macros */
#undef IO_MUX_GPIO_REG
#define IO_MUX_GPIO_N_REG(pin)           ((volatile uint32_t *)((uintptr_t)(IO_MUX_BASE_ADDR + IO_MUX_GPIO0_OFFSET + ((pin) * 4U))))
#define IO_MUX_GPIO_REG(pin)             IO_MUX_GPIO_N_REG(pin)
#define GPIO_PIN_CONF_REG(pin)           ((volatile uint32_t *)((uintptr_t)(GPIO_BASE_ADDR + GPIO_PIN0_CONF_OFFSET + ((pin) * 4U))))
#define GPIO_FUNC_OUT_SEL_REG(pin)       ((volatile uint32_t *)((uintptr_t)(GPIO_BASE_ADDR + GPIO_FUNC_OUT_SEL_OFFSET + ((pin) * 4U))))
#define GPIO_FUNC_IN_SEL_REG(sig)        ((volatile uint32_t *)((uintptr_t)(GPIO_BASE_ADDR + GPIO_FUNC_IN_SEL_OFFSET + ((sig) * 4U))))

/* ========================================================================= */
/* IO_MUX Register Bitfields (TRM §7.9.2 IO_MUX_GPIO_N_REG)                  */
/* ========================================================================= */
#define IO_MUX_MCU_OE_BIT                (1U << 0)
#define IO_MUX_SLP_SEL_BIT               (1U << 1)
#define IO_MUX_MCU_WPD_BIT               (1U << 2)
#define IO_MUX_MCU_WPU_BIT               (1U << 3)
#define IO_MUX_MCU_IE_BIT                (1U << 4)
#define IO_MUX_MCU_DRV_MASK              (0x3U << 5)
#define IO_MUX_MCU_DRV_SHIFT             5U
#define IO_MUX_FUN_WPD_BIT               (1U << 7)
#define IO_MUX_FUN_WPU_BIT               (1U << 8)
#define IO_MUX_FUN_IE_BIT                (1U << 9)
#define IO_MUX_FUN_DRV_MASK              (0x3U << 10)
#define IO_MUX_FUN_DRV_SHIFT             10U
#define IO_MUX_MCU_SEL_MASK              (0x7U << 12)
#define IO_MUX_MCU_SEL_SHIFT             12U
#define IO_MUX_FILTER_EN_BIT             (1U << 15)

#define IO_MUX_MCU_SEL_FUNC1_GPIO        1U
#define IO_MUX_MCU_SEL_FUNC0             0U

/* ========================================================================= */
/* GPIO Pin Configuration Register Bitfields (TRM §7.9.1 GPIO_PIN_N_REG)     */
/* ========================================================================= */
#define GPIO_PIN_PAD_DRIVER_BIT          (1U << 2)   /* 0: Push-pull, 1: Open-drain */
#define GPIO_PIN_INT_TYPE_MASK           (0x7U << 7)
#define GPIO_PIN_INT_TYPE_SHIFT          7U
#define GPIO_PIN_WAKEUP_ENABLE_BIT       (1U << 10)
#define GPIO_PIN_INT_ENA_MASK            (0x1FU << 13)
#define GPIO_PIN_INT_ENA_SHIFT           13U
#define GPIO_PIN_CPU_INT_ENA_BIT         (1U << 13)  /* PRO_CPU interrupt enable */

/* ========================================================================= */
/* GPIO Matrix Special Signal Indices                                        */
/* ========================================================================= */
#define GPIO_MATRIX_SIG_DIRECT_OUT       128U        /* Simple GPIO output driver */

/* ========================================================================= */
/* PCR & Clock Gate Control Bitfields                                        */
/* ========================================================================= */
#define GPIO_CLOCK_GATE_CLK_EN_BIT       (1U << 0)
#define PCR_IOMUX_CONF_CLK_EN_BIT        (1U << 0)
#define PCR_IOMUX_CONF_RST_EN_BIT        (1U << 1)

/* ========================================================================= */
/* Driver Status & Error Enumeration                                         */
/* ========================================================================= */
typedef enum {
    GPIO_OK                              = 0,
    GPIO_ERR_INVALID_PIN                 = -1,
    GPIO_ERR_INVALID_ARG                 = -2,
    GPIO_ERR_NOT_INITIALIZED             = -3
} gpio_status_t;

/* ========================================================================= */
/* Pin Direction Enumeration                                                 */
/* ========================================================================= */
typedef enum {
    GPIO_DIR_INPUT                       = 0,
    GPIO_DIR_OUTPUT                      = 1,
    GPIO_DIR_BIDIRECTIONAL               = 2
} gpio_direction_t;

/* ========================================================================= */
/* Internal Pull Resistor Enumeration                                        */
/* ========================================================================= */
typedef enum {
    GPIO_PULL_NONE                       = 0,
    GPIO_PULL_UP                         = 1,
    GPIO_PULL_DOWN                       = 2,
    GPIO_PULL_BOTH                       = 3
} gpio_pull_t;

/* ========================================================================= */
/* Pad Drive Strength Enumeration                                            */
/* ========================================================================= */
typedef enum {
    GPIO_DRIVE_0                         = 0, /* ~5 mA */
    GPIO_DRIVE_1                         = 1, /* ~10 mA */
    GPIO_DRIVE_2                         = 2, /* ~20 mA (Default) */
    GPIO_DRIVE_3                         = 3  /* ~40 mA */
} gpio_drive_strength_t;

/* ========================================================================= */
/* Pin Interrupt Trigger Type Enumeration                                    */
/* ========================================================================= */
typedef enum {
    GPIO_INTR_DISABLE                    = 0,
    GPIO_INTR_RISING_EDGE                = 1,
    GPIO_INTR_FALLING_EDGE               = 2,
    GPIO_INTR_ANY_EDGE                   = 3,
    GPIO_INTR_LOW_LEVEL                  = 4,
    GPIO_INTR_HIGH_LEVEL                 = 5
} gpio_intr_type_t;

/* ========================================================================= */
/* Pin Output Driver Mode                                                    */
/* ========================================================================= */
typedef enum {
    GPIO_MODE_PUSH_PULL                  = 0,
    GPIO_MODE_OPEN_DRAIN                 = 1
} gpio_drive_mode_t;

/* ========================================================================= */
/* Telemetry Snapshot Structure                                              */
/* ========================================================================= */
typedef struct {
    uint32_t enable_mask;
    uint32_t out_mask;
    uint32_t in_mask;
    uint32_t status_mask;
} gpio_telemetry_t;

/* ========================================================================= */
/* Public Driver API Function Declarations                                   */
/* ========================================================================= */

/*
 * gpio_init
 * Enables GPIO and IO_MUX peripheral clocks, releases resets.
 */
int gpio_init(void);

/*
 * gpio_set_direction
 * Configures pin input/output buffer direction.
 */
int gpio_set_direction(uint32_t pin, gpio_direction_t dir);

/*
 * gpio_set_pull
 * Configures internal pull-up and pull-down resistors for the specified pad.
 */
int gpio_set_pull(uint32_t pin, gpio_pull_t pull);

/*
 * gpio_set_drive_strength
 * Configures pad output drive strength (0: 5mA, 1: 10mA, 2: 20mA, 3: 40mA).
 */
int gpio_set_drive_strength(uint32_t pin, gpio_drive_strength_t drive);

/*
 * gpio_set_function
 * Configures IO_MUX function selection (e.g. IO_MUX_MCU_SEL_FUNC1_GPIO).
 */
int gpio_set_function(uint32_t pin, uint32_t func);

/*
 * gpio_set_drive_mode
 * Configures pad driver mode (push-pull vs open-drain).
 */
int gpio_set_drive_mode(uint32_t pin, gpio_drive_mode_t mode);

/*
 * gpio_set_level
 * Sets GPIO output level (0 = Low via W1TC, non-zero = High via W1TS).
 */
int gpio_set_level(uint32_t pin, uint32_t level);

/*
 * gpio_get_level
 * Reads physical input level of GPIO pin from GPIO_IN_REG.
 */
int gpio_get_level(uint32_t pin);

/*
 * gpio_get_output_level
 * Reads current driven output state of GPIO pin from GPIO_OUT_REG.
 */
int gpio_get_output_level(uint32_t pin);

/*
 * gpio_toggle_level
 * Toggles the output state of the specified GPIO pin.
 */
int gpio_toggle_level(uint32_t pin);

/*
 * gpio_set_intr_type
 * Configures interrupt trigger condition on the specified pin.
 */
int gpio_set_intr_type(uint32_t pin, gpio_intr_type_t intr_type);

/*
 * gpio_intr_enable
 * Enables CPU interrupt generation for the specified pin.
 */
int gpio_intr_enable(uint32_t pin);

/*
 * gpio_intr_disable
 * Disables CPU interrupt generation for the specified pin.
 */
int gpio_intr_disable(uint32_t pin);

/*
 * gpio_intr_clear
 * Clears pending interrupt flag on the specified pin.
 */
int gpio_intr_clear(uint32_t pin);

/*
 * gpio_get_telemetry
 * Populates snapshot of GPIO enable, out, in, and interrupt status masks.
 */
int gpio_get_telemetry(gpio_telemetry_t *telem);

#endif // IRON_V_GPIO_H
