/*
 * src/gpio.c
 *
 * ESP32-C6 GPIO Matrix & IO_MUX Multi-Function Pin Routing Driver
 * TRM Chapter 7 (IO MUX and GPIO Matrix, §7.1-§7.9)
 *
 * Implements pin configuration, IO_MUX pull/drive control, atomic W1TS/W1TC
 * level switching, and status telemetry with hardware memory fences.
 */

#include "gpio.h"

/* ========================================================================= */
/* Internal State & Emulation Support                                        */
/* ========================================================================= */
static bool s_gpio_initialized = false;

#if defined(__riscv)

static inline void gpio_fence(void)
{
    asm volatile("fence rw, rw" ::: "memory");
}

#else

/* Host Emulation Structures */
static uint32_t s_mock_gpio_out = 0U;
static uint32_t s_mock_gpio_enable = 0U;
static uint32_t s_mock_gpio_in = 0U;
static uint32_t s_mock_gpio_status = 0U;
static uint32_t s_mock_io_mux[GPIO_PIN_COUNT];
static uint32_t s_mock_gpio_pin_conf[GPIO_PIN_COUNT];
static uint32_t s_mock_gpio_func_out[GPIO_PIN_COUNT];

static inline void gpio_fence(void)
{
    __sync_synchronize();
}

#endif /* __riscv */

/* ========================================================================= */
/* Driver Initialization                                                     */
/* ========================================================================= */

int gpio_init(void)
{
#if defined(__riscv)
    /* 1. Enable GPIO peripheral clock gate */
    *GPIO_CLOCK_GATE_REG |= GPIO_CLOCK_GATE_CLK_EN_BIT;
    gpio_fence();

    /* 2. Enable IO_MUX peripheral clock and release reset */
    *PCR_IOMUX_CONF_REG |= PCR_IOMUX_CONF_CLK_EN_BIT;
    *PCR_IOMUX_CONF_REG &= ~PCR_IOMUX_CONF_RST_EN_BIT;
    gpio_fence();
#else
    s_mock_gpio_out = 0U;
    s_mock_gpio_enable = 0U;
    s_mock_gpio_in = 0U;
    s_mock_gpio_status = 0U;
    for (uint32_t i = 0U; i < GPIO_PIN_COUNT; i++)
    {
        s_mock_io_mux[i] = (IO_MUX_MCU_SEL_FUNC1_GPIO << IO_MUX_MCU_SEL_SHIFT) |
                           (GPIO_DRIVE_2 << IO_MUX_FUN_DRV_SHIFT);
        s_mock_gpio_pin_conf[i] = 0U;
        s_mock_gpio_func_out[i] = GPIO_MATRIX_SIG_DIRECT_OUT;
    }
#endif

    s_gpio_initialized = true;
    return GPIO_OK;
}

/* ========================================================================= */
/* Pin Direction & Function Configuration                                    */
/* ========================================================================= */

int gpio_set_direction(uint32_t pin, gpio_direction_t dir)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    volatile uint32_t *mux_reg = IO_MUX_GPIO_REG(pin);

    switch (dir)
    {
        case GPIO_DIR_INPUT:
            /* Configure GPIO function, enable input buffer, disable output buffer */
            *mux_reg = (*mux_reg & ~IO_MUX_MCU_SEL_MASK) |
                       (IO_MUX_MCU_SEL_FUNC1_GPIO << IO_MUX_MCU_SEL_SHIFT) |
                       IO_MUX_FUN_IE_BIT;
            *GPIO_ENABLE_W1TC_REG = (1U << pin);
            break;

        case GPIO_DIR_OUTPUT:
            /* Configure GPIO function, enable input buffer for readback, enable output buffer, route direct out */
            *mux_reg = (*mux_reg & ~IO_MUX_MCU_SEL_MASK) |
                       (IO_MUX_MCU_SEL_FUNC1_GPIO << IO_MUX_MCU_SEL_SHIFT) |
                       IO_MUX_FUN_IE_BIT;
            *GPIO_FUNC_OUT_SEL_REG(pin) = GPIO_MATRIX_SIG_DIRECT_OUT;
            *GPIO_ENABLE_W1TS_REG = (1U << pin);
            break;

        case GPIO_DIR_BIDIRECTIONAL:
            /* Configure GPIO function, enable input buffer, enable output buffer, route direct out */
            *mux_reg = (*mux_reg & ~IO_MUX_MCU_SEL_MASK) |
                       (IO_MUX_MCU_SEL_FUNC1_GPIO << IO_MUX_MCU_SEL_SHIFT) |
                       IO_MUX_FUN_IE_BIT;
            *GPIO_FUNC_OUT_SEL_REG(pin) = GPIO_MATRIX_SIG_DIRECT_OUT;
            *GPIO_ENABLE_W1TS_REG = (1U << pin);
            break;

        default:
            return GPIO_ERR_INVALID_ARG;
    }
    gpio_fence();
