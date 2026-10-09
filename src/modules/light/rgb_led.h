/*
 * Iron V - Addressable RGB LED driver (REV-23)
 *
 * The ESP32-C6-DevKitC-1 has one WS2812-class LED on GPIO8. RMT TX channel 0 sends the 24 bits
 * (GRB, MSB first) plus a reset low; the CPU only fills the channel RAM and starts it, so flash
 * cache misses cannot stretch a bit. The LED is written only when its value changes.
 * GPIO8 is a strapping pin: it is configured as an output here, after boot, never during reset.
 */
#ifndef IRON_V_RGB_LED_H
#define IRON_V_RGB_LED_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"

#define RGB_LED_GPIO                     CONFIG_LIGHT_LED_GPIO

/* RMT (TRM chapter 37). The generated regs/rmt.h only covers channel 0 offsets; these add the
 * channel index, the channel RAM and the APB/clock configuration register. */
#define RGB_LED_RMT_CHANNEL              0U
#define RGB_LED_RMT_BASE                 0x60006000U
#define RGB_LED_RMT_CONF0_OFFSET         0x010U   /* RMT_CHnCONF0_REG, n: 0-1, +4 per channel */
#define RGB_LED_RMT_SYS_CONF_OFFSET      0x068U   /* RMT_SYS_CONF_REG */
#define RGB_LED_RMT_INT_RAW_OFFSET       0x038U
#define RGB_LED_RMT_INT_CLR_OFFSET       0x044U
#define RGB_LED_RMT_RAM_OFFSET           0x400U   /* channel RAM, NONFIFO (direct) access */
#define RGB_LED_RMT_RAM_WORDS_PER_CH     48U      /* SOC_RMT_MEM_WORDS_PER_CHANNEL */
#define RGB_LED_RMT_REG(off)             ((volatile uint32_t *)(uintptr_t)(RGB_LED_RMT_BASE + (off)))
#define RGB_LED_RMT_CONF0_REG(ch)        RGB_LED_RMT_REG(RGB_LED_RMT_CONF0_OFFSET + ((ch) * 4U))
#define RGB_LED_RMT_RAM_REG(ch, word)    RGB_LED_RMT_REG(RGB_LED_RMT_RAM_OFFSET + \
                                             ((((ch) * RGB_LED_RMT_RAM_WORDS_PER_CH) + (word)) * 4U))

/* RMT_SYS_CONF_REG bits */
#define RGB_LED_SYS_APB_FIFO_MASK_BIT    (1U << 0)   /* 1: direct RAM access */
#define RGB_LED_SYS_MEM_CLK_FORCE_ON_BIT (1U << 1)
#define RGB_LED_SYS_MEM_FORCE_PD_BIT     (1U << 2)
#define RGB_LED_SYS_CLK_EN_BIT           (1U << 31)  /* register clock gate */

/* RMT_CHnCONF0_REG (TX) bits */
#define RGB_LED_CONF0_TX_START_BIT       (1U << 0)
#define RGB_LED_CONF0_MEM_RD_RST_BIT     (1U << 1)
#define RGB_LED_CONF0_APB_MEM_RST_BIT    (1U << 2)
#define RGB_LED_CONF0_IDLE_OUT_LV_BIT    (1U << 5)
#define RGB_LED_CONF0_IDLE_OUT_EN_BIT    (1U << 6)
#define RGB_LED_CONF0_DIV_CNT_SHIFT      8U
#define RGB_LED_CONF0_DIV_CNT_MASK       (0xFFU << RGB_LED_CONF0_DIV_CNT_SHIFT)
#define RGB_LED_CONF0_MEM_SIZE_SHIFT     16U
#define RGB_LED_CONF0_MEM_SIZE_MASK      (0x7U << RGB_LED_CONF0_MEM_SIZE_SHIFT)
#define RGB_LED_CONF0_CARRIER_EN_BIT     (1U << 21)
#define RGB_LED_CONF0_CONF_UPDATE_BIT    (1U << 24)
#define RGB_LED_CONF0_MEM_BLOCKS         1U

/* RMT_INT_RAW/CLR: TX end of channel n is bit n */
#define RGB_LED_INT_TX_END_BIT(ch)       (1U << (ch))

/* PCR: RMT bus clock and function clock (PLL_F80M, no fractional divider) */
#define RGB_LED_PCR_BASE                 0x60096000U
#define RGB_LED_PCR_RMT_CONF_REG         ((volatile uint32_t *)(uintptr_t)(RGB_LED_PCR_BASE + 0x2CU))
#define RGB_LED_PCR_RMT_SCLK_CONF_REG    ((volatile uint32_t *)(uintptr_t)(RGB_LED_PCR_BASE + 0x30U))
#define RGB_LED_PCR_RMT_CLK_EN_BIT       (1U << 0)
#define RGB_LED_PCR_RMT_RST_EN_BIT       (1U << 1)
#define RGB_LED_PCR_SCLK_DIV_MASK        0x000FFFFFU  /* DIV_A, DIV_B, DIV_NUM: all 0 = divide by 1 */
#define RGB_LED_PCR_SCLK_SEL_SHIFT       20U
#define RGB_LED_PCR_SCLK_SEL_MASK        (0x3U << RGB_LED_PCR_SCLK_SEL_SHIFT)
#define RGB_LED_PCR_SCLK_SEL_PLL_F80M    1U
#define RGB_LED_PCR_SCLK_EN_BIT          (1U << 22)

/* GPIO matrix output signal rmt_sig_out0 (TRM table 7.11-1; its output enable is always on) */
#define RGB_LED_RMT_SIG_OUT0             71U

/* Timing: 80 MHz / 8 = 10 MHz, 100 ns per tick. WS2812: 0 = 0.3 us high + 0.9 us low,
 * 1 = 0.9 us high + 0.3 us low (ESP-IDF led_strip values); reset low >= 280 us (WS2812B-V5). */
#define RGB_LED_RMT_DIV                  8U
#define RGB_LED_T0H_TICKS                3U
#define RGB_LED_T0L_TICKS                9U
#define RGB_LED_T1H_TICKS                9U
#define RGB_LED_T1L_TICKS                3U
#define RGB_LED_RESET_TICKS              2800U
#define RGB_LED_BITS                     24U
#define RGB_LED_FRAME_WORDS              (RGB_LED_BITS + 1U)   /* data + reset/end marker */

/* One RAM word holds two pulse codes: [15] level, [14:0] period; the low half is sent first */
#define RGB_LED_PULSE_LEVEL_BIT          (1U << 15)
#define RGB_LED_PULSE_PERIOD_MASK        0x7FFFU
#define RGB_LED_PULSE_SECOND_SHIFT       16U
#define RGB_LED_BYTE_MSB                 0x80U
#define RGB_LED_BYTE_BITS                8U

/* Fills out[RGB_LED_FRAME_WORDS] with the RMT words for one colour (host-testable) */
void rgb_led_encode(uint8_t r, uint8_t g, uint8_t b, uint32_t out[RGB_LED_FRAME_WORDS]);

void rgb_led_init(void);
/* Starts sending r,g,b. false: the previous frame is still on the wire (try again later). */
bool rgb_led_write(uint8_t r, uint8_t g, uint8_t b);

#if !defined(__riscv)
/* Host tests: the last colour handed to the "hardware" and how many frames were sent */
void     rgb_led_host_last(uint8_t *r, uint8_t *g, uint8_t *b);
uint32_t rgb_led_host_writes(void);
void     rgb_led_host_set_busy(bool busy);
#endif

#endif /* IRON_V_RGB_LED_H */
