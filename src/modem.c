/*
 * src/modem.c
 *
 * ESP32-C6 Modem Clock & Power Control Driver (MODEM_SYSCON / MODEM_LPCON)
 * TRM Chapter 8 (Reset and Clock, §8.3-§8.4) & SVD Hardware Register Map
 *
 * Implements hardware clock gating, power domain sequencing, and reset
 * orchestration for Wi-Fi, Bluetooth 5 (LE), IEEE 802.15.4 (Zigbee/Thread),
 * and RF coexistence low-power domains.
 */

#include "modem.h"

/* ========================================================================= */
/* Driver Internal State & Platform Emulation Storage                        */
/* ========================================================================= */
static modem_clock_state_t s_modem_state = {
    .wifi_clk_enabled      = 0U,
    .ble_clk_enabled       = 0U,
    .ieee802154_clk_enabled = 0U,
    .coexistence_enabled   = 0U
};

static bool s_modem_initialized = false;
static bool s_rf_synth_enabled = false;
static bool s_sar_adc_cal_primed = false;

#if defined(__riscv)

static inline void modem_fence(void)
{
    asm volatile("fence rw, rw" ::: "memory");
}

static inline void modem_delay(uint32_t cycles)
{
    for (volatile uint32_t i = 0; i < cycles; i++)
    {
        asm volatile("nop");
    }
}

static inline uint32_t reg_read(volatile uint32_t *addr)
{
    return *addr;
}

static inline void reg_write(volatile uint32_t *addr, uint32_t val)
{
    *addr = val;
    modem_fence();
}

static inline void reg_set_bits(volatile uint32_t *addr, uint32_t mask)
{
    *addr |= mask;
    modem_fence();
}

static inline void reg_clear_bits(volatile uint32_t *addr, uint32_t mask)
{
    *addr &= ~mask;
    modem_fence();
}

#else

/* Host Emulation Environment for Native Verification */
static uint32_t s_mock_pcr_modem_apb_conf   = 0U;
static uint32_t s_mock_modem_syscon_clk_conf = MODEM_CLK_DATA_DUMP_MUX_BIT;
static uint32_t s_mock_modem_syscon_clk_conf_fo = 0U;
static uint32_t s_mock_modem_syscon_clk_conf1 = 0U;
static uint32_t s_mock_modem_syscon_rst_conf  = 0U;
static uint32_t s_mock_modem_lpcon_coex_lp  = 0U;
static uint32_t s_mock_modem_lpcon_clk_conf = 0U;
static uint32_t s_mock_modem_syscon_date    = MODEM_SYSCON_DATE_EXPECTED;
static uint32_t s_mock_modem_lpcon_date     = MODEM_LPCON_DATE_EXPECTED;
static uint32_t s_mock_ieee802154_command   = 0U;
static uint32_t s_mock_ieee802154_ctrl_cfg  = 0U;

static uint32_t s_mock_lpcon_i2c_mst       = 0U;
static uint32_t s_mock_lpcon_fo            = 0U;
static uint32_t s_mock_lpcon_rst           = 0U;
static uint32_t s_mock_lpcon_mem           = 0U;
static uint32_t s_mock_lp_peri_clk_en      = 0U;
static uint32_t s_mock_lp_peri_rst_en      = 0U;
static uint32_t s_mock_lp_i2c_ana_mst_dev  = 0U;
static uint32_t s_mock_pmu_hp_active_icg   = 0U;
static uint32_t s_mock_pmu_imm_modem_icg   = 0U;
static uint32_t s_mock_pmu_imm_sleep_sysclk = 0U;
static uint32_t s_mock_pmu_imm_hp_ck_power = 0U;
static uint32_t s_mock_syscon_pwr_st       = 0U;
static uint32_t s_mock_lpcon_pwr_st        = 0U;
static uint32_t s_mock_lp_ana_peri_pwr     = 0U;
static uint32_t s_mock_lp_ana_peri_clk     = 0U;
static uint32_t s_mock_modem_rf_enable     = 0U;
static uint32_t s_mock_modem_rf_agc_ctrl   = 0U;
static uint32_t s_mock_i2c_ana_link0       = 0U;
static uint32_t s_mock_i2c_ana_link1       = 0U;
static uint32_t s_mock_modem_rf_analog_sw0 = 0U;
static uint32_t s_mock_modem_rf_analog_sw1 = 0U;
static uint32_t s_mock_sar_adc_cal[APB_SARADC_CAL_REG_COUNT] = {0};

static inline void modem_fence(void)
{
    __sync_synchronize();
}

static inline void modem_delay(uint32_t cycles)
{
    (void)cycles;
}

