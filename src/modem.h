/*
 * src/modem.h
 *
 * ESP32-C6 Modem Clock & Power Control Driver (MODEM_SYSCON / MODEM_LPCON)
 * TRM Chapter 8 (Reset and Clock, §8.3-§8.4) & SVD Hardware Register Map
 *
 * Provides register definitions, type-safe accessor macros, and clock/reset
 * orchestration for Wi-Fi, Bluetooth 5 (LE), IEEE 802.15.4 (Zigbee/Thread),
 * and RF coexistence low-power domains.
 */

#ifndef IRON_V_MODEM_H
#define IRON_V_MODEM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "regs/i2c_ana.h"
#include "regs/adc.h"
#include "regs/modem_rf_analog.h"

/* ========================================================================= */
/* Peripheral Base Addresses (TRM Table 3-3 Peripheral Memory Cartography)   */
/* ========================================================================= */
#define PCR_BASE_ADDR                        0x60096000U
#define MODEM_SYSCON_BASE_ADDR               0x600A9800U
#define MODEM_LPCON_BASE_ADDR                0x600AF000U
#define IEEE802154_BASE_ADDR                 0x600A3000U

/* ========================================================================= */
/* PCR Peripheral Clock Distribution: Modem APB Configuration               */
/* ========================================================================= */
#define PCR_MODEM_APB_CONF_OFFSET            0x0108U
#ifndef PCR_REG
#define PCR_REG(offset)                      ((volatile uint32_t *)(uintptr_t)(PCR_BASE_ADDR + (offset)))
#endif
#ifndef PCR_MODEM_APB_CONF_REG
#define PCR_MODEM_APB_CONF_REG               PCR_REG(PCR_MODEM_APB_CONF_OFFSET)
#endif
#define PCR_MODEM_APB_CLK_EN_BIT             (1U << 0)
#define PCR_MODEM_RST_EN_BIT                 (1U << 1)

/* ========================================================================= */
/* MODEM_SYSCON Register Offsets                                             */
/* ========================================================================= */
#define MODEM_SYSCON_TEST_CONF_OFFSET        0x0000U
#define MODEM_SYSCON_CLK_CONF_OFFSET         0x0004U
#define MODEM_SYSCON_CLK_CONF_FO_OFFSET      0x0008U
#define MODEM_SYSCON_CLK_PWR_ST_OFFSET       0x000CU
#define MODEM_SYSCON_MODEM_RST_OFFSET        0x0010U
#define MODEM_SYSCON_CLK_CONF1_OFFSET        0x0014U
#define MODEM_SYSCON_MEM_CONF_OFFSET         0x0018U
#define MODEM_SYSCON_DATE_OFFSET             0x0024U

/* ========================================================================= */
/* MODEM_SYSCON Parameterized MMIO Register Accessors                        */
/* ========================================================================= */
#define MODEM_SYSCON_REG(offset)             ((volatile uint32_t *)(MODEM_SYSCON_BASE_ADDR + (offset)))
#define MODEM_SYSCON_TEST_CONF_REG           MODEM_SYSCON_REG(MODEM_SYSCON_TEST_CONF_OFFSET)
#define MODEM_SYSCON_CLK_CONF_REG            MODEM_SYSCON_REG(MODEM_SYSCON_CLK_CONF_OFFSET)
#define MODEM_SYSCON_CLK_CONF_FORCE_ON_REG   MODEM_SYSCON_REG(MODEM_SYSCON_CLK_CONF_FO_OFFSET)
#define MODEM_SYSCON_CLK_CONF_POWER_ST_REG   MODEM_SYSCON_REG(MODEM_SYSCON_CLK_PWR_ST_OFFSET)
#define MODEM_SYSCON_CLK_PWR_ST_REG          MODEM_SYSCON_CLK_CONF_POWER_ST_REG
#define MODEM_SYSCON_MODEM_RST_CONF_REG      MODEM_SYSCON_REG(MODEM_SYSCON_MODEM_RST_OFFSET)
#define MODEM_SYSCON_CLK_CONF1_REG           MODEM_SYSCON_REG(MODEM_SYSCON_CLK_CONF1_OFFSET)
#define MODEM_SYSCON_MEM_CONF_REG            MODEM_SYSCON_REG(MODEM_SYSCON_MEM_CONF_OFFSET)
#define MODEM_SYSCON_DATE_REG                MODEM_SYSCON_REG(MODEM_SYSCON_DATE_OFFSET)

