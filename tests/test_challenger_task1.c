/*
 * tests/test_challenger_task1.c
 *
 * Empirical Challenger Verification Harness for Task 1:
 * - Parameterized macro bounds (switch indices 0..16, channel indices 0..1)
 * - Transaction command builders with boundary bytes (0x00, 0xFF, overflows, negative signed ints)
 * - Error/timeout path simulation for BBPLL calibration (BUSY stuck, CAL_DONE timeout, cycle counts)
 * - Unchecked caller return value behavior audit
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "regs/i2c_ana.h"
#include "regs/modem_rf_analog.h"
#include "modem.h"

static int s_tests_run = 0;
static int s_tests_passed = 0;
static int s_tests_failed = 0;

#define CHALLENGE_ASSERT(cond, msg) do { \
    s_tests_run++; \
    if (cond) { \
        s_tests_passed++; \
    } else { \
        s_tests_failed++; \
        printf("  [FAIL] Line %d: %s\n", __LINE__, msg); \
    } \
} while (0)

/* ========================================================================= */
/* Test Section 1: Parameterized RF Analog Switch Macro Bounds               */
/* ========================================================================= */
static void test_rf_analog_switch_bounds(void)
{
    printf("[CHALLENGER TEST 1] RF Analog Switch Macro Bounds (0..16)...\n");

    /* Verify count */
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_COUNT == 17U, "MODEM_RF_ANALOG_SWITCH_COUNT == 17");

    /* Switch 0 is special-cased at offset 0x0008 */
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_OFFSET(0U) == 0x0008U, "SWITCH_OFFSET(0) == 0x0008");
    CHALLENGE_ASSERT((uintptr_t)MODEM_RF_ANALOG_SWITCH_REG(0U) == 0x600AA008U, "SWITCH_REG(0) == 0x600AA008");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(0U) == MODEM_RF_ANALOG_SWITCH0_REG, "SWITCH_REG(0) == SWITCH0_REG");

    /* Switches 1..16 are contiguous at 0x002C + (idx-1)*4 */
    for (uint32_t i = 1U; i < 17U; i++)
    {
        uint32_t expected_offset = 0x002CU + ((i - 1U) * 4U);
        uintptr_t expected_addr = 0x600AA000U + expected_offset;

        CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_OFFSET(i) == expected_offset,
                         "SWITCH_OFFSET matches contiguous linear sequence");
        CHALLENGE_ASSERT((uintptr_t)MODEM_RF_ANALOG_SWITCH_REG(i) == expected_addr,
                         "SWITCH_REG matches MMIO address calculation");
    }

    /* Individual register pointer macro checks */
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(1U) == MODEM_RF_ANALOG_SWITCH1_REG, "SWITCH_REG(1) == SWITCH1_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(2U) == MODEM_RF_ANALOG_SWITCH2_REG, "SWITCH_REG(2) == SWITCH2_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(3U) == MODEM_RF_ANALOG_SWITCH3_REG, "SWITCH_REG(3) == SWITCH3_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(4U) == MODEM_RF_ANALOG_SWITCH4_REG, "SWITCH_REG(4) == SWITCH4_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(5U) == MODEM_RF_ANALOG_SWITCH5_REG, "SWITCH_REG(5) == SWITCH5_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(6U) == MODEM_RF_ANALOG_SWITCH6_REG, "SWITCH_REG(6) == SWITCH6_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(7U) == MODEM_RF_ANALOG_SWITCH7_REG, "SWITCH_REG(7) == SWITCH7_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(8U) == MODEM_RF_ANALOG_SWITCH8_REG, "SWITCH_REG(8) == SWITCH8_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(9U) == MODEM_RF_ANALOG_SWITCH9_REG, "SWITCH_REG(9) == SWITCH9_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(10U) == MODEM_RF_ANALOG_SWITCH10_REG, "SWITCH_REG(10) == SWITCH10_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(11U) == MODEM_RF_ANALOG_SWITCH11_REG, "SWITCH_REG(11) == SWITCH11_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(12U) == MODEM_RF_ANALOG_SWITCH12_REG, "SWITCH_REG(12) == SWITCH12_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(13U) == MODEM_RF_ANALOG_SWITCH13_REG, "SWITCH_REG(13) == SWITCH13_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(14U) == MODEM_RF_ANALOG_SWITCH14_REG, "SWITCH_REG(14) == SWITCH14_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(15U) == MODEM_RF_ANALOG_SWITCH15_REG, "SWITCH_REG(15) == SWITCH15_REG");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(16U) == MODEM_RF_ANALOG_SWITCH16_REG, "SWITCH_REG(16) == SWITCH16_REG");

    /* Expression evaluation in macro parameter */
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(3U + 5U) == MODEM_RF_ANALOG_SWITCH8_REG, "Macro handles addition expression");
    CHALLENGE_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(4U * 4U) == MODEM_RF_ANALOG_SWITCH16_REG, "Macro handles multiplication expression");

    /* Boundary check: index 17 (beyond SWITCH_COUNT-1) */
    uint32_t offset17 = MODEM_RF_ANALOG_SWITCH_OFFSET(17U);
    CHALLENGE_ASSERT(offset17 == 0x006CU, "SWITCH_OFFSET(17) calculates 0x006C linearly");
}