static inline uint32_t reg_read(volatile uint32_t *addr)
{
    uintptr_t a = (uintptr_t)addr;
    if (a == (uintptr_t)PCR_MODEM_APB_CONF_REG) return s_mock_pcr_modem_apb_conf;
    if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF_REG) return s_mock_modem_syscon_clk_conf;
    if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF_FORCE_ON_REG) return s_mock_modem_syscon_clk_conf_fo;
    if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF1_REG) return s_mock_modem_syscon_clk_conf1;
    if (a == (uintptr_t)MODEM_SYSCON_MODEM_RST_CONF_REG) return s_mock_modem_syscon_rst_conf;
    if (a == (uintptr_t)MODEM_LPCON_COEX_LP_CLK_CONF_REG) return s_mock_modem_lpcon_coex_lp;
    if (a == (uintptr_t)MODEM_LPCON_CLK_CONF_REG) return s_mock_modem_lpcon_clk_conf;
    if (a == (uintptr_t)MODEM_LPCON_I2C_MST_CLK_CONF_REG) return s_mock_lpcon_i2c_mst;
    if (a == (uintptr_t)MODEM_LPCON_CLK_CONF_FORCE_ON_REG) return s_mock_lpcon_fo;
    if (a == (uintptr_t)MODEM_LPCON_RST_CONF_REG) return s_mock_lpcon_rst;
    if (a == (uintptr_t)MODEM_LPCON_MEM_CONF_REG) return s_mock_lpcon_mem;
    if (a == (uintptr_t)LP_PERI_CLK_EN_REG) return s_mock_lp_peri_clk_en;
    if (a == (uintptr_t)LP_PERI_RESET_EN_REG) return s_mock_lp_peri_rst_en;
    if (a == (uintptr_t)LP_I2C_ANA_MST_DEVICE_EN_REG) return s_mock_lp_i2c_ana_mst_dev;
    if (a == (uintptr_t)MODEM_SYSCON_DATE_REG) return s_mock_modem_syscon_date;
    if (a == (uintptr_t)MODEM_LPCON_DATE_REG) return s_mock_modem_lpcon_date;
    if (a == (uintptr_t)IEEE802154_COMMAND_REG) return s_mock_ieee802154_command;
    if (a == (uintptr_t)IEEE802154_CTRL_CFG_REG) return s_mock_ieee802154_ctrl_cfg;
    if (a == (uintptr_t)PMU_HP_ACTIVE_ICG_MODEM_REG) return s_mock_pmu_hp_active_icg;
    if (a == (uintptr_t)PMU_IMM_MODEM_ICG_REG) return s_mock_pmu_imm_modem_icg;
    if (a == (uintptr_t)PMU_IMM_SLEEP_SYSCLK_REG) return s_mock_pmu_imm_sleep_sysclk;
    if (a == (uintptr_t)PMU_IMM_HP_CK_POWER_REG) return s_mock_pmu_imm_hp_ck_power;
    if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF_POWER_ST_REG) return s_mock_syscon_pwr_st;
    if (a == (uintptr_t)MODEM_LPCON_CLK_CONF_POWER_ST_REG) return s_mock_lpcon_pwr_st;
    if (a == (uintptr_t)LP_ANA_PERI_PWR_CONF_REG) return s_mock_lp_ana_peri_pwr;
    if (a == (uintptr_t)LP_ANA_PERI_CLK_CONF_REG) return s_mock_lp_ana_peri_clk;
    if (a == (uintptr_t)MODEM_RF_ENABLE_REG) return s_mock_modem_rf_enable;
    if (a == (uintptr_t)MODEM_RF_AGC_CTRL_REG) return s_mock_modem_rf_agc_ctrl;
    if (a == (uintptr_t)LP_CLKRST_I2C_ANA_MST_LINK0_REG) return s_mock_i2c_ana_link0;
    if (a == (uintptr_t)LP_CLKRST_I2C_ANA_MST_LINK1_REG) return s_mock_i2c_ana_link1;
    if (a == (uintptr_t)MODEM_RF_ANALOG_SWITCH0_REG) return s_mock_modem_rf_analog_sw0;
    if (a == (uintptr_t)MODEM_RF_ANALOG_SWITCH1_REG) return s_mock_modem_rf_analog_sw1;
    if (a >= (uintptr_t)APB_SARADC_CAL_REG(0) && a <= (uintptr_t)APB_SARADC_CAL_REG(APB_SARADC_CAL_REG_COUNT - 1))
    {
        return s_mock_sar_adc_cal[(a - (uintptr_t)APB_SARADC_CAL_REG(0)) / 4U];
    }
    return 0U;
}