/* ========================================================================= */
/* MODEM_SYSCON_CLK_CONF_REG Bitfields                                       */
/* ========================================================================= */
#define MODEM_CLK_DATA_DUMP_EN_BIT           (1U << 31)
#define MODEM_CLK_BLE_TIMER_EN_BIT           (1U << 30)
#define MODEM_CLK_MODEM_SEC_EN_BIT           (1U << 29)
#define MODEM_CLK_MODEM_SEC_APB_EN_BIT       (1U << 28)
#define MODEM_CLK_MODEM_SEC_BAH_EN_BIT       (1U << 27)
#define MODEM_CLK_MODEM_SEC_CCM_EN_BIT       (1U << 26)
#define MODEM_CLK_MODEM_SEC_ECB_EN_BIT       (1U << 25)
#define MODEM_CLK_ZB_MAC_EN_BIT              (1U << 24)
#define MODEM_CLK_ZB_APB_EN_BIT              (1U << 23)
#define MODEM_CLK_ZB_MAC_FO_BIT              (1U << 24)
#define MODEM_CLK_ZB_APB_FO_BIT              (1U << 23)
#define MODEM_CLK_ETM_EN_BIT                 (1U << 22)
#define MODEM_CLK_DATA_DUMP_MUX_BIT          (1U << 21)

/* ========================================================================= */
/* MODEM_SYSCON_MODEM_RST_CONF_REG Bitfields (1 = Assert Reset, 0 = Release) */
/* ========================================================================= */
#define MODEM_RST_DATA_DUMP_BIT              (1U << 31)
#define MODEM_RST_BLE_TIMER_BIT              (1U << 30)
#define MODEM_RST_MODEM_SEC_BIT              (1U << 29)
#define MODEM_RST_MODEM_BAH_BIT              (1U << 27)
#define MODEM_RST_MODEM_CCM_BIT              (1U << 26)
#define MODEM_RST_MODEM_ECB_BIT              (1U << 25)
#define MODEM_RST_ZBMAC_BIT                  (1U << 24)
#define MODEM_RST_ETM_BIT                    (1U << 22)
#define MODEM_RST_BTBB_BIT                   (1U << 18)
#define MODEM_RST_BTBB_APB_BIT               (1U << 17)
#define MODEM_RST_BTMAC_BIT                  (1U << 16)
#define MODEM_RST_BTMAC_APB_BIT              (1U << 15)
#define MODEM_RST_FE_BIT                     (1U << 14)
#define MODEM_RST_WIFIMAC_BIT                (1U << 10)
#define MODEM_RST_WIFIBB_BIT                 (1U << 8)

/* ========================================================================= */
/* MODEM_SYSCON_CLK_CONF1_REG Bitfields                                      */
/* ========================================================================= */
#define MODEM_CLK_FE_ANAMODE_160M_EN_BIT     (1U << 23)
#define MODEM_CLK_FE_ANAMODE_80M_EN_BIT      (1U << 22)
#define MODEM_CLK_FE_ANAMODE_40M_EN_BIT      (1U << 21)
#define MODEM_CLK_FE_480M_EN_BIT             (1U << 20)
#define MODEM_CLK_WIFIBB_480M_EN_BIT         (1U << 19)
#define MODEM_CLK_BT_EN_BIT                  (1U << 18)
#define MODEM_CLK_BT_APB_EN_BIT              (1U << 17)
#define MODEM_CLK_FE_APB_EN_BIT              (1U << 16)
#define MODEM_CLK_FE_CAL_160M_EN_BIT         (1U << 15)
#define MODEM_CLK_FE_160M_EN_BIT             (1U << 14)
#define MODEM_CLK_FE_80M_EN_BIT              (1U << 13)
#define MODEM_CLK_FE_40M_EN_BIT              (1U << 12)
#define MODEM_CLK_FE_20M_EN_BIT              (1U << 11)
#define MODEM_CLK_WIFI_APB_EN_BIT            (1U << 10)
#define MODEM_CLK_WIFIMAC_EN_BIT             (1U << 9)
#define MODEM_CLK_WIFIBB_160X1_EN_BIT        (1U << 8)
#define MODEM_CLK_WIFIBB_80X1_EN_BIT         (1U << 7)
#define MODEM_CLK_WIFIBB_40X1_EN_BIT         (1U << 6)
#define MODEM_CLK_WIFIBB_80X_EN_BIT          (1U << 5)
#define MODEM_CLK_WIFIBB_40X_EN_BIT          (1U << 4)
#define MODEM_CLK_WIFIBB_80M_EN_BIT          (1U << 3)
#define MODEM_CLK_WIFIBB_44M_EN_BIT          (1U << 2)
#define MODEM_CLK_WIFIBB_40M_EN_BIT          (1U << 1)
#define MODEM_CLK_WIFIBB_22M_EN_BIT          (1U << 0)

