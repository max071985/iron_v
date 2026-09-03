#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

/* Clock frequency configuration representation */
typedef struct {
    uint32_t xtal_mhz;       /* 40 MHz physical crystal */
    uint32_t pll_mhz;        /* 480 MHz PLL core frequency */
    uint32_t cpu_mhz;        /* Target: 160 MHz */
    uint32_t apb_mhz;        /* Target: 80 MHz */
} clock_config_t;

typedef enum {
    CLK_SOURCE_XTAL = 0,
    CLK_SOURCE_RC_FAST = 1,
    CLK_SOURCE_PLL = 2
} soc_clk_src_t;

/* Initialize PCR clock tree to 160 MHz CPU PLL and 80 MHz APB */
void clock_init(void);

/* Query active clock configuration */
void clock_get_config(clock_config_t *cfg);

/* Returns current CPU frequency in Hz */
uint32_t clock_get_cpu_freq_hz(void);

/* Returns current APB frequency in Hz */
uint32_t clock_get_apb_freq_hz(void);

#endif // CLOCK_H
