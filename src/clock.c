#include "clock.h"
#include "io_constants.h"

void clock_init(void)
{
    /* 1. Ensure XTAL frequency is explicitly set to 40 MHz in PCR */
    uint32_t sysclk = *PCR_SYSCLK_CONF_REG;
    sysclk &= ~PCR_SYSCLK_CONF_CLK_XTAL_FREQ_M;
    sysclk |= (SOC_XTAL_FREQ_MHZ << PCR_SYSCLK_CONF_CLK_XTAL_FREQ_S);
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

    /* 4. Configure AHB Bus Divider (Divider = 3 -> divide by 4 for 40 MHz AHB, TRM Table 8.2-2 Note 4) */
    *PCR_AHB_FREQ_CONF_REG = (0U << PCR_AHB_FREQ_CONF_AHB_LS_DIV_NUM_S) |
                             (3U << PCR_AHB_FREQ_CONF_AHB_HS_DIV_NUM_S);
    FENCE();

    /* 5. Configure APB Bus Divider (Divider = 0 -> divide by 1 from 40 MHz AHB for 40 MHz APB) */
    *PCR_APB_FREQ_CONF_REG = (0U << PCR_APB_FREQ_CONF_APB_DECREASE_DIV_NUM_S) |
                             (SOC_APB_DIVIDER_1 << PCR_APB_FREQ_CONF_APB_DIV_NUM_S);
    FENCE();

    /* 6. Switch SOC_CLK_SEL to PLL */
    sysclk = *PCR_SYSCLK_CONF_REG;
    sysclk &= ~PCR_SYSCLK_CONF_SOC_CLK_SEL_M;
    sysclk |= (CLK_SOURCE_PLL << PCR_SYSCLK_CONF_SOC_CLK_SEL_S);
    *PCR_SYSCLK_CONF_REG = sysclk;
    FENCE();
}

void clock_get_config(clock_config_t *cfg)
{
    if (!cfg) return;

    uint32_t sysclk = *PCR_SYSCLK_CONF_REG;
    cfg->xtal_mhz = (sysclk & PCR_SYSCLK_CONF_CLK_XTAL_FREQ_M) >> PCR_SYSCLK_CONF_CLK_XTAL_FREQ_S;
    soc_clk_src_t clk_sel = (soc_clk_src_t)((sysclk & PCR_SYSCLK_CONF_SOC_CLK_SEL_M) >> PCR_SYSCLK_CONF_SOC_CLK_SEL_S);

    if (clk_sel == CLK_SOURCE_PLL)
    {
        /* Running on PLL (160 MHz CPU, 40 MHz AHB/APB) */
        cfg->pll_mhz = SOC_PLL_CORE_FREQ_MHZ;
        cfg->cpu_mhz = SOC_CPU_TARGET_FREQ_MHZ;
        cfg->apb_mhz = SOC_APB_TARGET_FREQ_MHZ;
    }
    else if (clk_sel == CLK_SOURCE_RC_FAST)
    {
        /* Running on internal 17.5 MHz RC oscillator */
        cfg->pll_mhz = 0;
        cfg->cpu_mhz = 17;
        cfg->apb_mhz = 17;
    }
    else
    {
        /* Running on XTAL */
        cfg->pll_mhz = 0;
        cfg->cpu_mhz = cfg->xtal_mhz ? cfg->xtal_mhz : SOC_XTAL_FREQ_MHZ;
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