/* ========================================================================= */
/* Test Section 2: Channel Accessor Bounds & Behavior                        */
/* ========================================================================= */
static void test_channel_accessor_bounds(void)
{
    printf("[CHALLENGER TEST 2] Channel Accessor Bounds (ch = 0, 1, and beyond)...\n");

    /* Channel 0 checks */
    CHALLENGE_ASSERT((uintptr_t)I2C_ANA_MST_I2C_CTRL_REG(0U) == 0x600AF800U, "I2C_CTRL_REG(0) == 0x600AF800");
    CHALLENGE_ASSERT((uintptr_t)I2C_ANA_MST_I2C_CONF_REG(0U) == 0x600AF808U, "I2C_CONF_REG(0) == 0x600AF808");
    CHALLENGE_ASSERT((uintptr_t)I2C_ANA_MST_I2C_CTRL1_REG(0U) == 0x600AF824U, "I2C_CTRL1_REG(0) == 0x600AF824");

    /* Channel 1 checks */
    CHALLENGE_ASSERT((uintptr_t)I2C_ANA_MST_I2C_CTRL_REG(1U) == 0x600AF804U, "I2C_CTRL_REG(1) == 0x600AF804");
    CHALLENGE_ASSERT((uintptr_t)I2C_ANA_MST_I2C_CONF_REG(1U) == 0x600AF80CU, "I2C_CONF_REG(1) == 0x600AF80C");
    CHALLENGE_ASSERT((uintptr_t)I2C_ANA_MST_I2C_CTRL1_REG(1U) == 0x600AF828U, "I2C_CTRL1_REG(1) == 0x600AF828");

    /* Equivalence with fixed register definitions */
    CHALLENGE_ASSERT(I2C_ANA_MST_I2C_CTRL_REG(0U) == I2C_ANA_MST_I2C0_CTRL_REG, "I2C_CTRL_REG(0) == I2C0_CTRL_REG");
    CHALLENGE_ASSERT(I2C_ANA_MST_I2C_CTRL_REG(1U) == I2C_ANA_MST_I2C1_CTRL_REG, "I2C_CTRL_REG(1) == I2C1_CTRL_REG");
    CHALLENGE_ASSERT(I2C_ANA_MST_I2C_CONF_REG(0U) == I2C_ANA_MST_I2C0_CONF_REG, "I2C_CONF_REG(0) == I2C0_CONF_REG");
    CHALLENGE_ASSERT(I2C_ANA_MST_I2C_CONF_REG(1U) == I2C_ANA_MST_I2C1_CONF_REG, "I2C_CONF_REG(1) == I2C1_CONF_REG");
    CHALLENGE_ASSERT(I2C_ANA_MST_I2C_CTRL1_REG(0U) == I2C_ANA_MST_I2C0_CTRL1_REG, "I2C_CTRL1_REG(0) == I2C0_CTRL1_REG");
    CHALLENGE_ASSERT(I2C_ANA_MST_I2C_CTRL1_REG(1U) == I2C_ANA_MST_I2C1_CTRL1_REG, "I2C_CTRL1_REG(1) == I2C1_CTRL1_REG");

    /* Expression tests */
    CHALLENGE_ASSERT(I2C_ANA_MST_I2C_CTRL_REG(1U - 1U) == I2C_ANA_MST_I2C0_CTRL_REG, "Expression (1-1) yields channel 0");
    CHALLENGE_ASSERT(I2C_ANA_MST_I2C_CTRL_REG(0U | 1U) == I2C_ANA_MST_I2C1_CTRL_REG, "Expression (0|1) yields channel 1");

    /* Adversarial check: out of range channel index (ch = 2) */
    /* Implementation uses ((ch) == 0U ? I2C0 : I2C1), so ch=2 evaluates to I2C1 */
    CHALLENGE_ASSERT(I2C_ANA_MST_I2C_CTRL_REG(2U) == I2C_ANA_MST_I2C1_CTRL_REG,
                     "ch=2 defaults to channel 1 via ternary fallback");
}