#else
    switch (dir)
    {
        case GPIO_DIR_INPUT:
            s_mock_io_mux[pin] |= IO_MUX_FUN_IE_BIT;
            s_mock_gpio_enable &= ~(1U << pin);
            break;

        case GPIO_DIR_OUTPUT:
            s_mock_io_mux[pin] |= IO_MUX_FUN_IE_BIT;
            s_mock_gpio_enable |= (1U << pin);
            s_mock_gpio_func_out[pin] = GPIO_MATRIX_SIG_DIRECT_OUT;
            break;

        case GPIO_DIR_BIDIRECTIONAL:
            s_mock_io_mux[pin] |= IO_MUX_FUN_IE_BIT;
            s_mock_gpio_enable |= (1U << pin);
            s_mock_gpio_func_out[pin] = GPIO_MATRIX_SIG_DIRECT_OUT;
            break;

        default:
            return GPIO_ERR_INVALID_ARG;
    }
#endif

    return GPIO_OK;
}

int gpio_set_pull(uint32_t pin, gpio_pull_t pull)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    volatile uint32_t *mux_reg = IO_MUX_GPIO_REG(pin);
    uint32_t val = *mux_reg & ~(IO_MUX_FUN_WPU_BIT | IO_MUX_FUN_WPD_BIT);

    switch (pull)
    {
        case GPIO_PULL_NONE:
            break;

        case GPIO_PULL_UP:
            val |= IO_MUX_FUN_WPU_BIT;
            break;

        case GPIO_PULL_DOWN:
            val |= IO_MUX_FUN_WPD_BIT;
            break;

        case GPIO_PULL_BOTH:
            val |= (IO_MUX_FUN_WPU_BIT | IO_MUX_FUN_WPD_BIT);
            break;

        default:
            return GPIO_ERR_INVALID_ARG;
    }

    *mux_reg = val;
    gpio_fence();
#else
    s_mock_io_mux[pin] &= ~(IO_MUX_FUN_WPU_BIT | IO_MUX_FUN_WPD_BIT);
    switch (pull)
    {
        case GPIO_PULL_NONE:
            break;

        case GPIO_PULL_UP:
            s_mock_io_mux[pin] |= IO_MUX_FUN_WPU_BIT;
            s_mock_gpio_in |= (1U << pin);
            break;

        case GPIO_PULL_DOWN:
            s_mock_io_mux[pin] |= IO_MUX_FUN_WPD_BIT;
            s_mock_gpio_in &= ~(1U << pin);
            break;

        case GPIO_PULL_BOTH:
            s_mock_io_mux[pin] |= (IO_MUX_FUN_WPU_BIT | IO_MUX_FUN_WPD_BIT);
            break;

        default:
            return GPIO_ERR_INVALID_ARG;
    }
#endif

    return GPIO_OK;
}

int gpio_set_drive_strength(uint32_t pin, gpio_drive_strength_t drive)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

    if ((uint32_t)drive > (uint32_t)GPIO_DRIVE_3)
    {
        return GPIO_ERR_INVALID_ARG;
    }

#if defined(__riscv)
    volatile uint32_t *mux_reg = IO_MUX_GPIO_REG(pin);
    uint32_t val = *mux_reg & ~IO_MUX_FUN_DRV_MASK;
    val |= (((uint32_t)drive & 0x3U) << IO_MUX_FUN_DRV_SHIFT);
    *mux_reg = val;
    gpio_fence();
#else
    s_mock_io_mux[pin] &= ~IO_MUX_FUN_DRV_MASK;
    s_mock_io_mux[pin] |= (((uint32_t)drive & 0x3U) << IO_MUX_FUN_DRV_SHIFT);
#endif

    return GPIO_OK;
}

int gpio_set_function(uint32_t pin, uint32_t func)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

    if (func > 7U)
    {
        return GPIO_ERR_INVALID_ARG;
    }

#if defined(__riscv)
    volatile uint32_t *mux_reg = IO_MUX_GPIO_REG(pin);
    uint32_t val = *mux_reg & ~IO_MUX_MCU_SEL_MASK;
    val |= ((func & 0x7U) << IO_MUX_MCU_SEL_SHIFT);
    *mux_reg = val;
    gpio_fence();
#else
    s_mock_io_mux[pin] &= ~IO_MUX_MCU_SEL_MASK;
    s_mock_io_mux[pin] |= ((func & 0x7U) << IO_MUX_MCU_SEL_SHIFT);
#endif

    return GPIO_OK;
}

int gpio_set_drive_mode(uint32_t pin, gpio_drive_mode_t mode)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    volatile uint32_t *conf_reg = GPIO_PIN_CONF_REG(pin);
    if (mode == GPIO_MODE_OPEN_DRAIN)
    {
        *conf_reg |= GPIO_PIN_PAD_DRIVER_BIT;
    }
    else
    {
        *conf_reg &= ~GPIO_PIN_PAD_DRIVER_BIT;
    }
    gpio_fence();
#else
    if (mode == GPIO_MODE_OPEN_DRAIN)
    {
        s_mock_gpio_pin_conf[pin] |= GPIO_PIN_PAD_DRIVER_BIT;
    }
    else
    {
        s_mock_gpio_pin_conf[pin] &= ~GPIO_PIN_PAD_DRIVER_BIT;
    }