static inline void reg_write(volatile uint32_t *addr, uint32_t val)
{
    uintptr_t a = (uintptr_t)addr;
    if (a == (uintptr_t)PCR_MODEM_APB_CONF_REG) s_mock_pcr_modem_apb_conf = val;
    else if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF_REG) s_mock_modem_syscon_clk_conf = val;
    else if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF_FORCE_ON_REG) s_mock_modem_syscon_clk_conf_fo = val;
    else if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF1_REG) s_mock_modem_syscon_clk_conf1 = val;
    else if (a == (uintptr_t)MODEM_SYSCON_MODEM_RST_CONF_REG) s_mock_modem_syscon_rst_conf = val;
    else if (a == (uintptr_t)MODEM_LPCON_COEX_LP_CLK_CONF_REG) s_mock_modem_lpcon_coex_lp = val;
    else if (a == (uintptr_t)MODEM_LPCON_CLK_CONF_REG) s_mock_modem_lpcon_clk_conf = val;
    else if (a == (uintptr_t)MODEM_LPCON_I2C_MST_CLK_CONF_REG) s_mock_lpcon_i2c_mst = val;
    else if (a == (uintptr_t)MODEM_LPCON_CLK_CONF_FORCE_ON_REG) s_mock_lpcon_fo = val;
    else if (a == (uintptr_t)MODEM_LPCON_RST_CONF_REG) s_mock_lpcon_rst = val;
    else if (a == (uintptr_t)MODEM_LPCON_MEM_CONF_REG) s_mock_lpcon_mem = val;
    else if (a == (uintptr_t)LP_PERI_CLK_EN_REG) s_mock_lp_peri_clk_en = val;
    else if (a == (uintptr_t)LP_PERI_RESET_EN_REG) s_mock_lp_peri_rst_en = val;
    else if (a == (uintptr_t)LP_I2C_ANA_MST_DEVICE_EN_REG) s_mock_lp_i2c_ana_mst_dev = val;
    else if (a == (uintptr_t)MODEM_SYSCON_DATE_REG) s_mock_modem_syscon_date = val;
    else if (a == (uintptr_t)MODEM_LPCON_DATE_REG) s_mock_modem_lpcon_date = val;
    else if (a == (uintptr_t)IEEE802154_COMMAND_REG) s_mock_ieee802154_command = val;
    else if (a == (uintptr_t)IEEE802154_CTRL_CFG_REG) s_mock_ieee802154_ctrl_cfg = val;
    else if (a == (uintptr_t)PMU_HP_ACTIVE_ICG_MODEM_REG) s_mock_pmu_hp_active_icg = val;
    else if (a == (uintptr_t)PMU_IMM_MODEM_ICG_REG) s_mock_pmu_imm_modem_icg = val;
    else if (a == (uintptr_t)PMU_IMM_SLEEP_SYSCLK_REG) s_mock_pmu_imm_sleep_sysclk = val;
    else if (a == (uintptr_t)PMU_IMM_HP_CK_POWER_REG) s_mock_pmu_imm_hp_ck_power = val;
    else if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF_POWER_ST_REG) s_mock_syscon_pwr_st = val;
    else if (a == (uintptr_t)MODEM_LPCON_CLK_CONF_POWER_ST_REG) s_mock_lpcon_pwr_st = val;
    else if (a == (uintptr_t)LP_ANA_PERI_PWR_CONF_REG) s_mock_lp_ana_peri_pwr = (val & LP_ANA_PERI_PWR_ENABLE_BIT);
    else if (a == (uintptr_t)LP_ANA_PERI_CLK_CONF_REG) s_mock_lp_ana_peri_clk = val;
    else if (a == (uintptr_t)MODEM_RF_ENABLE_REG) s_mock_modem_rf_enable = val;
    else if (a == (uintptr_t)MODEM_RF_AGC_CTRL_REG) s_mock_modem_rf_agc_ctrl = val;
    else if (a == (uintptr_t)LP_CLKRST_I2C_ANA_MST_LINK0_REG) s_mock_i2c_ana_link0 = val;
    else if (a == (uintptr_t)LP_CLKRST_I2C_ANA_MST_LINK1_REG) s_mock_i2c_ana_link1 = val;
    else if (a == (uintptr_t)MODEM_RF_ANALOG_SWITCH0_REG) s_mock_modem_rf_analog_sw0 = val;
    else if (a == (uintptr_t)MODEM_RF_ANALOG_SWITCH1_REG) s_mock_modem_rf_analog_sw1 = val;
}

static inline void reg_set_bits(volatile uint32_t *addr, uint32_t mask)
{
    reg_write(addr, reg_read(addr) | mask);
}

static inline void reg_clear_bits(volatile uint32_t *addr, uint32_t mask)
{
    reg_write(addr, reg_read(addr) & ~mask);
}

#endif /* __riscv */

/* ========================================================================= */
/* Subsystem Lifecycle Initialization                                        */
/* ========================================================================= */

