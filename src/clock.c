#include "clock.h"
#include "io_constants.h"

void clock_init(void)
{
    /* 1. Ensure XTAL frequency is explicitly set to 40 MHz in PCR */
    uint32_t sysclk = *PCR_SYSCLK_CONF_REG;
    sysclk &= ~PCR_SYSCLK_CONF_CLK_XTAL_FREQ_M;
    sysclk |= (40U << PCR_SYSCLK_CONF_CLK_XTAL_FREQ_S);
    *PCR_SYSCLK_CONF_REG = sysclk;
    FENCE();

    /* 2. Enable SPLL clock dividers (160 MHz and 80 MHz branches) */
    *PCR_PLL_DIV_CLK_EN_REG |= (PCR_PLL_DIV_CLK_EN_PLL_160M_CLK_EN_M |
                                PCR_PLL_DIV_CLK_EN_PLL_80M_CLK_EN_M);
    FENCE();

    /* 3. Configure CPU Frequency Divider (Divider = 0 for full 160 MHz) */
    *PCR_CPU_FREQ_CONF_REG = (0U << PCR_CPU_FREQ_CONF_CPU_LS_DIV_NUM_S) |
                             (0U << PCR_CPU_FREQ_CONF_CPU_HS_DIV_NUM_S);
    FENCE();

    /* 4. Configure AHB Bus Divider (Divider = 0) */
    *PCR_AHB_FREQ_CONF_REG = (0U << PCR_AHB_FREQ_CONF_AHB_LS_DIV_NUM_S) |
                             (0U << PCR_AHB_FREQ_CONF_AHB_HS_DIV_NUM_S);
    FENCE();

    /* 5. Configure APB Bus Divider (Divider = 1 -> divide by 2 for 80 MHz APB) */
    *PCR_APB_FREQ_CONF_REG = (0U << PCR_APB_FREQ_CONF_APB_DECREASE_DIV_NUM_S) |
                             (1U << PCR_APB_FREQ_CONF_APB_DIV_NUM_S);
    FENCE();

    /* 6. Switch SOC_CLK_SEL to PLL (2'b10 = 2) */
    sysclk = *PCR_SYSCLK_CONF_REG;
    sysclk &= ~PCR_SYSCLK_CONF_SOC_CLK_SEL_M;
    sysclk |= (2U << PCR_SYSCLK_CONF_SOC_CLK_SEL_S);
    *PCR_SYSCLK_CONF_REG = sysclk;
    FENCE();
}

void clock_get_config(clock_config_t *cfg)
{
    if (!cfg) return;

    uint32_t sysclk = *PCR_SYSCLK_CONF_REG;
    cfg->xtal_mhz = (sysclk & PCR_SYSCLK_CONF_CLK_XTAL_FREQ_M) >> PCR_SYSCLK_CONF_CLK_XTAL_FREQ_S;
    uint32_t clk_sel = (sysclk & PCR_SYSCLK_CONF_SOC_CLK_SEL_M) >> PCR_SYSCLK_CONF_SOC_CLK_SEL_S;

    if (clk_sel == 2)
    {
        /* Running on PLL */
        cfg->pll_mhz = 480;
        cfg->cpu_mhz = 160;
        cfg->apb_mhz = 80;
    }
    else
    {
        /* Running on XTAL or RC_FAST */
        cfg->pll_mhz = 0;
        cfg->cpu_mhz = cfg->xtal_mhz ? cfg->xtal_mhz : 40;
        cfg->apb_mhz = cfg->cpu_mhz;
    }
}

uint32_t clock_get_cpu_freq_hz(void)
{
    clock_config_t cfg;
    clock_get_config(&cfg);
    return cfg.cpu_mhz * 1000000U;
}

uint32_t clock_get_apb_freq_hz(void)
{
    clock_config_t cfg;
    clock_get_config(&cfg);
    return cfg.apb_mhz * 1000000U;
}