#endif

    return GPIO_OK;
}

/* ========================================================================= */
/* Level Read / Write / Toggle                                               */
/* ========================================================================= */

int gpio_set_level(uint32_t pin, uint32_t level)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    if (level != 0U)
    {
        *GPIO_OUT_W1TS_REG = (1U << pin);
    }
    else
    {
        *GPIO_OUT_W1TC_REG = (1U << pin);
    }
    gpio_fence();
#else
    if (level != 0U)
    {
        s_mock_gpio_out |= (1U << pin);
        s_mock_gpio_in |= (1U << pin);
    }
    else
    {
        s_mock_gpio_out &= ~(1U << pin);
        s_mock_gpio_in &= ~(1U << pin);
    }
#endif

    return GPIO_OK;
}

int gpio_get_level(uint32_t pin)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    gpio_fence();
    return ((*GPIO_IN_REG & (1U << pin)) != 0U) ? 1 : 0;
#else
    return ((s_mock_gpio_in & (1U << pin)) != 0U) ? 1 : 0;
#endif
}

int gpio_get_output_level(uint32_t pin)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    gpio_fence();
    return ((*GPIO_OUT_REG & (1U << pin)) != 0U) ? 1 : 0;
#else
    return ((s_mock_gpio_out & (1U << pin)) != 0U) ? 1 : 0;
#endif
}

int gpio_toggle_level(uint32_t pin)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    gpio_fence();
    uint32_t current = *GPIO_OUT_REG & (1U << pin);
    if (current != 0U)
    {
        *GPIO_OUT_W1TC_REG = (1U << pin);
    }
    else
    {
        *GPIO_OUT_W1TS_REG = (1U << pin);
    }
    gpio_fence();
#else
    if ((s_mock_gpio_out & (1U << pin)) != 0U)
    {
        s_mock_gpio_out &= ~(1U << pin);
        s_mock_gpio_in &= ~(1U << pin);
    }
    else
    {
        s_mock_gpio_out |= (1U << pin);
        s_mock_gpio_in |= (1U << pin);
    }
#endif

    return GPIO_OK;
}

/* ========================================================================= */
/* Interrupt Configuration & Control                                         */
/* ========================================================================= */

int gpio_set_intr_type(uint32_t pin, gpio_intr_type_t intr_type)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    volatile uint32_t *conf_reg = GPIO_PIN_CONF_REG(pin);
    uint32_t val = *conf_reg & ~GPIO_PIN_INT_TYPE_MASK;
    val |= (((uint32_t)intr_type & 0x7U) << GPIO_PIN_INT_TYPE_SHIFT);
    *conf_reg = val;
    gpio_fence();
#else
    s_mock_gpio_pin_conf[pin] &= ~GPIO_PIN_INT_TYPE_MASK;
    s_mock_gpio_pin_conf[pin] |= (((uint32_t)intr_type & 0x7U) << GPIO_PIN_INT_TYPE_SHIFT);
#endif

    return GPIO_OK;
}

int gpio_intr_enable(uint32_t pin)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    *GPIO_PIN_CONF_REG(pin) |= GPIO_PIN_CPU_INT_ENA_BIT;
    gpio_fence();
#else
    s_mock_gpio_pin_conf[pin] |= GPIO_PIN_CPU_INT_ENA_BIT;
#endif

    return GPIO_OK;
}

int gpio_intr_disable(uint32_t pin)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    *GPIO_PIN_CONF_REG(pin) &= ~GPIO_PIN_CPU_INT_ENA_BIT;
    gpio_fence();
#else
    s_mock_gpio_pin_conf[pin] &= ~GPIO_PIN_CPU_INT_ENA_BIT;
#endif

    return GPIO_OK;
}

int gpio_intr_clear(uint32_t pin)
{
    if (pin > GPIO_PIN_MAX)
    {
        return GPIO_ERR_INVALID_PIN;
    }

#if defined(__riscv)
    *GPIO_STATUS_W1TC_REG = (1U << pin);
    gpio_fence();
#else
    s_mock_gpio_status &= ~(1U << pin);
#endif

    return GPIO_OK;
}

/* ========================================================================= */
/* Telemetry Query                                                           */
/* ========================================================================= */

int gpio_get_telemetry(gpio_telemetry_t *telem)
{
    if (telem == NULL)
    {
        return GPIO_ERR_INVALID_ARG;
    }

#if defined(__riscv)
    gpio_fence();
    telem->enable_mask = *GPIO_ENABLE_REG;
    telem->out_mask    = *GPIO_OUT_REG;
    telem->in_mask     = *GPIO_IN_REG;
    telem->status_mask = *GPIO_STATUS_REG;
#else
    telem->enable_mask = s_mock_gpio_enable;
    telem->out_mask    = s_mock_gpio_out;
    telem->in_mask     = s_mock_gpio_in;
    telem->status_mask = s_mock_gpio_status;
#endif

    return GPIO_OK;
}