/* ========================================================================= */
/* MODEM_LPCON Register Offsets                                              */
/* ========================================================================= */
#define MODEM_LPCON_TEST_CONF_OFFSET         0x0000U
#define MODEM_LPCON_LP_TIMER_CONF_OFFSET     0x0004U
#define MODEM_LPCON_COEX_LP_CLK_OFFSET       0x0008U
#define MODEM_LPCON_WIFI_LP_CLK_OFFSET       0x000CU
#define MODEM_LPCON_I2C_MST_CLK_OFFSET       0x0010U
#define MODEM_LPCON_MODEM_32K_CLK_OFFSET     0x0014U
#define MODEM_LPCON_CLK_CONF_OFFSET          0x0018U
#define MODEM_LPCON_CLK_CONF_FO_OFFSET       0x001CU
#define MODEM_LPCON_CLK_PWR_ST_OFFSET        0x0020U
#define MODEM_LPCON_RST_CONF_OFFSET          0x0024U
#define MODEM_LPCON_MEM_CONF_OFFSET          0x0028U
#define MODEM_LPCON_DATE_OFFSET              0x002CU

/* ========================================================================= */
/* MODEM_LPCON Parameterized MMIO Register Accessors                         */
/* ========================================================================= */
#define MODEM_LPCON_REG(offset)              ((volatile uint32_t *)(MODEM_LPCON_BASE_ADDR + (offset)))
#define MODEM_LPCON_TEST_CONF_REG            MODEM_LPCON_REG(MODEM_LPCON_TEST_CONF_OFFSET)
#define MODEM_LPCON_LP_TIMER_CONF_REG        MODEM_LPCON_REG(MODEM_LPCON_LP_TIMER_CONF_OFFSET)
#define MODEM_LPCON_COEX_LP_CLK_CONF_REG     MODEM_LPCON_REG(MODEM_LPCON_COEX_LP_CLK_OFFSET)
#define MODEM_LPCON_WIFI_LP_CLK_CONF_REG     MODEM_LPCON_REG(MODEM_LPCON_WIFI_LP_CLK_OFFSET)
#define MODEM_LPCON_I2C_MST_CLK_CONF_REG     MODEM_LPCON_REG(MODEM_LPCON_I2C_MST_CLK_OFFSET)
#define MODEM_LPCON_MODEM_32K_CLK_CONF_REG   MODEM_LPCON_REG(MODEM_LPCON_MODEM_32K_CLK_OFFSET)
#define MODEM_LPCON_CLK_CONF_REG             MODEM_LPCON_REG(MODEM_LPCON_CLK_CONF_OFFSET)
#define MODEM_LPCON_CLK_CONF_FORCE_ON_REG    MODEM_LPCON_REG(MODEM_LPCON_CLK_CONF_FO_OFFSET)
#define MODEM_LPCON_CLK_CONF_POWER_ST_REG    MODEM_LPCON_REG(MODEM_LPCON_CLK_PWR_ST_OFFSET)
#define MODEM_LPCON_RST_CONF_REG             MODEM_LPCON_REG(MODEM_LPCON_RST_CONF_OFFSET)
#define MODEM_LPCON_MEM_CONF_REG             MODEM_LPCON_REG(MODEM_LPCON_MEM_CONF_OFFSET)
#define MODEM_LPCON_DATE_REG                 MODEM_LPCON_REG(MODEM_LPCON_DATE_OFFSET)

/* ========================================================================= */
/* MODEM_LPCON Bitfield Definitions                                          */
/* ========================================================================= */
#define MODEM_LPCON_CLK_COEX_LP_SEL_XTAL_BIT (1U << 2)
#define MODEM_LPCON_CLK_COEX_LP_SEL_FAST_BIT (1U << 1)
#define MODEM_LPCON_CLK_COEX_LP_SEL_SLOW_BIT (1U << 0)
#define MODEM_LPCON_CLK_COEX_LP_SEL_XTAL32K_B (1U << 3)
#define MODEM_LPCON_CLK_COEX_LP_DIV_NUM_MASK (0x0000FFF0U)