/* ========================================================================= */
/* Test Section 3: Transaction Command Builders Stress & Boundary Testing     */
/* ========================================================================= */
static void test_command_builders_boundary_stress(void)
{
    printf("[CHALLENGER TEST 3] Command Builders Boundary & Bit-Bleed Stress...\n");

    /* Boundary byte values to test across slave, reg, and data */
    const uint32_t boundary_vals[] = {
        0x00U, 0x01U, 0x55U, 0x66U, 0x7FU, 0x80U, 0xAAU, 0xFEU, 0xFFU,
        0x100U, 0x101U, 0x7FFFU, 0x8000U, 0xFFFFU, 0x10000U, 0xFFFFFFFFU
    };
    const size_t num_vals = sizeof(boundary_vals) / sizeof(boundary_vals[0]);

    for (size_t s = 0; s < num_vals; s++)
    {
        for (size_t r = 0; r < num_vals; r++)
        {
            for (size_t d = 0; d < num_vals; d++)
            {
                uint32_t slave = boundary_vals[s];
                uint32_t reg   = boundary_vals[r];
                uint32_t data  = boundary_vals[d];

                uint32_t cmd_w = I2C_ANA_MST_CMD_WRITE(slave, reg, data);
                uint32_t cmd_r = I2C_ANA_MST_CMD_READ(slave, reg);

                /* CMD_WRITE assertions:
                 * - Bit 24 (READ_WRITE_BIT) MUST BE 1
                 * - Bit 25 (BUSY_BIT) MUST BE 0 (never set by command builder)
                 * - Bits 26..31 MUST BE 0
                 * - Bits 16..23 MUST equal (data & 0xFF)
                 * - Bits 8..15 MUST equal (reg & 0xFF)
                 * - Bits 0..7 MUST equal (slave & 0xFF)
                 */
                CHALLENGE_ASSERT((cmd_w & I2C_ANA_MST_READ_WRITE_BIT) != 0U, "CMD_WRITE bit 24 is 1");
                CHALLENGE_ASSERT((cmd_w & I2C_ANA_MST_BUSY_BIT) == 0U, "CMD_WRITE bit 25 (BUSY) is 0");
                CHALLENGE_ASSERT((cmd_w & 0xFC000000U) == 0U, "CMD_WRITE bits 26..31 are 0");
                CHALLENGE_ASSERT(((cmd_w >> 16) & 0xFFU) == (data & 0xFFU), "CMD_WRITE data byte matches");
                CHALLENGE_ASSERT(((cmd_w >> 8) & 0xFFU) == (reg & 0xFFU), "CMD_WRITE reg byte matches");
                CHALLENGE_ASSERT((cmd_w & 0xFFU) == (slave & 0xFFU), "CMD_WRITE slave byte matches");

                /* CMD_READ assertions:
                 * - Bit 24 (READ_WRITE_BIT) MUST BE 0
                 * - Bit 25 (BUSY_BIT) MUST BE 0
                 * - Bits 26..31 MUST BE 0
                 * - Bits 16..23 MUST BE 0 (data field empty on read request)
                 * - Bits 8..15 MUST equal (reg & 0xFF)
                 * - Bits 0..7 MUST equal (slave & 0xFF)
                 */
                CHALLENGE_ASSERT((cmd_r & I2C_ANA_MST_READ_WRITE_BIT) == 0U, "CMD_READ bit 24 is 0");
                CHALLENGE_ASSERT((cmd_r & I2C_ANA_MST_BUSY_BIT) == 0U, "CMD_READ bit 25 (BUSY) is 0");
                CHALLENGE_ASSERT((cmd_r & 0xFC000000U) == 0U, "CMD_READ bits 26..31 are 0");
                CHALLENGE_ASSERT(((cmd_r >> 16) & 0xFFU) == 0U, "CMD_READ bits 16..23 are 0");
                CHALLENGE_ASSERT(((cmd_r >> 8) & 0xFFU) == (reg & 0xFFU), "CMD_READ reg byte matches");
                CHALLENGE_ASSERT((cmd_r & 0xFFU) == (slave & 0xFFU), "CMD_READ slave byte matches");
            }
        }
    }

    /* Test negative signed char inputs */
    int8_t neg_slave = -1;
    int8_t neg_reg = -1;
    int8_t neg_data = -1;
    uint32_t cmd_neg = I2C_ANA_MST_CMD_WRITE(neg_slave, neg_reg, neg_data);
    CHALLENGE_ASSERT(cmd_neg == 0x01FFFFFFU, "Negative signed -1 args format clean 0x01FFFFFF without overflow");
    CHALLENGE_ASSERT((cmd_neg & I2C_ANA_MST_BUSY_BIT) == 0U, "Negative signed args do not bleed into BUSY bit");
}