modem_status_t modem_init(void)
{
    /* 1. Configure PMU HP Active Modem ICG code and disable hardware clock gating */
    reg_write(PMU_HP_ACTIVE_ICG_MODEM_REG,
              PMU_HP_ACTIVE_ICG_MODEM_HP_ACTIVE_DIG_ICG_MODEM_CODE_V(PMU_HP_ICG_MODEM_CODE_ACTIVE));
    reg_write(MODEM_SYSCON_CLK_CONF_POWER_ST_REG, MODEM_SYSCON_CLK_CONF_POWER_ST_ACTIVE_ALL);
    reg_write(MODEM_LPCON_CLK_CONF_POWER_ST_REG, MODEM_LPCON_CLK_CONF_POWER_ST_ACTIVE_ALL);

    /* 2. Software trigger force update modem ICG code and clock switch */
    reg_write(PMU_IMM_MODEM_ICG_REG, PMU_IMM_MODEM_ICG_UPDATE_DIG_ICG_MODEM_EN_M);
    reg_write(PMU_IMM_SLEEP_SYSCLK_REG, PMU_IMM_SLEEP_SYSCLK_UPDATE_DIG_ICG_SWITCH_M);

    /* 3. Tie high root analog and PLL power controls */
    reg_set_bits(PMU_IMM_HP_CK_POWER_REG,
                 PMU_IMM_HP_CK_POWER_TIE_HIGH_GLOBAL_BBPLL_ICG_M |
                 PMU_IMM_HP_CK_POWER_TIE_HIGH_GLOBAL_XTAL_ICG_M |
                 PMU_IMM_HP_CK_POWER_TIE_HIGH_XPD_BB_I2C_M |
                 PMU_IMM_HP_CK_POWER_TIE_HIGH_XPD_BBPLL_I2C_M |
                 PMU_IMM_HP_CK_POWER_TIE_HIGH_XPD_BBPLL_M |
                 PMU_IMM_HP_CK_POWER_TIE_HIGH_XPD_XTAL_M);

    /* 4. Enable PCR Modem APB peripheral clock and release hardware reset */
    reg_set_bits(PCR_MODEM_APB_CONF_REG, PCR_MODEM_APB_CLK_EN_BIT);
    reg_clear_bits(PCR_MODEM_APB_CONF_REG, PCR_MODEM_RST_EN_BIT);

    /* 5. Configure RF Coexistence Low-Power Clock (Select XTAL source) */
    reg_set_bits(MODEM_LPCON_COEX_LP_CLK_CONF_REG, MODEM_LPCON_CLK_COEX_LP_SEL_XTAL_BIT);
    reg_set_bits(MODEM_LPCON_CLK_CONF_REG, MODEM_LPCON_CLK_COEX_EN_BIT);

    /* 6. Enable common modem security accelerator and base BLE timer clocks */
    modem_enable_i2c_ana_mst();
    reg_set_bits(MODEM_SYSCON_CLK_CONF_REG,
                 MODEM_CLK_MODEM_SEC_EN_BIT |
                 MODEM_CLK_MODEM_SEC_APB_EN_BIT |
                 MODEM_CLK_BLE_TIMER_EN_BIT);

    /* 7. Settling delay for clock distribution network */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 8. Release resets for security and base timer peripherals */
    reg_clear_bits(MODEM_SYSCON_MODEM_RST_CONF_REG,
                   MODEM_RST_BLE_TIMER_BIT |
                   MODEM_RST_MODEM_SEC_BIT);

    /* 9. Enable Analog RF Synthesizer and LP_ANALOG_PERI power domains */
    modem_enable_rf_synthesizer();

    /* 10. Prime APB SAR ADC DC offset calibration (Task 4) */
    modem_prime_sar_adc_calibration();

    /* 11. Update internal telemetry state */
    s_modem_state.wifi_clk_enabled       = 0U;
    s_modem_state.ble_clk_enabled        = 1U;
    s_modem_state.ieee802154_clk_enabled = 0U;
    s_modem_state.coexistence_enabled    = 1U;

    s_modem_initialized = true;
    return MODEM_OK;
}

/* ========================================================================= */
/* Wi-Fi Subsystem Clock Controls                                            */
/* ========================================================================= */

