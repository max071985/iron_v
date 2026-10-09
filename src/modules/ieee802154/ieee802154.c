/*
 * src/ieee802154.c
 *
 * ESP32-C6 IEEE 802.15.4 (Zigbee / Thread) Radio Transceiver Driver
 * TRM Chapter 30 (IEEE 802.15.4 Subsystem) & Chapter 8 (MODEM_SYSCON)
 *
 * Implements hardware register configuration, frequency and channel selection,
 * hardware auto-ACK, short/extended addressing, and transceiver lifecycle controls.
 */

#include "ieee802154.h"
#include "modem.h"
#include "string.h"

/* ========================================================================= */
/* Static Storage & Subsystem Telemetry State                                */
/* ========================================================================= */
static ieee802154_telemetry_t s_ieee802154_telemetry = {
    .state          = IEEE802154_STATE_DISABLE,
    .channel        = IEEE802154_CHANNEL_DEFAULT,
    .freq_mhz       = 2425U,
    .short_addr     = IEEE802154_DEFAULT_SHORT_ADDR,
    .pan_id         = IEEE802154_DEFAULT_PAN_ID,
    .ext_addr       = {0x40U, 0x4CU, 0xCAU, 0xFFU, 0xFEU, 0x45U, 0x1EU, 0x14U},
    .tx_power       = IEEE802154_TX_POWER_DEFAULT,
    .auto_ack_tx    = (CONFIG_IEEE802154_AUTO_ACK_TX != 0U),
    .auto_ack_rx    = (CONFIG_IEEE802154_AUTO_ACK_RX != 0U),
    .promiscuous    = false,
    .date_version   = IEEE802154_MAC_DATE_EXPECTED,
    .tx_count       = 0U,
    .rx_count       = 0U,
    .cca_fail_count = 0U
};

static bool s_ieee802154_initialized = false;

/* ========================================================================= */
/* Memory & Hardware Synchronization Barrier                                 */
/* ========================================================================= */
static inline void ieee802154_fence(void)
{
#if defined(__riscv)
    asm volatile("fence rw, rw" ::: "memory");
#else
    __sync_synchronize();
#endif
}

/* ========================================================================= */
/* MMIO Register Access Abstraction (Host Mock vs Target Silicon)            */
/* ========================================================================= */
#if defined(__riscv)

static inline uint32_t reg_read(volatile uint32_t *addr)
{
    return *addr;
}

static inline void reg_write(volatile uint32_t *addr, uint32_t val)
{
    *addr = val;
    ieee802154_fence();
}

static inline void reg_set_bits(volatile uint32_t *addr, uint32_t mask)
{
    *addr |= mask;
    ieee802154_fence();
}

static inline void reg_clear_bits(volatile uint32_t *addr, uint32_t mask)
{
    *addr &= ~mask;
    ieee802154_fence();
}

#else

/* Host Emulation Environment for Native Verification */
static uint32_t s_mock_cmd_reg          = 0U;
static uint32_t s_mock_ctrl_cfg_reg     = 0U;
static uint32_t s_mock_short_addr_reg   = 0U;
static uint32_t s_mock_pan_id_reg       = 0U;
static uint32_t s_mock_ext_addr0_reg    = 0U;
static uint32_t s_mock_ext_addr1_reg    = 0U;
static uint32_t s_mock_channel_reg      = 0U;
static uint32_t s_mock_tx_power_reg     = 0U;
static uint32_t s_mock_rx_status_reg    = 0U;
static uint32_t s_mock_tx_status_reg    = 0U;
static uint32_t s_mock_txrx_status_reg  = 0U;
static uint32_t s_mock_core_gck_reg     = 0U;
static uint32_t s_mock_mac_date_reg     = IEEE802154_MAC_DATE_EXPECTED;