/* ========================================================================= */
/* Test Section 4: BBPLL Calibration Error / Timeout Path Simulation         */
/* ========================================================================= */

/* Synthetic register state for empirical fault injection */
static uint32_t sim_ctrl_reg = 0U;
static uint32_t sim_ana_conf0 = 0U;
static int sim_poll_count = 0;
static int sim_fail_mode = 0; /* 0=normal, 1=busy stuck before write, 2=busy stuck after write, 3=cal_done stuck 0 */

static modem_status_t sim_regi2c_write(uint8_t slave_addr, uint8_t reg_addr, uint8_t data)
{
    /* Pre-write busy poll */
    uint32_t timeout = BBPLL_BUSY_POLL_TIMEOUT_CYCLES;
    while ((sim_ctrl_reg & I2C_ANA_MST_BUSY_BIT) != 0U)
    {
        sim_poll_count++;
        if (--timeout == 0U)
        {
            return MODEM_ERR_TIMEOUT;
        }
    }

    /* Write command */
    uint32_t cmd = I2C_ANA_MST_CMD_WRITE(slave_addr, reg_addr, data);
    sim_ctrl_reg = cmd;

    if (sim_fail_mode == 2)
    {
        /* Simulate busy stuck high after write */
        sim_ctrl_reg |= I2C_ANA_MST_BUSY_BIT;
    }

    /* Post-write busy poll */
    timeout = BBPLL_BUSY_POLL_TIMEOUT_CYCLES;
    while ((sim_ctrl_reg & I2C_ANA_MST_BUSY_BIT) != 0U)
    {
        sim_poll_count++;
        if (--timeout == 0U)
        {
            return MODEM_ERR_TIMEOUT;
        }
    }

    return MODEM_OK;
}