modem_status_t modem_enable_wifi_clocks(void)
{
    if (!s_modem_initialized)
    {
        modem_init();
    }

    /* 1. Ensure PMU active modem ICG and clock power state maps remain un-gated */
    reg_write(PMU_HP_ACTIVE_ICG_MODEM_REG,
              PMU_HP_ACTIVE_ICG_MODEM_HP_ACTIVE_DIG_ICG_MODEM_CODE_V(PMU_HP_ICG_MODEM_CODE_ACTIVE));
    reg_write(MODEM_SYSCON_CLK_CONF_POWER_ST_REG, MODEM_SYSCON_CLK_CONF_POWER_ST_ACTIVE_ALL);
    reg_write(MODEM_LPCON_CLK_CONF_POWER_ST_REG, MODEM_LPCON_CLK_CONF_POWER_ST_ACTIVE_ALL);
    reg_write(PMU_IMM_MODEM_ICG_REG, PMU_IMM_MODEM_ICG_UPDATE_DIG_ICG_MODEM_EN_M);
    reg_write(PMU_IMM_SLEEP_SYSCLK_REG, PMU_IMM_SLEEP_SYSCLK_UPDATE_DIG_ICG_SWITCH_M);

    /* 2. Power up RF and Baseband memories in MODEM_LPCON */
    reg_set_bits(MODEM_LPCON_MEM_CONF_REG,
                 MODEM_LPCON_MEM_CHAN_FREQ_PU_BIT |
                 MODEM_LPCON_MEM_DC_PU_BIT |
                 MODEM_LPCON_MEM_AGC_PU_BIT |
                 MODEM_LPCON_MEM_PBUS_PU_BIT |
                 MODEM_LPCON_MEM_BC_PU_BIT |
                 MODEM_LPCON_MEM_I2C_MST_PU_BIT);
    reg_clear_bits(MODEM_LPCON_MEM_CONF_REG,
                   MODEM_LPCON_MEM_CHAN_FREQ_PD_BIT |
                   MODEM_LPCON_MEM_DC_PD_BIT |
                   MODEM_LPCON_MEM_AGC_PD_BIT |
                   MODEM_LPCON_MEM_PBUS_PD_BIT |
                   MODEM_LPCON_MEM_BC_PD_BIT |
                   MODEM_LPCON_MEM_I2C_MST_PD_BIT);

    /* 3. Force-on modem memory clocks and power in MODEM_LPCON */
    reg_set_bits(MODEM_LPCON_CLK_CONF_FORCE_ON_REG,
                 MODEM_LPCON_CLK_WIFIPWR_FO_BIT |
                 MODEM_LPCON_CLK_COEX_FO_BIT |
                 MODEM_LPCON_CLK_BCMEM_FO_BIT |
                 MODEM_LPCON_CLK_CHAN_FREQ_MEM_FO_BIT |
                 MODEM_LPCON_CLK_PBUS_MEM_FO_BIT |
                 MODEM_LPCON_CLK_AGC_MEM_FO_BIT |
                 MODEM_LPCON_CLK_DC_MEM_FO_BIT |
                 MODEM_LPCON_CLK_I2C_MST_FO_BIT |
                 MODEM_LPCON_CLK_I2C_MST_MEM_FO_BIT);

    /* 4. Enable Wi-Fi power and Coex clock in MODEM_LPCON */
    reg_set_bits(MODEM_LPCON_CLK_CONF_REG,
                 MODEM_LPCON_CLK_WIFIPWR_EN_BIT |
                 MODEM_LPCON_CLK_COEX_EN_BIT);

    /* 5. Release resets in MODEM_LPCON */
    reg_clear_bits(MODEM_LPCON_RST_CONF_REG,
                   MODEM_LPCON_RST_WIFIPWR_BIT |
                   MODEM_LPCON_RST_COEX_BIT |
                   MODEM_LPCON_RST_I2C_MST_BIT);

    /* 6. Enable analog I2C master for RF / transceiver calibration */
    modem_enable_i2c_ana_mst();

    /* 7. Assert all Wi-Fi MAC, APB, Baseband, and Front-End (FE) clocks in MODEM_SYSCON */
    reg_set_bits(MODEM_SYSCON_CLK_CONF1_REG,
                 MODEM_CLK_WIFI_APB_EN_BIT |
                 MODEM_CLK_WIFIMAC_EN_BIT |
                 MODEM_CLK_WIFIBB_ALL_EN_MASK |
                 MODEM_CLK_FE_ALL_EN_MASK);

    /* 8. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 9. Clear reset bits in MODEM_SYSCON (Wi-Fi MAC, Baseband, and Front-End) */
    reg_clear_bits(MODEM_SYSCON_MODEM_RST_CONF_REG,
                   MODEM_RST_WIFIMAC_BIT |
                   MODEM_RST_WIFIBB_BIT |
                   MODEM_RST_FE_BIT);

    /* 10. Prime APB SAR ADC DC offset calibration (Task 4) */
    modem_prime_sar_adc_calibration();

    s_modem_state.wifi_clk_enabled = 1U;
    return MODEM_OK;
}

modem_status_t modem_enable_i2c_ana_mst(void)
{
    /* 1. Enable LP_PERI Analog I2C clock and release reset */
    reg_set_bits(LP_PERI_CLK_EN_REG, LP_PERI_CLK_LP_ANA_I2C_BIT);
    reg_clear_bits(LP_PERI_RESET_EN_REG, LP_PERI_RST_LP_ANA_I2C_BIT);

    /* 2. Configure MODEM_LPCON I2C Master clock (160 MHz source) */
    reg_set_bits(MODEM_LPCON_I2C_MST_CLK_CONF_REG, MODEM_LPCON_I2C_MST_SEL_160M_BIT);
    reg_set_bits(MODEM_LPCON_CLK_CONF_REG, MODEM_LPCON_CLK_I2C_MST_EN_BIT);
    reg_set_bits(MODEM_LPCON_CLK_CONF_FORCE_ON_REG,
                 MODEM_LPCON_CLK_I2C_MST_FO_BIT | MODEM_LPCON_CLK_I2C_MST_MEM_FO_BIT);

    /* 3. Power up I2C master memory and clear power down */
    reg_set_bits(MODEM_LPCON_MEM_CONF_REG, MODEM_LPCON_MEM_I2C_MST_PU_BIT);
    reg_clear_bits(MODEM_LPCON_MEM_CONF_REG, MODEM_LPCON_MEM_I2C_MST_PD_BIT);

    /* 4. Release MODEM_LPCON I2C Master reset */
    reg_clear_bits(MODEM_LPCON_RST_CONF_REG, MODEM_LPCON_RST_I2C_MST_BIT);

    /* 5. Enable all analog I2C devices in LP_I2C_ANA_MST */
    reg_write(LP_I2C_ANA_MST_DEVICE_EN_REG, LP_I2C_ANA_MST_ALL_DEVICES_EN);

    /* 6. Configure I2C Analog Master bus links in LP_CLKRST (0x600B0418-041C, Task 4) */
    reg_write(LP_CLKRST_I2C_ANA_MST_LINK0_REG, LP_CLKRST_I2C_ANA_MST_LINK_VAL);
    reg_write(LP_CLKRST_I2C_ANA_MST_LINK1_REG, LP_CLKRST_I2C_ANA_MST_LINK_VAL);

    /* 7. Enable RF front-end devices in I2C_ANA_MST (0x600AF800, Task 4) */
    reg_write(I2C_ANA_MST_DEVICE_EN_REG, I2C_ANA_MST_DEVICE_EN_ALL);

    /* 8. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    return MODEM_OK;
}

modem_status_t modem_enable_rf_synthesizer(void)
{
    /* 1. Configure and power up analog peripherals via LP_ANALOG_PERI (0x600B2C00) */
    reg_write(LP_ANA_PERI_PWR_CONF_REG, LP_ANA_PERI_PWR_ENABLE_VAL);
    reg_write(LP_ANA_PERI_CLK_CONF_REG, LP_ANA_PERI_CLK_ENABLE_VAL);

    /* 2. Assert Master RF Enable bit in MODEM_RF block (0x600A7104) */
    reg_set_bits(MODEM_RF_ENABLE_REG, MODEM_RF_ENABLE_MASTER_BIT);

    /* 3. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    s_rf_synth_enabled = true;
    return MODEM_OK;
}

bool modem_is_rf_synth_enabled(void)
{
    return s_rf_synth_enabled && ((reg_read(MODEM_RF_ENABLE_REG) & MODEM_RF_ENABLE_MASTER_BIT) != 0U);
}

modem_status_t modem_configure_i2c_analog_master(void)
{
    return modem_enable_i2c_ana_mst();
}

modem_status_t modem_prime_sar_adc_calibration(void)
{
    /* Prime APB SAR ADC DC offset calibration state machine by reading 0x6000E0D0 - 0x6000E0FC (Task 4) */
    for (uint32_t i = 0U; i < APB_SARADC_CAL_REG_COUNT; i++)
    {
        (void)reg_read(APB_SARADC_CAL_REG(i));
    }
    s_sar_adc_cal_primed = true;
    return MODEM_OK;
}

