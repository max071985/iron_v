/*
 * tests/test_challenger_t1_2_svd.c
 *
 * Empirical Challenger 2 Verification Harness for Task 1:
 * - Adversarial SVD / TRM hardware register map alignment audit
 * - Base address verification:
 *     I2C_ANA_MST (0x600AF800)
 *     MODEM_LPCON (0x600AF000)
 *     PMU_IMM_HP_CK_POWER_REG (0x600B00CC)
 *     MODEM_FE (0x600A0000)
 * - Register offsets 0x0000..0x0034 in I2C_ANA_MST
 * - 480 MHz multiplier equation: 40 MHz * (8 + 4) = 480 MHz
 * - Bitfield precision, transaction packing, and error paths
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "regs/i2c_ana.h"
#include "regs/modem_rf_analog.h"
#include "regs/pmu.h"
#include "modem.h"

static int s_total_assertions = 0;
static int s_passed_assertions = 0;
static int s_failed_assertions = 0;

#define CHALLENGER_ASSERT(cond, msg) do { \
    s_total_assertions++; \
    if (cond) { \
        s_passed_assertions++; \
    } else { \
        s_failed_assertions++; \
        printf("  [CHALLENGE FAIL] Line %d: %s\n", __LINE__, msg); \
    } \
} while (0)

/* ========================================================================= */
/* Test 1: Authoritative Base Addresses Verification                         */
/* ========================================================================= */
static void verify_hardware_base_addresses(void)
{
    printf("[CHALLENGER 2] 1. Verifying Authoritative Hardware Base Addresses...\n");

    /* 1. I2C_ANA_MST: 0x600AF800 */
    CHALLENGER_ASSERT(I2C_ANA_MST_BASE_ADDR == 0x600AF800U, "I2C_ANA_MST_BASE_ADDR == 0x600AF800");

    /* 2. MODEM_LPCON: 0x600AF000 */
    CHALLENGER_ASSERT(MODEM_LPCON_BASE_ADDR == 0x600AF000U, "MODEM_LPCON_BASE_ADDR == 0x600AF000");

    /* 3. PMU Base & IMM_HP_CK_POWER_REG: 0x600B0000 / 0x600B00CC */
    CHALLENGER_ASSERT(PMU_BASE == 0x600B0000U, "PMU_BASE == 0x600B0000");
    CHALLENGER_ASSERT((uintptr_t)PMU_IMM_HP_CK_POWER_REG == 0x600B00CCU, "PMU_IMM_HP_CK_POWER_REG == 0x600B00CC");

    /* 4. MODEM_FE: 0x600A0000 */
    CHALLENGER_ASSERT(MODEM_FE_BASE_ADDR == 0x600A0000U, "MODEM_FE_BASE_ADDR == 0x600A0000");

    /* 5. MODEM_FE_FREQ_STATUS_REG: 0x600A00CC */
    CHALLENGER_ASSERT((uintptr_t)MODEM_FE_FREQ_STATUS_REG == 0x600A00CCU, "MODEM_FE_FREQ_STATUS_REG == 0x600A00CC");
    CHALLENGER_ASSERT(MODEM_FE_FREQ_LOCK_BIT == (1U << 8), "MODEM_FE_FREQ_LOCK_BIT == (1 << 8)");

    /* 6. Other relevant peripheral bases */
    CHALLENGER_ASSERT(PCR_BASE_ADDR == 0x60096000U, "PCR_BASE_ADDR == 0x60096000");
    CHALLENGER_ASSERT(MODEM_SYSCON_BASE_ADDR == 0x600A9800U, "MODEM_SYSCON_BASE_ADDR == 0x600A9800");
    CHALLENGER_ASSERT(IEEE802154_BASE_ADDR == 0x600A3000U, "IEEE802154_BASE_ADDR == 0x600A3000");
    CHALLENGER_ASSERT(MODEM_RF_ANALOG_BASE_ADDR == 0x600AA000U, "MODEM_RF_ANALOG_BASE_ADDR == 0x600AA000");
}