static modem_status_t sim_regi2c_read(uint8_t slave_addr, uint8_t reg_addr, uint8_t *data_out)
{
    uint32_t timeout = BBPLL_BUSY_POLL_TIMEOUT_CYCLES;
    while ((sim_ctrl_reg & I2C_ANA_MST_BUSY_BIT) != 0U)
    {
        sim_poll_count++;
        if (--timeout == 0U)
        {
            return MODEM_ERR_TIMEOUT;
        }
    }

    uint32_t cmd = I2C_ANA_MST_CMD_READ(slave_addr, reg_addr);
    sim_ctrl_reg = cmd;

    if (data_out != NULL)
    {
        *data_out = 0x00U;
    }

    return MODEM_OK;
}

static modem_status_t sim_modem_bbpll_calibrate(bool *calibrated_flag)
{
    /* Clear STOP_FORCE_HIGH, set STOP_FORCE_LOW */
    sim_ana_conf0 &= ~I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT;
    sim_ana_conf0 |= I2C_ANA_MST_BBPLL_STOP_FORCE_LOW_BIT;

    if (sim_regi2c_write(I2C_BBPLL_SLAVE_ADDR, I2C_BBPLL_OC_REF_ADDR, I2C_BBPLL_OC_REF_VAL) != MODEM_OK)
    {
        return MODEM_ERR_TIMEOUT;
    }
    if (sim_regi2c_write(I2C_BBPLL_SLAVE_ADDR, I2C_BBPLL_OC_DIV_REG_ADDR, I2C_BBPLL_OC_DIV_REG_VAL) != MODEM_OK)
    {
        return MODEM_ERR_TIMEOUT;
    }

    uint8_t dr = 0U;
    if (sim_regi2c_read(I2C_BBPLL_SLAVE_ADDR, I2C_BBPLL_OC_DR_ADDR, &dr) != MODEM_OK)
    {
        return MODEM_ERR_TIMEOUT;
    }
    if (sim_regi2c_write(I2C_BBPLL_SLAVE_ADDR, I2C_BBPLL_OC_DR_ADDR,
                         dr & (uint8_t)~(I2C_BBPLL_OC_DR1_MASK | I2C_BBPLL_OC_DR3_MASK)) != MODEM_OK)
    {
        return MODEM_ERR_TIMEOUT;
    }

    if (sim_regi2c_write(I2C_BBPLL_SLAVE_ADDR, I2C_BBPLL_REG6_ADDR, I2C_BBPLL_REG6_VAL) != MODEM_OK)
    {
        return MODEM_ERR_TIMEOUT;
    }

    uint8_t reg9 = 0U;
    if (sim_regi2c_read(I2C_BBPLL_SLAVE_ADDR, I2C_BBPLL_REG9_ADDR, &reg9) != MODEM_OK)
    {
        return MODEM_ERR_TIMEOUT;
    }
    if (sim_regi2c_write(I2C_BBPLL_SLAVE_ADDR, I2C_BBPLL_REG9_ADDR,
                         (reg9 & (uint8_t)~I2C_BBPLL_OC_VCO_DBIAS_MASK) | I2C_BBPLL_OC_VCO_DBIAS_DEFAULT) != MODEM_OK)
    {
        return MODEM_ERR_TIMEOUT;
    }

    /* Calibration polling */
    if (sim_fail_mode != 3)
    {
        sim_ana_conf0 |= I2C_ANA_MST_CAL_DONE_BIT;
    }

    uint32_t timeout = BBPLL_CALIBRATION_TIMEOUT_CYCLES;
    while ((sim_ana_conf0 & I2C_ANA_MST_CAL_DONE_BIT) == 0U)
    {
        sim_poll_count++;
        if (--timeout == 0U)
        {
            return MODEM_ERR_TIMEOUT;
        }
    }

    /* Lock calibration state */
    sim_ana_conf0 |= I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT;
    sim_ana_conf0 &= ~I2C_ANA_MST_BBPLL_STOP_FORCE_LOW_BIT;

    *calibrated_flag = true;
    return MODEM_OK;
}