#define MODEM_LPCON_CLK_WIFIPWR_EN_BIT       (1U << 0)
#define MODEM_LPCON_CLK_COEX_EN_BIT          (1U << 1)
#define MODEM_LPCON_CLK_I2C_MST_EN_BIT       (1U << 2)
#define MODEM_LPCON_CLK_LP_TIMER_EN_BIT      (1U << 3)
#define MODEM_LPCON_I2C_MST_SEL_160M_BIT     (1U << 0)
#define MODEM_LPCON_CLK_I2C_MST_FO_BIT       (1U << 2)
#define MODEM_LPCON_CLK_I2C_MST_MEM_FO_BIT   (1U << 5)
#define MODEM_LPCON_RST_I2C_MST_BIT          (1U << 2)
#define MODEM_LPCON_MEM_I2C_MST_PU_BIT       (1U << 8)
#define MODEM_LPCON_MEM_I2C_MST_PD_BIT       (1U << 9)

#define MODEM_CLK_WIFIBB_ALL_EN_MASK         (MODEM_CLK_WIFIBB_22M_EN_BIT | \
                                              MODEM_CLK_WIFIBB_40M_EN_BIT | \
                                              MODEM_CLK_WIFIBB_44M_EN_BIT | \
                                              MODEM_CLK_WIFIBB_80M_EN_BIT | \
                                              MODEM_CLK_WIFIBB_40X_EN_BIT | \
                                              MODEM_CLK_WIFIBB_80X_EN_BIT | \
                                              MODEM_CLK_WIFIBB_40X1_EN_BIT | \
                                              MODEM_CLK_WIFIBB_80X1_EN_BIT | \
                                              MODEM_CLK_WIFIBB_160X1_EN_BIT | \
                                              MODEM_CLK_WIFIBB_480M_EN_BIT)

#define MODEM_CLK_FE_ALL_EN_MASK             (MODEM_CLK_FE_20M_EN_BIT | \
                                              MODEM_CLK_FE_40M_EN_BIT | \
                                              MODEM_CLK_FE_80M_EN_BIT | \
                                              MODEM_CLK_FE_160M_EN_BIT | \
                                              MODEM_CLK_FE_CAL_160M_EN_BIT | \
                                              MODEM_CLK_FE_APB_EN_BIT | \
                                              MODEM_CLK_FE_480M_EN_BIT | \
                                              MODEM_CLK_FE_ANAMODE_40M_EN_BIT | \
                                              MODEM_CLK_FE_ANAMODE_80M_EN_BIT | \
                                              MODEM_CLK_FE_ANAMODE_160M_EN_BIT)

#define MODEM_LPCON_RST_WIFIPWR_BIT          (1U << 0)
#define MODEM_LPCON_RST_COEX_BIT             (1U << 1)
#define MODEM_LPCON_CLK_WIFIPWR_FO_BIT       (1U << 0)
#define MODEM_LPCON_CLK_COEX_FO_BIT          (1U << 1)
#define MODEM_LPCON_CLK_BCMEM_FO_BIT         (1U << 4)
#define MODEM_LPCON_CLK_CHAN_FREQ_MEM_FO_BIT (1U << 6)
#define MODEM_LPCON_CLK_PBUS_MEM_FO_BIT      (1U << 7)
#define MODEM_LPCON_CLK_AGC_MEM_FO_BIT       (1U << 8)
#define MODEM_LPCON_CLK_DC_MEM_FO_BIT        (1U << 9)

#define MODEM_LPCON_MEM_DC_PU_BIT            (1U << 0)
#define MODEM_LPCON_MEM_DC_PD_BIT            (1U << 1)
#define MODEM_LPCON_MEM_AGC_PU_BIT           (1U << 2)
#define MODEM_LPCON_MEM_AGC_PD_BIT           (1U << 3)
#define MODEM_LPCON_MEM_PBUS_PU_BIT          (1U << 4)
#define MODEM_LPCON_MEM_PBUS_PD_BIT          (1U << 5)
#define MODEM_LPCON_MEM_BC_PU_BIT            (1U << 6)
#define MODEM_LPCON_MEM_BC_PD_BIT            (1U << 7)
#define MODEM_LPCON_MEM_CHAN_FREQ_PU_BIT     (1U << 10)
#define MODEM_LPCON_MEM_CHAN_FREQ_PD_BIT     (1U << 11)