/* ========================================================================= */
/* Test 2: Register Offsets 0x0000..0x0034 in I2C_ANA_MST                    */
/* ========================================================================= */
static void verify_i2c_ana_mst_register_offsets(void)
{
    printf("[CHALLENGER 2] 2. Verifying I2C_ANA_MST Register Offsets 0x0000..0x0034...\n");

    struct {
        uint32_t offset;
        volatile uint32_t *reg_ptr;
        uint32_t expected_addr;
        const char *name;
    } i2c_regs[] = {
        { I2C_ANA_MST_I2C0_CTRL_OFFSET,    I2C_ANA_MST_I2C0_CTRL_REG,    0x600AF800U, "I2C0_CTRL" },
        { I2C_ANA_MST_I2C1_CTRL_OFFSET,    I2C_ANA_MST_I2C1_CTRL_REG,    0x600AF804U, "I2C1_CTRL" },
        { I2C_ANA_MST_I2C0_CONF_OFFSET,    I2C_ANA_MST_I2C0_CONF_REG,    0x600AF808U, "I2C0_CONF" },
        { I2C_ANA_MST_I2C1_CONF_OFFSET,    I2C_ANA_MST_I2C1_CONF_REG,    0x600AF80CU, "I2C1_CONF" },
        { I2C_ANA_MST_BURST_CONF_OFFSET,   I2C_ANA_MST_BURST_CONF_REG,   0x600AF810U, "BURST_CONF" },
        { I2C_ANA_MST_BURST_STATUS_OFFSET, I2C_ANA_MST_BURST_STATUS_REG, 0x600AF814U, "BURST_STATUS" },
        { I2C_ANA_MST_ANA_CONF0_OFFSET,    I2C_ANA_MST_ANA_CONF0_REG,    0x600AF818U, "ANA_CONF0" },
        { I2C_ANA_MST_ANA_CONF1_OFFSET,    I2C_ANA_MST_ANA_CONF1_REG,    0x600AF81CU, "ANA_CONF1" },
        { I2C_ANA_MST_ANA_CONF2_OFFSET,    I2C_ANA_MST_ANA_CONF2_REG,    0x600AF820U, "ANA_CONF2" },
        { I2C_ANA_MST_I2C0_CTRL1_OFFSET,   I2C_ANA_MST_I2C0_CTRL1_REG,   0x600AF824U, "I2C0_CTRL1" },
        { I2C_ANA_MST_I2C1_CTRL1_OFFSET,   I2C_ANA_MST_I2C1_CTRL1_REG,   0x600AF828U, "I2C1_CTRL1" },
        { I2C_ANA_MST_DATE_OFFSET,         I2C_ANA_MST_DATE_REG,         0x600AF834U, "DATE" },
    };

    size_t count = sizeof(i2c_regs) / sizeof(i2c_regs[0]);
    for (size_t i = 0; i < count; i++)
    {
        uint32_t calc_addr = I2C_ANA_MST_BASE_ADDR + i2c_regs[i].offset;
        CHALLENGER_ASSERT(calc_addr == i2c_regs[i].expected_addr, "Calculated offset matches expected address");
        CHALLENGER_ASSERT((uintptr_t)i2c_regs[i].reg_ptr == i2c_regs[i].expected_addr, "Register pointer matches expected address");
        CHALLENGER_ASSERT((uintptr_t)I2C_ANA_MST_REG(i2c_regs[i].offset) == i2c_regs[i].expected_addr, "Macro I2C_ANA_MST_REG matches address");
    }

    /* Verify channel accessors */
    CHALLENGER_ASSERT(I2C_ANA_MST_I2C_CTRL_REG(0) == I2C_ANA_MST_I2C0_CTRL_REG, "I2C_CTRL_REG(0) == I2C0_CTRL_REG");
    CHALLENGER_ASSERT(I2C_ANA_MST_I2C_CTRL_REG(1) == I2C_ANA_MST_I2C1_CTRL_REG, "I2C_CTRL_REG(1) == I2C1_CTRL_REG");
    CHALLENGER_ASSERT(I2C_ANA_MST_I2C_CONF_REG(0) == I2C_ANA_MST_I2C0_CONF_REG, "I2C_CONF_REG(0) == I2C0_CONF_REG");
    CHALLENGER_ASSERT(I2C_ANA_MST_I2C_CONF_REG(1) == I2C_ANA_MST_I2C1_CONF_REG, "I2C_CONF_REG(1) == I2C1_CONF_REG");
    CHALLENGER_ASSERT(I2C_ANA_MST_I2C_CTRL1_REG(0) == I2C_ANA_MST_I2C0_CTRL1_REG, "I2C_CTRL1_REG(0) == I2C0_CTRL1_REG");
    CHALLENGER_ASSERT(I2C_ANA_MST_I2C_CTRL1_REG(1) == I2C_ANA_MST_I2C1_CTRL1_REG, "I2C_CTRL1_REG(1) == I2C1_CTRL1_REG");

    /* Verify backward compatibility alias */
    CHALLENGER_ASSERT(I2C_ANA_MST_DEVICE_EN_OFFSET == 0x0014U, "I2C_ANA_MST_DEVICE_EN_OFFSET alias == 0x0014");
    CHALLENGER_ASSERT(I2C_ANA_MST_DEVICE_EN_REG == I2C_ANA_MST_BURST_STATUS_REG, "DEVICE_EN_REG alias == BURST_STATUS_REG");
}