modem_status_t modem_disable_wifi_clocks(void)
{
    /* 1. Assert hardware resets */
    reg_set_bits(MODEM_SYSCON_MODEM_RST_CONF_REG,
                 MODEM_RST_WIFIMAC_BIT |
                 MODEM_RST_WIFIBB_BIT |
                 MODEM_RST_FE_BIT);

    /* 2. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 3. Gate off clock enable bits */
    reg_clear_bits(MODEM_SYSCON_CLK_CONF1_REG,
                   MODEM_CLK_WIFI_APB_EN_BIT |
                   MODEM_CLK_WIFIMAC_EN_BIT |
                   MODEM_CLK_WIFIBB_ALL_EN_MASK |
                   MODEM_CLK_FE_ALL_EN_MASK);

    s_modem_state.wifi_clk_enabled = 0U;
    return MODEM_OK;
}

/* ========================================================================= */
/* Bluetooth 5 (LE) Subsystem Clock Controls                                 */
/* ========================================================================= */

modem_status_t modem_enable_ble_clocks(void)
{
    if (!s_modem_initialized)
    {
        modem_init();
    }

    /* 1. Assert BLE timer and Bluetooth APB clocks */
    reg_set_bits(MODEM_SYSCON_CLK_CONF_REG, MODEM_CLK_BLE_TIMER_EN_BIT);
    reg_set_bits(MODEM_SYSCON_CLK_CONF1_REG,
                 MODEM_CLK_BT_APB_EN_BIT |
                 MODEM_CLK_BT_EN_BIT);

    /* 2. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 3. Clear Bluetooth subsystem resets */
    reg_clear_bits(MODEM_SYSCON_MODEM_RST_CONF_REG,
                   MODEM_RST_BLE_TIMER_BIT |
                   MODEM_RST_BTMAC_BIT |
                   MODEM_RST_BTMAC_APB_BIT |
                   MODEM_RST_BTBB_BIT |
                   MODEM_RST_BTBB_APB_BIT);

    s_modem_state.ble_clk_enabled = 1U;
    return MODEM_OK;
}

modem_status_t modem_disable_ble_clocks(void)
{
    /* 1. Assert Bluetooth subsystem resets */
    reg_set_bits(MODEM_SYSCON_MODEM_RST_CONF_REG,
                 MODEM_RST_BLE_TIMER_BIT |
                 MODEM_RST_BTMAC_BIT |
                 MODEM_RST_BTMAC_APB_BIT |
                 MODEM_RST_BTBB_BIT |
                 MODEM_RST_BTBB_APB_BIT);

    /* 2. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 3. Gate off clock enable bits */
    reg_clear_bits(MODEM_SYSCON_CLK_CONF1_REG,
                   MODEM_CLK_BT_APB_EN_BIT |
                   MODEM_CLK_BT_EN_BIT);

    s_modem_state.ble_clk_enabled = 0U;
    return MODEM_OK;
}

/* ========================================================================= */
/* IEEE 802.15.4 (Zigbee / Thread) Subsystem Clock Controls                  */
/* ========================================================================= */

