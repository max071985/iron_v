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
static uint32_t s_mock_modem_syscon_clk_conf1 = 0U;
static uint32_t s_mock_modem_syscon_rst_conf  = 0U;
static uint32_t s_mock_modem_lpcon_coex_lp  = 0U;
static uint32_t s_mock_modem_lpcon_clk_conf = 0U;
static uint32_t s_mock_modem_syscon_date    = MODEM_SYSCON_DATE_EXPECTED;
static uint32_t s_mock_modem_lpcon_date     = MODEM_LPCON_DATE_EXPECTED;
static uint32_t s_mock_ieee802154_command   = 0U;
static uint32_t s_mock_ieee802154_ctrl_cfg  = 0U;

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
    if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF1_REG) return s_mock_modem_syscon_clk_conf1;
    if (a == (uintptr_t)MODEM_SYSCON_MODEM_RST_CONF_REG) return s_mock_modem_syscon_rst_conf;
    if (a == (uintptr_t)MODEM_LPCON_COEX_LP_CLK_CONF_REG) return s_mock_modem_lpcon_coex_lp;
    if (a == (uintptr_t)MODEM_LPCON_CLK_CONF_REG) return s_mock_modem_lpcon_clk_conf;
    if (a == (uintptr_t)MODEM_SYSCON_DATE_REG) return s_mock_modem_syscon_date;
    if (a == (uintptr_t)MODEM_LPCON_DATE_REG) return s_mock_modem_lpcon_date;
    if (a == (uintptr_t)IEEE802154_COMMAND_REG) return s_mock_ieee802154_command;
    if (a == (uintptr_t)IEEE802154_CTRL_CFG_REG) return s_mock_ieee802154_ctrl_cfg;
    return 0U;
}

static inline void reg_write(volatile uint32_t *addr, uint32_t val)
{
    uintptr_t a = (uintptr_t)addr;
    if (a == (uintptr_t)PCR_MODEM_APB_CONF_REG) s_mock_pcr_modem_apb_conf = val;
    else if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF_REG) s_mock_modem_syscon_clk_conf = val;
    else if (a == (uintptr_t)MODEM_SYSCON_CLK_CONF1_REG) s_mock_modem_syscon_clk_conf1 = val;
    else if (a == (uintptr_t)MODEM_SYSCON_MODEM_RST_CONF_REG) s_mock_modem_syscon_rst_conf = val;
    else if (a == (uintptr_t)MODEM_LPCON_COEX_LP_CLK_CONF_REG) s_mock_modem_lpcon_coex_lp = val;
    else if (a == (uintptr_t)MODEM_LPCON_CLK_CONF_REG) s_mock_modem_lpcon_clk_conf = val;
    else if (a == (uintptr_t)MODEM_SYSCON_DATE_REG) s_mock_modem_syscon_date = val;
    else if (a == (uintptr_t)MODEM_LPCON_DATE_REG) s_mock_modem_lpcon_date = val;
    else if (a == (uintptr_t)IEEE802154_COMMAND_REG) s_mock_ieee802154_command = val;
    else if (a == (uintptr_t)IEEE802154_CTRL_CFG_REG) s_mock_ieee802154_ctrl_cfg = val;
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
    /* 1. Enable PCR Modem APB peripheral clock and release hardware reset */
    reg_set_bits(PCR_MODEM_APB_CONF_REG, PCR_MODEM_APB_CLK_EN_BIT);
    reg_clear_bits(PCR_MODEM_APB_CONF_REG, PCR_MODEM_RST_EN_BIT);

    /* 2. Configure RF Coexistence Low-Power Clock (Select XTAL source) */
    reg_set_bits(MODEM_LPCON_COEX_LP_CLK_CONF_REG, MODEM_LPCON_CLK_COEX_LP_SEL_XTAL_BIT);
    reg_set_bits(MODEM_LPCON_CLK_CONF_REG, MODEM_LPCON_CLK_COEX_EN_BIT);

    /* 3. Enable common modem security accelerator and base BLE timer clocks */
    reg_set_bits(MODEM_SYSCON_CLK_CONF_REG,
                 MODEM_CLK_MODEM_SEC_EN_BIT |
                 MODEM_CLK_MODEM_SEC_APB_EN_BIT |
                 MODEM_CLK_BLE_TIMER_EN_BIT);

    /* 4. Settling delay for clock distribution network */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 5. Release resets for security and base timer peripherals */
    reg_clear_bits(MODEM_SYSCON_MODEM_RST_CONF_REG,
                   MODEM_RST_BLE_TIMER_BIT |
                   MODEM_RST_MODEM_SEC_BIT);

    /* 6. Update internal telemetry state */
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

    /* 1. Assert clock enable bits in MODEM_SYSCON_CLK_CONF1_REG */
    reg_set_bits(MODEM_SYSCON_CLK_CONF1_REG,
                 MODEM_CLK_WIFI_APB_EN_BIT |
                 MODEM_CLK_WIFIMAC_EN_BIT);

    /* 2. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 3. Clear reset bits in MODEM_SYSCON_MODEM_RST_CONF_REG */
    reg_clear_bits(MODEM_SYSCON_MODEM_RST_CONF_REG,
                   MODEM_RST_WIFIMAC_BIT |
                   MODEM_RST_WIFIBB_BIT);

    s_modem_state.wifi_clk_enabled = 1U;
    return MODEM_OK;
}

modem_status_t modem_disable_wifi_clocks(void)
{
    /* 1. Assert hardware resets */
    reg_set_bits(MODEM_SYSCON_MODEM_RST_CONF_REG,
                 MODEM_RST_WIFIMAC_BIT |
                 MODEM_RST_WIFIBB_BIT);

    /* 2. Settling delay */
    modem_delay(MODEM_CLOCK_SETTLE_CYCLES);

    /* 3. Gate off clock enable bits */
    reg_clear_bits(MODEM_SYSCON_CLK_CONF1_REG,
                   MODEM_CLK_WIFI_APB_EN_BIT |
                   MODEM_CLK_WIFIMAC_EN_BIT);

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

    /* 3. Gate off clock enable bits */
    reg_clear_bits(MODEM_SYSCON_CLK_CONF_REG,
                   MODEM_CLK_ZB_MAC_EN_BIT |
                   MODEM_CLK_ZB_APB_EN_BIT);

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