/* ========================================================================= */
/* Test 3: BBPLL 480 MHz Multiplier Equation & Bitfields                     */
/* ========================================================================= */
static void verify_bbpll_multiplier_equation(void)
{
    printf("[CHALLENGER 2] 3. Verifying BBPLL 480 MHz Multiplier Equation...\n");

    /* 
     * BBPLL Analog Frequency Synthesis Mathematical Proof:
     * On ESP32-C6, the BBPLL is an internal analog PLL.
     * With reference crystal f_xtal = 40 MHz:
     *   f_ref = f_xtal / (div_ref + 1)
     *   With div_ref = 0 (I2C_BBPLL_OC_REF_DIV_40M = 0):
     *     f_ref = 40 MHz / 1 = 40 MHz
     *   Multiplier factor: M = (div7_0 + 4)
     *   With div7_0 = 8 (I2C_BBPLL_OC_DIV_REG_VAL = 8):
     *     M = 8 + 4 = 12
     *   f_bbpll = f_ref * M = 40 MHz * 12 = 480 MHz!
     */
    uint32_t f_xtal = 40U; /* 40 MHz */
    uint32_t div_ref = I2C_BBPLL_OC_REF_DIV_40M;
    uint32_t div7_0 = I2C_BBPLL_OC_DIV_REG_VAL;

    CHALLENGER_ASSERT(div_ref == 0U, "I2C_BBPLL_OC_REF_DIV_40M == 0");
    CHALLENGER_ASSERT(div7_0 == 8U, "I2C_BBPLL_OC_DIV_REG_VAL == 8");

    uint32_t multiplier = div7_0 + 4U;
    CHALLENGER_ASSERT(multiplier == 12U, "Multiplier factor (8 + 4) == 12");

    uint32_t f_bbpll = (f_xtal / (div_ref + 1U)) * multiplier;
    CHALLENGER_ASSERT(f_bbpll == 480U, "Synthesized frequency: 40 MHz * (8 + 4) == 480 MHz");

    /* Verify register 2: OC_REF */
    CHALLENGER_ASSERT(I2C_BBPLL_OC_REF_ADDR == 2U, "I2C_BBPLL_OC_REF_ADDR == 2");
    CHALLENGER_ASSERT(I2C_BBPLL_OC_REF_VAL == 0x50U, "I2C_BBPLL_OC_REF_VAL == 0x50 ((5 << 4) | 0)");

    /* Verify register 3: OC_DIV */
    CHALLENGER_ASSERT(I2C_BBPLL_OC_DIV_REG_ADDR == 3U, "I2C_BBPLL_OC_DIV_REG_ADDR == 3");
    CHALLENGER_ASSERT(I2C_BBPLL_OC_DIV_REG_VAL == 8U, "I2C_BBPLL_OC_DIV_REG_VAL == 8");

    /* Verify register 5: Intermediate Dividers */
    CHALLENGER_ASSERT(I2C_BBPLL_OC_DR_ADDR == 5U, "I2C_BBPLL_OC_DR_ADDR == 5");
    CHALLENGER_ASSERT(I2C_BBPLL_OC_DR1_MASK == 0x07U, "I2C_BBPLL_OC_DR1_MASK == 0x07");
    CHALLENGER_ASSERT(I2C_BBPLL_OC_DR3_MASK == 0x70U, "I2C_BBPLL_OC_DR3_MASK == 0x70");

    /* Verify register 6: REG6 */
    CHALLENGER_ASSERT(I2C_BBPLL_REG6_ADDR == 6U, "I2C_BBPLL_REG6_ADDR == 6");
    CHALLENGER_ASSERT(I2C_BBPLL_REG6_VAL == 0x73U, "I2C_BBPLL_REG6_VAL == 0x73 ((1<<6)|(3<<4)|3)");

    /* Verify register 9: VCO Dynamic Bias */
    CHALLENGER_ASSERT(I2C_BBPLL_REG9_ADDR == 9U, "I2C_BBPLL_REG9_ADDR == 9");
    CHALLENGER_ASSERT(I2C_BBPLL_OC_VCO_DBIAS_DEFAULT == 2U, "I2C_BBPLL_OC_VCO_DBIAS_DEFAULT == 2");

    /* Verify calibration bitfields */
    CHALLENGER_ASSERT(I2C_ANA_MST_CAL_DONE_BIT == (1U << 24), "CAL_DONE_BIT == (1 << 24)");
    CHALLENGER_ASSERT(I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT == (1U << 2), "STOP_FORCE_HIGH_BIT == (1 << 2)");
    CHALLENGER_ASSERT(I2C_ANA_MST_BBPLL_STOP_FORCE_LOW_BIT == (1U << 3), "STOP_FORCE_LOW_BIT == (1 << 3)");

    /* Verify PMU power bits for BBPLL */
    CHALLENGER_ASSERT(PMU_HP_CK_GLOBAL_BBPLL_ICG_BIT == (1U << 25), "PMU_HP_CK_GLOBAL_BBPLL_ICG_BIT == (1 << 25)");
    CHALLENGER_ASSERT(PMU_HP_CK_XPD_BB_I2C_BIT == (1U << 28), "PMU_HP_CK_XPD_BB_I2C_BIT == (1 << 28)");
    CHALLENGER_ASSERT(PMU_HP_CK_XPD_BBPLL_I2C_BIT == (1U << 29), "PMU_HP_CK_XPD_BBPLL_I2C_BIT == (1 << 29)");
    CHALLENGER_ASSERT(PMU_HP_CK_XPD_BBPLL_BIT == (1U << 30), "PMU_HP_CK_XPD_BBPLL_BIT == (1 << 30)");
    CHALLENGER_ASSERT(PMU_BBPLL_POWER_ENABLE_MASK == (0x72000000U | (1U << 25)), "PMU_BBPLL_POWER_ENABLE_MASK == 0x72000000 | (1<<25)");

    /* Settle delay & timeouts */
    CHALLENGER_ASSERT(BBPLL_SETTLE_DELAY_US == 10U, "BBPLL_SETTLE_DELAY_US == 10 us erratum settle delay");
    CHALLENGER_ASSERT(BBPLL_CALIBRATION_TIMEOUT_CYCLES == 50000U, "BBPLL_CALIBRATION_TIMEOUT_CYCLES == 50000");
    CHALLENGER_ASSERT(BBPLL_BUSY_POLL_TIMEOUT_CYCLES == 10000U, "BBPLL_BUSY_POLL_TIMEOUT_CYCLES == 10000");
}