static inline uint32_t reg_read(volatile uint32_t *addr)
{
    uintptr_t a = (uintptr_t)addr;
    if (a == (uintptr_t)IEEE802154_COMMAND_REG) return s_mock_cmd_reg;
    if (a == (uintptr_t)IEEE802154_CTRL_CFG_REG) return s_mock_ctrl_cfg_reg;
    if (a == (uintptr_t)IEEE802154_INF0_SHORT_ADDR_REG) return s_mock_short_addr_reg;
    if (a == (uintptr_t)IEEE802154_INF0_PAN_ID_REG) return s_mock_pan_id_reg;
    if (a == (uintptr_t)IEEE802154_INF0_EXTEND_ADDR0_REG) return s_mock_ext_addr0_reg;
    if (a == (uintptr_t)IEEE802154_INF0_EXTEND_ADDR1_REG) return s_mock_ext_addr1_reg;
    if (a == (uintptr_t)IEEE802154_CHANNEL_REG) return s_mock_channel_reg;
    if (a == (uintptr_t)IEEE802154_TX_POWER_REG) return s_mock_tx_power_reg;
    if (a == (uintptr_t)IEEE802154_RX_STATUS_REG) return s_mock_rx_status_reg;
    if (a == (uintptr_t)IEEE802154_TX_STATUS_REG) return s_mock_tx_status_reg;
    if (a == (uintptr_t)IEEE802154_TXRX_STATUS_REG) return s_mock_txrx_status_reg;
    if (a == (uintptr_t)IEEE802154_CORE_GCK_CFG_REG) return s_mock_core_gck_reg;
    if (a == (uintptr_t)IEEE802154_MAC_DATE_REG) return s_mock_mac_date_reg;
    return 0U;
}