modem_status_t modem_enable_ieee802154_clocks(void)
{
    if (!s_modem_initialized)
    {
        modem_init();
    }

    /* 1. Assert IEEE 802.15.4 MAC and APB clock enables */
    reg_set_bits(MODEM_SYSCON_CLK_CONF_REG,
                 MODEM_CLK_ZB_MAC_EN_BIT |
                 MODEM_CLK_ZB_APB_EN_BIT);

    reg_set_bits(MODEM_SYSCON_CLK_CONF_FORCE_ON_REG,
                 MODEM_CLK_ZB_MAC_FO_BIT |
                 MODEM_CLK_ZB_APB_FO_BIT);

    /* 2. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 3. Clear Zigbee/Thread MAC reset */
    reg_clear_bits(MODEM_SYSCON_MODEM_RST_CONF_REG, MODEM_RST_ZBMAC_BIT);

    s_modem_state.ieee802154_clk_enabled = 1U;
    return MODEM_OK;
}

modem_status_t modem_disable_ieee802154_clocks(void)
{
    /* 1. Assert Zigbee/Thread MAC reset */
    reg_set_bits(MODEM_SYSCON_MODEM_RST_CONF_REG, MODEM_RST_ZBMAC_BIT);

    /* 2. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 3. Gate off clock enable and force-on bits */
    reg_clear_bits(MODEM_SYSCON_CLK_CONF_REG,
                   MODEM_CLK_ZB_MAC_EN_BIT |
                   MODEM_CLK_ZB_APB_EN_BIT);

    reg_clear_bits(MODEM_SYSCON_CLK_CONF_FORCE_ON_REG,
                   MODEM_CLK_ZB_MAC_FO_BIT |
                   MODEM_CLK_ZB_APB_FO_BIT);

    s_modem_state.ieee802154_clk_enabled = 0U;
    return MODEM_OK;
}

/* ========================================================================= */
/* Orchestrated Wireless Subsystem Clock & Power Sequence (TEST 29 Stimulus)  */
/* ========================================================================= */

modem_status_t modem_enable_all_clocks(void)
{
    /* Step 0: Ensure PCR modem APB clock is enabled */
    reg_set_bits(PCR_MODEM_APB_CONF_REG, PCR_MODEM_APB_CLK_EN_BIT);
    reg_clear_bits(PCR_MODEM_APB_CONF_REG, PCR_MODEM_RST_EN_BIT);

    /* Step 1: Write MODEM_SYSCON_CLK_CONF_REG (bits 30, 29, 28, 24, 23) */
    reg_set_bits(MODEM_SYSCON_CLK_CONF_REG,
                 MODEM_CLK_BLE_TIMER_EN_BIT |
                 MODEM_CLK_MODEM_SEC_EN_BIT |
                 MODEM_CLK_MODEM_SEC_APB_EN_BIT |
                 MODEM_CLK_ZB_MAC_EN_BIT |
                 MODEM_CLK_ZB_APB_EN_BIT);

    /* Step 2: Write MODEM_SYSCON_CLK_CONF1_REG (bits 18, 17, 10, 9) */
    reg_set_bits(MODEM_SYSCON_CLK_CONF1_REG,
                 MODEM_CLK_BT_EN_BIT |
                 MODEM_CLK_BT_APB_EN_BIT |
                 MODEM_CLK_WIFI_APB_EN_BIT |
                 MODEM_CLK_WIFIMAC_EN_BIT);

    /* Step 3: Settling delay (at least 1000 CPU cycles) */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* Step 4: Clear reset bits 30, 24, 10, 8 in MODEM_SYSCON_MODEM_RST_CONF_REG */
    reg_clear_bits(MODEM_SYSCON_MODEM_RST_CONF_REG,
                   MODEM_RST_BLE_TIMER_BIT |
                   MODEM_RST_ZBMAC_BIT |
                   MODEM_RST_WIFIMAC_BIT |
                   MODEM_RST_WIFIBB_BIT |
                   MODEM_RST_MODEM_SEC_BIT |
                   MODEM_RST_BTMAC_BIT |
                   MODEM_RST_BTMAC_APB_BIT |
                   MODEM_RST_BTBB_BIT |
                   MODEM_RST_BTBB_APB_BIT);

    /* Step 5: Write MODEM_LPCON_COEX_LP_CLK_CONF_REG to set bit 2 (CLK_COEX_LP_SEL_XTAL) */
    reg_set_bits(MODEM_LPCON_COEX_LP_CLK_CONF_REG, MODEM_LPCON_CLK_COEX_LP_SEL_XTAL_BIT);
    reg_set_bits(MODEM_LPCON_CLK_CONF_REG, MODEM_LPCON_CLK_COEX_EN_BIT);

    /* Step 6: Enable Analog RF Synthesizer and LP_ANALOG_PERI power domains */
    modem_enable_rf_synthesizer();

    /* Update internal state flags */
    s_modem_state.wifi_clk_enabled       = 1U;
    s_modem_state.ble_clk_enabled        = 1U;
    s_modem_state.ieee802154_clk_enabled = 1U;
    s_modem_state.coexistence_enabled    = 1U;

    s_modem_initialized = true;
    return MODEM_OK;
}

/* ========================================================================= */
/* Coexistence Low-Power Clock Controls                                      */
/* ========================================================================= */