/* ========================================================================= */
/* Test 4: Driver Execution & Mock State Alignment                           */
/* ========================================================================= */
static void verify_driver_execution_mock_differential(void)
{
    printf("[CHALLENGER 2] 4. Verifying Driver Execution & Mock Differential Alignment...\n");

    /* Initialize modem */
    modem_status_t init_res = modem_init();
    CHALLENGER_ASSERT(init_res == MODEM_OK, "modem_init() returned MODEM_OK");

    /* BBPLL calibration verification */
    CHALLENGER_ASSERT(modem_is_bbpll_calibrated() == true, "modem_is_bbpll_calibrated() is true");
    uint32_t conf0 = modem_get_i2c_ana_mst_ana_conf0();
    CHALLENGER_ASSERT((conf0 & I2C_ANA_MST_CAL_DONE_BIT) != 0U, "ANA_CONF0 CAL_DONE is asserted");
    CHALLENGER_ASSERT((conf0 & I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT) != 0U, "ANA_CONF0 STOP_FORCE_HIGH is asserted");
    CHALLENGER_ASSERT((conf0 & I2C_ANA_MST_BBPLL_STOP_FORCE_LOW_BIT) == 0U, "ANA_CONF0 STOP_FORCE_LOW is cleared");

    /* Verify modem_enable_i2c_ana_mst() delegates to calibration */
    CHALLENGER_ASSERT(modem_enable_i2c_ana_mst() == MODEM_OK, "modem_enable_i2c_ana_mst() succeeds");

    /* Verify Wi-Fi enable */
    modem_status_t wifi_res = modem_enable_wifi_clocks();
    CHALLENGER_ASSERT(wifi_res == MODEM_OK, "modem_enable_wifi_clocks() succeeds");
    CHALLENGER_ASSERT(modem_is_wifi_enabled() == true, "modem_is_wifi_enabled() is true");

    /* Verify RF switch configuration */
    CHALLENGER_ASSERT(modem_get_rf_analog_switch0() == MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG, "Switch 0 configured");
    CHALLENGER_ASSERT(modem_get_rf_analog_switch1() == MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG, "Switch 1 configured");
}

/* ========================================================================= */
/* Main Runner                                                               */
/* ========================================================================= */
int main(void)
{
    printf("======================================================================\n");
    printf("   IRON-V TASK 1 CHALLENGER 2: SVD/TRM HARDWARE ALIGNMENT AUDIT       \n");
    printf("======================================================================\n");

    verify_hardware_base_addresses();
    verify_i2c_ana_mst_register_offsets();
    verify_bbpll_multiplier_equation();
    verify_driver_execution_mock_differential();

    printf("======================================================================\n");
    printf("  Challenger 2 Verification Summary:\n");
    printf("    Total Assertions: %d\n", s_total_assertions);
    printf("    Passed:           %d\n", s_passed_assertions);
    printf("    Failed:           %d\n", s_failed_assertions);
    printf("======================================================================\n");

    if (s_failed_assertions > 0)
    {
        printf("  VERDICT: REJECT / ISSUES DETECTED\n");
        return 1;
    }

    printf("  VERDICT: APPROVE - 100%% HARDWARE SVD/TRM ALIGNMENT VERIFIED\n");
    return 0;
}
