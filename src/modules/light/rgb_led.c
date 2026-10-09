/*
 * Iron V - Addressable RGB LED driver (REV-23). See rgb_led.h.
 */
#include "rgb_led.h"
#include "gpio.h"
#include "string.h"

static bool s_rgb_led_started;   /* a frame was started; its TX-end flag tells when it is done */

static uint32_t rgb_led_pulse(uint32_t high_ticks, uint32_t low_ticks)
{
    uint32_t first = RGB_LED_PULSE_LEVEL_BIT | (high_ticks & RGB_LED_PULSE_PERIOD_MASK);
    uint32_t second = low_ticks & RGB_LED_PULSE_PERIOD_MASK;   /* level 0 */
    return first | (second << RGB_LED_PULSE_SECOND_SHIFT);
}

void rgb_led_encode(uint8_t r, uint8_t g, uint8_t b, uint32_t out[RGB_LED_FRAME_WORDS])
{
    const uint8_t grb[] = {g, r, b};
    uint32_t w = 0U;
    for (uint32_t i = 0U; i < sizeof(grb); i++)
    {
        for (uint8_t mask = RGB_LED_BYTE_MSB; mask != 0U; mask >>= 1)
        {
            out[w++] = ((grb[i] & mask) != 0U) ? rgb_led_pulse(RGB_LED_T1H_TICKS, RGB_LED_T1L_TICKS)
                                               : rgb_led_pulse(RGB_LED_T0H_TICKS, RGB_LED_T0L_TICKS);
        }
    }
    /* Reset low, then a zero period: end marker (idle level comes from IDLE_OUT_LV) */
    out[w] = RGB_LED_RESET_TICKS & RGB_LED_PULSE_PERIOD_MASK;
}

#if defined(__riscv)

void rgb_led_init(void)
{
    /* Bus clock on, out of reset; function clock PLL_F80M / 1 */
    *RGB_LED_PCR_RMT_CONF_REG |= RGB_LED_PCR_RMT_CLK_EN_BIT;
    *RGB_LED_PCR_RMT_CONF_REG &= ~RGB_LED_PCR_RMT_RST_EN_BIT;
    uint32_t sclk = *RGB_LED_PCR_RMT_SCLK_CONF_REG;
    sclk &= ~(RGB_LED_PCR_SCLK_DIV_MASK | RGB_LED_PCR_SCLK_SEL_MASK);
    sclk |= (RGB_LED_PCR_SCLK_SEL_PLL_F80M << RGB_LED_PCR_SCLK_SEL_SHIFT) | RGB_LED_PCR_SCLK_EN_BIT;
    *RGB_LED_PCR_RMT_SCLK_CONF_REG = sclk;

    /* Register clock, RAM clock, direct RAM access */
    uint32_t sys = *RGB_LED_RMT_REG(RGB_LED_RMT_SYS_CONF_OFFSET);
    sys &= ~RGB_LED_SYS_MEM_FORCE_PD_BIT;
    sys |= RGB_LED_SYS_CLK_EN_BIT | RGB_LED_SYS_MEM_CLK_FORCE_ON_BIT | RGB_LED_SYS_APB_FIFO_MASK_BIT;
    *RGB_LED_RMT_REG(RGB_LED_RMT_SYS_CONF_OFFSET) = sys;

    /* TX channel: divider, one RAM block, no carrier, idle low */
    volatile uint32_t *conf0 = RGB_LED_RMT_CONF0_REG(RGB_LED_RMT_CHANNEL);
    uint32_t c = *conf0;
    c &= ~(RGB_LED_CONF0_DIV_CNT_MASK | RGB_LED_CONF0_MEM_SIZE_MASK | RGB_LED_CONF0_CARRIER_EN_BIT |
           RGB_LED_CONF0_IDLE_OUT_LV_BIT);
    c |= (RGB_LED_RMT_DIV << RGB_LED_CONF0_DIV_CNT_SHIFT) |
         (RGB_LED_CONF0_MEM_BLOCKS << RGB_LED_CONF0_MEM_SIZE_SHIFT) | RGB_LED_CONF0_IDLE_OUT_EN_BIT;
    *conf0 = c;
    *conf0 = c | RGB_LED_CONF0_CONF_UPDATE_BIT;

    /* Pin: GPIO function and output enable, then route rmt_sig_out0 through the matrix */
    (void)gpio_set_direction(RGB_LED_GPIO, GPIO_DIR_OUTPUT);
    *GPIO_FUNC_OUT_SEL_REG(RGB_LED_GPIO) = RGB_LED_RMT_SIG_OUT0;

    *RGB_LED_RMT_REG(RGB_LED_RMT_INT_CLR_OFFSET) = RGB_LED_INT_TX_END_BIT(RGB_LED_RMT_CHANNEL);
    s_rgb_led_started = false;
}

bool rgb_led_write(uint8_t r, uint8_t g, uint8_t b)
{
    const uint32_t done_bit = RGB_LED_INT_TX_END_BIT(RGB_LED_RMT_CHANNEL);
    if (s_rgb_led_started && (*RGB_LED_RMT_REG(RGB_LED_RMT_INT_RAW_OFFSET) & done_bit) == 0U)
    {
        return false;
    }

    uint32_t words[RGB_LED_FRAME_WORDS];
    rgb_led_encode(r, g, b, words);
    for (uint32_t i = 0U; i < RGB_LED_FRAME_WORDS; i++)
    {
        *RGB_LED_RMT_RAM_REG(RGB_LED_RMT_CHANNEL, i) = words[i];
    }

    *RGB_LED_RMT_REG(RGB_LED_RMT_INT_CLR_OFFSET) = done_bit;
    volatile uint32_t *conf0 = RGB_LED_RMT_CONF0_REG(RGB_LED_RMT_CHANNEL);
    *conf0 |= RGB_LED_CONF0_MEM_RD_RST_BIT;
    *conf0 &= ~RGB_LED_CONF0_MEM_RD_RST_BIT;
    *conf0 |= RGB_LED_CONF0_CONF_UPDATE_BIT;
    *conf0 |= RGB_LED_CONF0_TX_START_BIT;
    s_rgb_led_started = true;
    return true;
}

#else /* host */

static uint8_t  s_host_rgb[3];
static uint32_t s_host_writes;
static bool     s_host_busy;

void rgb_led_init(void)
{
    memset(s_host_rgb, 0, sizeof(s_host_rgb));
    s_host_writes = 0U;
    s_host_busy = false;
    s_rgb_led_started = false;
}

bool rgb_led_write(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_host_busy)
    {
        return false;
    }
    s_host_rgb[0] = r;
    s_host_rgb[1] = g;
    s_host_rgb[2] = b;
    s_host_writes++;
    s_rgb_led_started = true;
    return true;
}

void rgb_led_host_last(uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = s_host_rgb[0];
    *g = s_host_rgb[1];
    *b = s_host_rgb[2];
}

uint32_t rgb_led_host_writes(void)
{
    return s_host_writes;
}

void rgb_led_host_set_busy(bool busy)
{
    s_host_busy = busy;
}

#endif