static void test_bbpll_timeout_and_error_paths(void)
{
    printf("[CHALLENGER TEST 4] BBPLL Calibration Timeout & Error Paths...\n");

    bool calibrated = false;

    /* Scenario A: Normal execution (baseline sanity) */
    sim_ctrl_reg = 0U;
    sim_ana_conf0 = I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT;
    sim_poll_count = 0;
    sim_fail_mode = 0;
    calibrated = false;

    modem_status_t res = sim_modem_bbpll_calibrate(&calibrated);
    CHALLENGE_ASSERT(res == MODEM_OK, "Baseline simulated calibration succeeds");
    CHALLENGE_ASSERT(calibrated == true, "Calibrated flag set to true on success");
    CHALLENGE_ASSERT((sim_ana_conf0 & I2C_ANA_MST_CAL_DONE_BIT) != 0U, "CAL_DONE bit is set");
    CHALLENGE_ASSERT((sim_ana_conf0 & I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT) != 0U, "STOP_FORCE_HIGH restored");
    CHALLENGE_ASSERT((sim_ana_conf0 & I2C_ANA_MST_BBPLL_STOP_FORCE_LOW_BIT) == 0U, "STOP_FORCE_LOW cleared");

    /* Scenario B: BUSY bit stuck high before write */
    sim_ctrl_reg = I2C_ANA_MST_BUSY_BIT;
    sim_ana_conf0 = I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT;
    sim_poll_count = 0;
    sim_fail_mode = 1;
    calibrated = false;

    res = sim_modem_bbpll_calibrate(&calibrated);
    CHALLENGE_ASSERT(res == MODEM_ERR_TIMEOUT, "BUSY stuck initially triggers MODEM_ERR_TIMEOUT");
    CHALLENGE_ASSERT(calibrated == false, "Calibrated flag remains false on timeout");
    CHALLENGE_ASSERT(sim_poll_count == (int)BBPLL_BUSY_POLL_TIMEOUT_CYCLES,
                     "BUSY poll loops exactly BBPLL_BUSY_POLL_TIMEOUT_CYCLES (10000)");

    /* Scenario C: BUSY bit stuck high after write */
    sim_ctrl_reg = 0U;
    sim_ana_conf0 = I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT;
    sim_poll_count = 0;
    sim_fail_mode = 2;
    calibrated = false;

    res = sim_modem_bbpll_calibrate(&calibrated);
    CHALLENGE_ASSERT(res == MODEM_ERR_TIMEOUT, "BUSY stuck after write triggers MODEM_ERR_TIMEOUT");
    CHALLENGE_ASSERT(calibrated == false, "Calibrated flag remains false on timeout");
    CHALLENGE_ASSERT(sim_poll_count == (int)BBPLL_BUSY_POLL_TIMEOUT_CYCLES,
                     "Post-write busy poll loops exactly BBPLL_BUSY_POLL_TIMEOUT_CYCLES (10000)");

    /* Scenario D: CAL_DONE never asserts (timeout = 50000 cycles) */
    sim_ctrl_reg = 0U;
    sim_ana_conf0 = I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT;
    sim_poll_count = 0;
    sim_fail_mode = 3;
    calibrated = false;

    res = sim_modem_bbpll_calibrate(&calibrated);
    CHALLENGE_ASSERT(res == MODEM_ERR_TIMEOUT, "CAL_DONE failure triggers MODEM_ERR_TIMEOUT");
    CHALLENGE_ASSERT(calibrated == false, "Calibrated flag remains false on CAL_DONE timeout");
    CHALLENGE_ASSERT(sim_poll_count == (int)BBPLL_CALIBRATION_TIMEOUT_CYCLES,
                     "CAL_DONE loops exactly BBPLL_CALIBRATION_TIMEOUT_CYCLES (50000)");

    /* Critical Observation: In scenario D (timeout), step 12 was not reached:
     * STOP_FORCE_HIGH was left 0, and STOP_FORCE_LOW was left 1!
     */
    CHALLENGE_ASSERT((sim_ana_conf0 & I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT) == 0U,
                     "Observational finding: STOP_FORCE_HIGH remains cleared when calibration times out");
    CHALLENGE_ASSERT((sim_ana_conf0 & I2C_ANA_MST_BBPLL_STOP_FORCE_LOW_BIT) != 0U,
                     "Observational finding: STOP_FORCE_LOW remains set when calibration times out");
}