#define LP_PERI_CLK_LP_ANA_I2C_BIT           (1U << 29)
#define LP_PERI_RST_LP_ANA_I2C_BIT           (1U << 29)
#define LP_I2C_ANA_MST_ALL_DEVICES_EN        0x00000FFFU

#include "regs/lp_peri.h"
#include "regs/lp_i2c_ana_mst.h"
#include "regs/lp_ana.h"
#include "regs/modem_rf.h"
#include "regs/pmu.h"

/* ========================================================================= */
/* PMU Power State & Modem ICG Mode Definitions                              */
/* ========================================================================= */
#define PMU_HP_ICG_MODEM_CODE_SLEEP          0U
#define PMU_HP_ICG_MODEM_CODE_MODEM          1U
#define PMU_HP_ICG_MODEM_CODE_ACTIVE         2U

/* Un-gate all modem clock domains across power states in MODEM_SYSCON & MODEM_LPCON */
#define MODEM_SYSCON_CLK_CONF_POWER_ST_ACTIVE_ALL ( \
    (0xFU << 8)  | /* ZB */ \
    (0xFU << 12) | /* FE */ \
    (0xFU << 16) | /* BT */ \
    (0xFU << 20) | /* WIFI */ \
    (0xFU << 24) | /* MODEM_PERI */ \
    (0xFU << 28))  /* MODEM_APB */

#define MODEM_LPCON_CLK_CONF_POWER_ST_ACTIVE_ALL ( \
    (0xFU << 16) | /* WIFIPWR */ \
    (0xFU << 20) | /* COEX */ \
    (0xFU << 24) | /* I2C_MST */ \
    (0xFU << 28))  /* LP_APB */

/* ========================================================================= */
/* IEEE 802.15.4 Baseband Verification Register Accessors                    */
/* ========================================================================= */
#define IEEE802154_REG(offset)               ((volatile uint32_t *)(IEEE802154_BASE_ADDR + (offset)))
#define IEEE802154_COMMAND_OFFSET            0x0000U
#define IEEE802154_CTRL_CFG_OFFSET           0x0004U
#define IEEE802154_COMMAND_REG               IEEE802154_REG(IEEE802154_COMMAND_OFFSET)
#define IEEE802154_CTRL_CFG_REG              IEEE802154_REG(IEEE802154_CTRL_CFG_OFFSET)

/* ========================================================================= */
/* MODEM RF Front-End Base Address & Parameterized MMIO Accessor             */
/* ========================================================================= */
#define MODEM_FE_BASE_ADDR                   0x600A0000U
#define MODEM_FE_SIZE                        0x1000U
#define MODEM_FE_END_ADDR                    (MODEM_FE_BASE_ADDR + MODEM_FE_SIZE)
#define MODEM_FE_REG(offset)                 ((volatile uint32_t *)(uintptr_t)(MODEM_FE_BASE_ADDR + (offset)))

#define MODEM_FE_FREQ_STATUS_OFFSET          0x00CCU
#define MODEM_FE_FREQ_STATUS_REG             MODEM_FE_REG(MODEM_FE_FREQ_STATUS_OFFSET)
#define MODEM_FE_FREQ_LOCK_BIT               (1U << 8)
#define MODEM_FE_FREQ_LOCK_TIMEOUT_US        10000U

#define MODEM_FE_IQ_CTRL_OFFSET              0x0450U
#define MODEM_FE_IQ_TRIG_OFFSET              0x0454U
#define MODEM_FE_IQ_STAT_OFFSET              0x0458U
#define MODEM_FE_IQ_CTRL_REG                 MODEM_FE_REG(MODEM_FE_IQ_CTRL_OFFSET)
#define MODEM_FE_IQ_TRIG_REG                 MODEM_FE_REG(MODEM_FE_IQ_TRIG_OFFSET)
#define MODEM_FE_IQ_STAT_REG                 MODEM_FE_REG(MODEM_FE_IQ_STAT_OFFSET)
#define MODEM_FE_IQ_DONE_BIT                 (1U << 0)
#define MODEM_FE_IQ_TIMEOUT_US               5000U