modem_status_t modem_enable_coexistence(void)
{
    reg_set_bits(MODEM_LPCON_COEX_LP_CLK_CONF_REG, MODEM_LPCON_CLK_COEX_LP_SEL_XTAL_BIT);
    reg_set_bits(MODEM_LPCON_CLK_CONF_REG, MODEM_LPCON_CLK_COEX_EN_BIT);
    s_modem_state.coexistence_enabled = 1U;
    return MODEM_OK;
}

modem_status_t modem_disable_coexistence(void)
{
    reg_clear_bits(MODEM_LPCON_CLK_CONF_REG, MODEM_LPCON_CLK_COEX_EN_BIT);
    s_modem_state.coexistence_enabled = 0U;
    return MODEM_OK;
}

/* ========================================================================= */
/* Telemetry and State Queries                                               */
/* ========================================================================= */

modem_status_t modem_get_clock_state(modem_clock_state_t *state)
{
    if (state == NULL)
    {
        return MODEM_ERR_INVALID_ARG;
    }

    *state = s_modem_state;
    return MODEM_OK;
}

uint32_t modem_get_syscon_date(void)
{
    return reg_read(MODEM_SYSCON_DATE_REG);
}

uint32_t modem_get_lpcon_date(void)
{
    return reg_read(MODEM_LPCON_DATE_REG);
}

uint32_t modem_get_rf_enable_reg(void)
{
    return reg_read(MODEM_RF_ENABLE_REG);
}

uint32_t modem_get_lp_ana_peri_pwr_reg(void)
{
    return reg_read(LP_ANA_PERI_PWR_CONF_REG);
}

uint32_t modem_get_lp_ana_peri_clk_reg(void)
{
    return reg_read(LP_ANA_PERI_CLK_CONF_REG);
}

bool modem_is_wifi_enabled(void)
{
    return (s_modem_state.wifi_clk_enabled != 0U);
}

bool modem_is_ble_enabled(void)
{
    return (s_modem_state.ble_clk_enabled != 0U);
}

bool modem_is_ieee802154_enabled(void)
{
    return (s_modem_state.ieee802154_clk_enabled != 0U);
}

bool modem_is_coex_enabled(void)
{
    return (s_modem_state.coexistence_enabled != 0U);
}

uint32_t modem_get_i2c_ana_mst_link0_reg(void)
{
    return reg_read(LP_CLKRST_I2C_ANA_MST_LINK0_REG);
}

uint32_t modem_get_i2c_ana_mst_link1_reg(void)
{
    return reg_read(LP_CLKRST_I2C_ANA_MST_LINK1_REG);
}

bool modem_is_sar_adc_cal_primed(void)
{
    return s_sar_adc_cal_primed;
}

/* ========================================================================= */
/* Bare-Metal Wi-Fi RX AGC Override                                          */
/* ========================================================================= */

void modem_force_rx_agc(void)
{
#if defined(__riscv)
    uint32_t val = *MODEM_RF_AGC_CTRL_REG;
    *MODEM_RF_AGC_CTRL_REG = val | MODEM_RF_AGC_OPT_EN_MASK;
    asm volatile("fence" ::: "memory");
    *MODEM_RF_AGC_CTRL_REG |= MODEM_RF_AGC_EN_BIT;
    asm volatile("fence" ::: "memory");
#else
    uint32_t val = reg_read(MODEM_RF_AGC_CTRL_REG);
    reg_write(MODEM_RF_AGC_CTRL_REG, val | MODEM_RF_AGC_OPT_EN_MASK);
    modem_fence();
    reg_set_bits(MODEM_RF_AGC_CTRL_REG, MODEM_RF_AGC_EN_BIT);
    modem_fence();
#endif
}

/* ========================================================================= */
/* Bare-Metal RF Front-End Analog Routing & TX Power Activation (Task 5.7.3) */
/* ========================================================================= */

#if defined(__riscv)
extern void tx_paon_set_new(void);
extern void open_i2c_xpd_new(uint32_t mode);
extern void noise_floor_auto_set_new(void);
#endif

static uint32_t s_rf_analog_sw0 = 0U;
static uint32_t s_rf_analog_sw1 = 0U;

void modem_rf_analog_init(void)
{
    /* Enable internal RF front-end antenna switch routing for TX/RX */
    s_rf_analog_sw0 = MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG;
    s_rf_analog_sw1 = MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG;
    reg_write(MODEM_RF_ANALOG_SWITCH0_REG, s_rf_analog_sw0);
    reg_write(MODEM_RF_ANALOG_SWITCH1_REG, s_rf_analog_sw1);
    modem_fence();
}

void modem_force_tx_pa(void)
{
    modem_rf_analog_init();

#if defined(__riscv)
    /* Open internal I2C analog front-end bus links */
    open_i2c_xpd_new(0U);

    /* Assert Power Amplifier bias and active TX configuration */
    tx_paon_set_new();

    /* Recalibrate noise floor and DC offsets */
    noise_floor_auto_set_new();
#endif

    modem_fence();
}

uint32_t modem_get_rf_analog_switch0(void)
{
    return s_rf_analog_sw0;
}

uint32_t modem_get_rf_analog_switch1(void)
{
    return s_rf_analog_sw1;
}