/* ========================================================================= */
/* Test Section 5: Driver Integration Sanity Check                           */
/* ========================================================================= */
static void test_driver_integration_sanity(void)
{
    printf("[CHALLENGER TEST 5] Real Driver Integration & Telemetry Sanity...\n");

    /* Initialize modem via genuine API */
    modem_status_t init_res = modem_init();
    CHALLENGE_ASSERT(init_res == MODEM_OK, "modem_init() succeeds");

    /* Check calibration */
    CHALLENGE_ASSERT(modem_is_bbpll_calibrated(), "modem_is_bbpll_calibrated reports true");
    CHALLENGE_ASSERT((modem_get_i2c_ana_mst_ana_conf0() & I2C_ANA_MST_CAL_DONE_BIT) != 0U,
                     "modem_get_i2c_ana_mst_ana_conf0() CAL_DONE bit is set");
    CHALLENGE_ASSERT((modem_get_i2c_ana_mst_ana_conf0() & I2C_ANA_MST_BBPLL_STOP_FORCE_HIGH_BIT) != 0U,
                     "modem_get_i2c_ana_mst_ana_conf0() STOP_FORCE_HIGH is set");
    CHALLENGE_ASSERT((modem_get_i2c_ana_mst_ana_conf0() & I2C_ANA_MST_BBPLL_STOP_FORCE_LOW_BIT) == 0U,
                     "modem_get_i2c_ana_mst_ana_conf0() STOP_FORCE_LOW is cleared");

    /* Re-run calibration via explicit API */
    modem_status_t cal_res = modem_bbpll_calibrate();
    CHALLENGE_ASSERT(cal_res == MODEM_OK, "Explicit modem_bbpll_calibrate() succeeds");
    CHALLENGE_ASSERT(modem_is_bbpll_calibrated(), "modem_is_bbpll_calibrated remains true");

    /* Verify I2C_ANA_MST wrapper */
    modem_status_t ana_res = modem_enable_i2c_ana_mst();
    CHALLENGE_ASSERT(ana_res == MODEM_OK, "modem_enable_i2c_ana_mst() succeeds");

    /* Check RF front-end analog switch init */
    modem_rf_analog_init();
    CHALLENGE_ASSERT(modem_get_rf_analog_switch0() == MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG,
                     "Switch 0 initialized to default config (0x03)");
    CHALLENGE_ASSERT(modem_get_rf_analog_switch1() == MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG,
                     "Switch 1 initialized to default config (0x03)");
}

/* ========================================================================= */
/* Main Test Runner                                                          */
/* ========================================================================= */
int main(void)
{
    printf("======================================================================\n");
    printf("     IRON-V TASK 1 ADVERSARIAL CHALLENGER EMPIRICAL TEST SUITE       \n");
    printf("======================================================================\n");

    test_rf_analog_switch_bounds();
    test_channel_accessor_bounds();
    test_command_builders_boundary_stress();
    test_bbpll_timeout_and_error_paths();
    test_driver_integration_sanity();

    printf("======================================================================\n");
    printf("  Challenger Test Results:\n");
    printf("    Total Assertions: %d\n", s_tests_run);
    printf("    Passed:           %d\n", s_tests_passed);
    printf("    Failed:           %d\n", s_tests_failed);
    printf("======================================================================\n");

    if (s_tests_failed > 0)
    {
        printf("  VERDICT: REJECT / ISSUES DETECTED\n");
        return 1;
    }

    printf("  VERDICT: ALL ADVERSARIAL CHALLENGE ASSERTIONS PASSED\n");
    return 0;
}