#define MODEM_FE_IQ_RESULT_I_OFFSET          0x0488U
#define MODEM_FE_IQ_RESULT_Q_OFFSET          0x048CU
#define MODEM_FE_IQ_RESULT_PWR_OFFSET        0x0490U
#define MODEM_FE_IQ_RESULT_I_REG             MODEM_FE_REG(MODEM_FE_IQ_RESULT_I_OFFSET)
#define MODEM_FE_IQ_RESULT_Q_REG             MODEM_FE_REG(MODEM_FE_IQ_RESULT_Q_OFFSET)
#define MODEM_FE_IQ_RESULT_PWR_REG           MODEM_FE_REG(MODEM_FE_IQ_RESULT_PWR_OFFSET)

/* ========================================================================= */
/* Hardware Version Date Constants (Verified on ESP32-C6 Silicon)           */
/* ========================================================================= */
#define MODEM_SYSCON_DATE_EXPECTED           0x02206300U
#define MODEM_LPCON_DATE_EXPECTED            0x02206240U

/* Settling delay between clock enabling and reset releasing (in CPU cycles) */
#define MODEM_CLOCK_SETTLE_CYCLES            1000U

/* ========================================================================= */
/* Return Status Enumerations                                                */
/* ========================================================================= */
typedef enum {
    MODEM_OK = 0,
    MODEM_ERR_INVALID_ARG = -1,
    MODEM_ERR_TIMEOUT = -2,
    MODEM_ERR_BUS_FAULT = -3
} modem_status_t;

/* ========================================================================= */
/* Concrete Data Structures (docs/development-roadmap.md Task 5.1)          */
/* ========================================================================= */
typedef struct {
    uint8_t wifi_clk_enabled;
    uint8_t ble_clk_enabled;
    uint8_t ieee802154_clk_enabled;
    uint8_t coexistence_enabled;
} modem_clock_state_t;

/* ========================================================================= */
/* Public Driver API Declarations                                            */
/* ========================================================================= */

/* Subsystem lifecycle initialization */
modem_status_t modem_init(void);

/* Discrete subsystem clock controls */
modem_status_t modem_enable_wifi_clocks(void);
modem_status_t modem_disable_wifi_clocks(void);
modem_status_t modem_enable_i2c_ana_mst(void);
modem_status_t modem_enable_rf_synthesizer(void);
modem_status_t modem_enable_ble_clocks(void);
modem_status_t modem_disable_ble_clocks(void);
modem_status_t modem_enable_ieee802154_clocks(void);
modem_status_t modem_disable_ieee802154_clocks(void);

/* Orchestrated wireless subsystem clock & power sequence (TEST 29 stimulus) */
modem_status_t modem_enable_all_clocks(void);

/* Coexistence low-power clock controls */
modem_status_t modem_enable_coexistence(void);
modem_status_t modem_disable_coexistence(void);
bool modem_validate_coexistence(void);

/* Telemetry and state query APIs */
modem_status_t modem_get_clock_state(modem_clock_state_t *state);
uint32_t modem_get_syscon_date(void);
uint32_t modem_get_lpcon_date(void);
uint32_t modem_get_rf_enable_reg(void);
uint32_t modem_get_lp_i2c_ana_mst_device_en(void);
uint32_t modem_get_lp_ana_peri_pwr_reg(void);
uint32_t modem_get_lp_ana_peri_clk_reg(void);
bool modem_is_wifi_enabled(void);
bool modem_is_ble_enabled(void);
bool modem_is_ieee802154_enabled(void);
bool modem_is_coex_enabled(void);
bool modem_is_rf_synth_enabled(void);

/* I2C Analog Master & SAR ADC Calibration APIs (Task 4) */
modem_status_t modem_configure_i2c_analog_master(void);
uint32_t modem_get_i2c_ana_mst_ana_conf0(void);
modem_status_t modem_prime_sar_adc_calibration(void);
uint32_t modem_get_i2c_ana_mst_link0_reg(void);
uint32_t modem_get_i2c_ana_mst_link1_reg(void);
bool modem_is_sar_adc_cal_primed(void);

/* Bare-Metal Wi-Fi RX AGC Override */
void modem_force_rx_agc(void);

/* Bare-Metal RF Front-End Analog Routing & TX Power Activation (Task 5.7.3) */
void modem_rf_analog_init(void);
void modem_force_tx_pa(void);
uint32_t modem_get_rf_analog_switch0(void);
uint32_t modem_get_rf_analog_switch1(void);

#endif /* IRON_V_MODEM_H */