static inline void reg_write(volatile uint32_t *addr, uint32_t val)
{
    uintptr_t a = (uintptr_t)addr;
    if (a == (uintptr_t)IEEE802154_COMMAND_REG) s_mock_cmd_reg = val;
    else if (a == (uintptr_t)IEEE802154_CTRL_CFG_REG) s_mock_ctrl_cfg_reg = val;
    else if (a == (uintptr_t)IEEE802154_INF0_SHORT_ADDR_REG) s_mock_short_addr_reg = val;
    else if (a == (uintptr_t)IEEE802154_INF0_PAN_ID_REG) s_mock_pan_id_reg = val;
    else if (a == (uintptr_t)IEEE802154_INF0_EXTEND_ADDR0_REG) s_mock_ext_addr0_reg = val;
    else if (a == (uintptr_t)IEEE802154_INF0_EXTEND_ADDR1_REG) s_mock_ext_addr1_reg = val;
    else if (a == (uintptr_t)IEEE802154_CHANNEL_REG) s_mock_channel_reg = val;
    else if (a == (uintptr_t)IEEE802154_TX_POWER_REG) s_mock_tx_power_reg = val;
    else if (a == (uintptr_t)IEEE802154_RX_STATUS_REG) s_mock_rx_status_reg = val;
    else if (a == (uintptr_t)IEEE802154_TX_STATUS_REG) s_mock_tx_status_reg = val;
    else if (a == (uintptr_t)IEEE802154_TXRX_STATUS_REG) s_mock_txrx_status_reg = val;
    else if (a == (uintptr_t)IEEE802154_CORE_GCK_CFG_REG) s_mock_core_gck_reg = val;
    else if (a == (uintptr_t)IEEE802154_MAC_DATE_REG) s_mock_mac_date_reg = val;
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
/* Authentic Silicon MAC & Extended EUI-64 Extraction                        */
/* ========================================================================= */
static void ieee802154_extract_eui64(uint8_t *out_eui64)
{
#if defined(__riscv)
    uint32_t mac0 = *EFUSE_MAC_SYS_0_REG;
    uint32_t mac1 = *EFUSE_MAC_SYS_1_REG;

    out_eui64[0] = (uint8_t)((mac1 >> 8U) & 0xFFU);
    out_eui64[1] = (uint8_t)(mac1 & 0xFFU);
    out_eui64[2] = (uint8_t)((mac0 >> 24U) & 0xFFU);
    out_eui64[3] = 0xFFU;
    out_eui64[4] = 0xFEU;
    out_eui64[5] = (uint8_t)((mac0 >> 16U) & 0xFFU);
    out_eui64[6] = (uint8_t)((mac0 >> 8U) & 0xFFU);
    out_eui64[7] = (uint8_t)(mac0 & 0xFFU);
#else
    out_eui64[0] = 0x40U;
    out_eui64[1] = 0x4CU;
    out_eui64[2] = 0xCAU;
    out_eui64[3] = 0xFFU;
    out_eui64[4] = 0xFEU;
    out_eui64[5] = 0x45U;
    out_eui64[6] = 0x1EU;
    out_eui64[7] = 0x14U;
#endif
}

/* ========================================================================= */
/* Subsystem Lifecycle Initialization                                        */
/* ========================================================================= */

ieee802154_status_t ieee802154_init(void)
{
    /* 1. Enable modem clocks and release reset via MODEM_SYSCON */
    modem_enable_ieee802154_clocks();

    /* 2. Issue FORCE_TRX_OFF command to place radio in safe standby state */
    reg_write(IEEE802154_COMMAND_REG, (uint32_t)IEEE802154_CMD_FORCE_TRX_OFF);

    /* 3. Configure default RF channel (Channel 15 -> 2425 MHz) */
    reg_write(IEEE802154_CHANNEL_REG, (uint32_t)IEEE802154_CHANNEL_DEFAULT);

    /* 4. Configure hardware auto-ACK TX and RX */
    uint32_t ctrl_cfg = 0U;
    if (CONFIG_IEEE802154_AUTO_ACK_TX)
    {
        ctrl_cfg |= IEEE802154_CTRL_AUTO_ACK_TX_BIT;
    }
    if (CONFIG_IEEE802154_AUTO_ACK_RX)
    {
        ctrl_cfg |= IEEE802154_CTRL_AUTO_ACK_RX_BIT;
    }
    reg_write(IEEE802154_CTRL_CFG_REG, ctrl_cfg);

    /* 5. Set default short address (0x1234) and PAN ID (0x1A2B) */
    reg_write(IEEE802154_INF0_SHORT_ADDR_REG, (uint32_t)IEEE802154_DEFAULT_SHORT_ADDR);
    reg_write(IEEE802154_INF0_PAN_ID_REG, (uint32_t)IEEE802154_DEFAULT_PAN_ID);

    /* 6. Extract authentic EUI-64 and configure extended address registers */
    ieee802154_extract_eui64(s_ieee802154_telemetry.ext_addr);
    uint32_t ext_low = ((uint32_t)s_ieee802154_telemetry.ext_addr[4]) |
                       ((uint32_t)s_ieee802154_telemetry.ext_addr[5] << 8U) |
                       ((uint32_t)s_ieee802154_telemetry.ext_addr[6] << 16U) |
                       ((uint32_t)s_ieee802154_telemetry.ext_addr[7] << 24U);
    uint32_t ext_high = ((uint32_t)s_ieee802154_telemetry.ext_addr[0]) |
                        ((uint32_t)s_ieee802154_telemetry.ext_addr[1] << 8U) |
                        ((uint32_t)s_ieee802154_telemetry.ext_addr[2] << 16U) |
                        ((uint32_t)s_ieee802154_telemetry.ext_addr[3] << 24U);
    reg_write(IEEE802154_INF0_EXTEND_ADDR0_REG, ext_low);
    reg_write(IEEE802154_INF0_EXTEND_ADDR1_REG, ext_high);

    /* 7. Configure default RF transmit power */
    reg_write(IEEE802154_TX_POWER_REG, (uint32_t)IEEE802154_TX_POWER_DEFAULT);

    /* 8. Latch hardware silicon date version */
    s_ieee802154_telemetry.date_version = reg_read(IEEE802154_MAC_DATE_REG);
    if (s_ieee802154_telemetry.date_version == 0U)
    {
        s_ieee802154_telemetry.date_version = IEEE802154_MAC_DATE_EXPECTED;
    }

    s_ieee802154_telemetry.state       = IEEE802154_STATE_TRX_OFF;
    s_ieee802154_telemetry.channel     = IEEE802154_CHANNEL_DEFAULT;
    s_ieee802154_telemetry.freq_mhz    = ieee802154_get_freq_mhz(IEEE802154_CHANNEL_DEFAULT);
    s_ieee802154_telemetry.short_addr  = IEEE802154_DEFAULT_SHORT_ADDR;
    s_ieee802154_telemetry.pan_id      = IEEE802154_DEFAULT_PAN_ID;
    s_ieee802154_telemetry.tx_power    = IEEE802154_TX_POWER_DEFAULT;
    s_ieee802154_telemetry.auto_ack_tx = (CONFIG_IEEE802154_AUTO_ACK_TX != 0U);
    s_ieee802154_telemetry.auto_ack_rx = (CONFIG_IEEE802154_AUTO_ACK_RX != 0U);
    s_ieee802154_telemetry.promiscuous = false;

    s_ieee802154_initialized = true;
    return IEEE802154_OK;
}

/* ========================================================================= */
/* Command Dispatcher                                                        */
/* ========================================================================= */

ieee802154_status_t ieee802154_cmd(ieee802154_cmd_t cmd)
{
    if (cmd != IEEE802154_CMD_TX_START &&
        cmd != IEEE802154_CMD_RX_START &&
        cmd != IEEE802154_CMD_CCA_START &&
        cmd != IEEE802154_CMD_FORCE_TRX_OFF)
    {
        return IEEE802154_ERR_INVALID_ARG;
    }

    if (!s_ieee802154_initialized)
    {
        ieee802154_init();
    }

    reg_write(IEEE802154_COMMAND_REG, (uint32_t)cmd);

    switch (cmd)
    {
        case IEEE802154_CMD_TX_START:
            s_ieee802154_telemetry.state = IEEE802154_STATE_TX;
            s_ieee802154_telemetry.tx_count++;
            break;
        case IEEE802154_CMD_RX_START:
            s_ieee802154_telemetry.state = IEEE802154_STATE_RX;
            s_ieee802154_telemetry.rx_count++;
            break;
        case IEEE802154_CMD_CCA_START:
            s_ieee802154_telemetry.state = IEEE802154_STATE_CCA;
            break;
        case IEEE802154_CMD_FORCE_TRX_OFF:
        default:
            s_ieee802154_telemetry.state = IEEE802154_STATE_TRX_OFF;
            break;
    }

    return IEEE802154_OK;
}

/* ========================================================================= */
/* RF Channel & Frequency Configuration                                      */
/* ========================================================================= */

ieee802154_status_t ieee802154_set_channel(uint8_t channel)
{
    if (channel < IEEE802154_CHANNEL_MIN || channel > IEEE802154_CHANNEL_MAX)
    {
        return IEEE802154_ERR_INVALID_ARG;
    }

    if (!s_ieee802154_initialized)
    {
        ieee802154_init();
    }

    reg_write(IEEE802154_CHANNEL_REG, (uint32_t)channel);
    s_ieee802154_telemetry.channel = channel;
    s_ieee802154_telemetry.freq_mhz = ieee802154_get_freq_mhz(channel);

    return IEEE802154_OK;
}

uint8_t ieee802154_get_channel(void)
{
    return s_ieee802154_telemetry.channel;
}

uint16_t ieee802154_get_freq_mhz(uint8_t channel)
{
    if (channel < IEEE802154_CHANNEL_MIN || channel > IEEE802154_CHANNEL_MAX)
    {
        return 0U;
    }
    return (uint16_t)(IEEE802154_FREQ_BASE_MHZ + (IEEE802154_FREQ_STEP_MHZ * (channel - IEEE802154_CHANNEL_MIN)));
}

/* ========================================================================= */
/* MAC Addressing Controls                                                   */
/* ========================================================================= */

ieee802154_status_t ieee802154_set_short_address(uint16_t addr)
{
    if (!s_ieee802154_initialized)
    {
        ieee802154_init();
    }

    reg_write(IEEE802154_INF0_SHORT_ADDR_REG, (uint32_t)addr);
    s_ieee802154_telemetry.short_addr = addr;
    return IEEE802154_OK;
}

uint16_t ieee802154_get_short_address(void)
{
    return s_ieee802154_telemetry.short_addr;
}

ieee802154_status_t ieee802154_set_pan_id(uint16_t pan_id)
{
    if (!s_ieee802154_initialized)
    {
        ieee802154_init();
    }

    reg_write(IEEE802154_INF0_PAN_ID_REG, (uint32_t)pan_id);
    s_ieee802154_telemetry.pan_id = pan_id;
    return IEEE802154_OK;
}

uint16_t ieee802154_get_pan_id(void)
{
    return s_ieee802154_telemetry.pan_id;
}

ieee802154_status_t ieee802154_set_extended_address(const uint8_t *ext_addr)
{
    if (ext_addr == NULL)
    {
        return IEEE802154_ERR_INVALID_ARG;
    }

    if (!s_ieee802154_initialized)
    {
        ieee802154_init();
    }

    memcpy(s_ieee802154_telemetry.ext_addr, ext_addr, IEEE802154_EXT_ADDR_LEN);

    uint32_t ext_low = ((uint32_t)ext_addr[4]) |
                       ((uint32_t)ext_addr[5] << 8U) |
                       ((uint32_t)ext_addr[6] << 16U) |
                       ((uint32_t)ext_addr[7] << 24U);
    uint32_t ext_high = ((uint32_t)ext_addr[0]) |
                        ((uint32_t)ext_addr[1] << 8U) |
                        ((uint32_t)ext_addr[2] << 16U) |
                        ((uint32_t)ext_addr[3] << 24U);

    reg_write(IEEE802154_INF0_EXTEND_ADDR0_REG, ext_low);
    reg_write(IEEE802154_INF0_EXTEND_ADDR1_REG, ext_high);
    return IEEE802154_OK;
}

ieee802154_status_t ieee802154_get_extended_address(uint8_t *out_addr)
{
    if (out_addr == NULL)
    {
        return IEEE802154_ERR_INVALID_ARG;
    }

    memcpy(out_addr, s_ieee802154_telemetry.ext_addr, IEEE802154_EXT_ADDR_LEN);
    return IEEE802154_OK;
}

/* ========================================================================= */
/* Frame Filtering, Auto-ACK & RF Power Controls                            */
/* ========================================================================= */

ieee802154_status_t ieee802154_set_auto_ack(bool tx_ack, bool rx_ack)
{
    if (!s_ieee802154_initialized)
    {
        ieee802154_init();
    }

    uint32_t ctrl = reg_read(IEEE802154_CTRL_CFG_REG);
    if (tx_ack)
    {
        ctrl |= IEEE802154_CTRL_AUTO_ACK_TX_BIT;
    }
    else
    {
        ctrl &= ~IEEE802154_CTRL_AUTO_ACK_TX_BIT;
    }

    if (rx_ack)
    {
        ctrl |= IEEE802154_CTRL_AUTO_ACK_RX_BIT;
    }
    else
    {
        ctrl &= ~IEEE802154_CTRL_AUTO_ACK_RX_BIT;
    }

    reg_write(IEEE802154_CTRL_CFG_REG, ctrl);
    s_ieee802154_telemetry.auto_ack_tx = tx_ack;
    s_ieee802154_telemetry.auto_ack_rx = rx_ack;

    return IEEE802154_OK;
}

ieee802154_status_t ieee802154_set_promiscuous(bool enable)
{
    if (!s_ieee802154_initialized)
    {
        ieee802154_init();
    }

    uint32_t ctrl = reg_read(IEEE802154_CTRL_CFG_REG);
    if (enable)
    {
        ctrl |= IEEE802154_CTRL_PROMISCUOUS_BIT;
    }
    else
    {
        ctrl &= ~IEEE802154_CTRL_PROMISCUOUS_BIT;
    }

    reg_write(IEEE802154_CTRL_CFG_REG, ctrl);
    s_ieee802154_telemetry.promiscuous = enable;
    return IEEE802154_OK;
}

ieee802154_status_t ieee802154_set_tx_power(uint8_t power)
{
    if (power > IEEE802154_TX_POWER_MAX)
    {
        return IEEE802154_ERR_INVALID_ARG;
    }

    if (!s_ieee802154_initialized)
    {
        ieee802154_init();
    }

    reg_write(IEEE802154_TX_POWER_REG, (uint32_t)power);
    s_ieee802154_telemetry.tx_power = power;
    return IEEE802154_OK;
}

uint8_t ieee802154_get_tx_power(void)
{
    return s_ieee802154_telemetry.tx_power;
}

/* ========================================================================= */
/* State & Telemetry Accessors                                               */
/* ========================================================================= */

ieee802154_state_t ieee802154_get_state(void)
{
    return s_ieee802154_telemetry.state;
}

ieee802154_status_t ieee802154_get_telemetry(ieee802154_telemetry_t *out_telem)
{
    if (out_telem == NULL)
    {
        return IEEE802154_ERR_INVALID_ARG;
    }

    *out_telem = s_ieee802154_telemetry;
    return IEEE802154_OK;
}

uint32_t ieee802154_get_date_version(void)
{
    return s_ieee802154_telemetry.date_version;
}
