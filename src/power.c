/*
 * src/power.c
 *
 * ESP32-C6 Low-Power SRAM Shared Mailbox, Retention & Sleep State Machine
 * TRM Chapter 3 (§3.7.1 Memory Access) & Chapter 12 (§12.4.2 PMU)
 *
 * Implements bidirectional communication with the LP core coprocessor via
 * retained LP SRAM mailbox (0x50003000), power mode state transitions,
 * and retained scratchpad memory register access.
 */

#include "power.h"
#include "lp_core.h"
#include "string.h"

/* ========================================================================= */
/* Subsystem Internal State Tracking                                         */
/* ========================================================================= */
static power_mode_t s_power_mode = PM_STATE_ACTIVE;
static bool s_power_initialized = false;
static uint32_t s_last_cmd = LP_CMD_NONE;
static uint32_t s_last_ack = LP_CMD_NONE;

/* ========================================================================= */
/* Hardware vs Host Unit-Testing Emulation Abstraction                       */
/* ========================================================================= */
#if defined(__riscv)

static inline void power_fence(void)
{
    asm volatile("fence rw, rw" ::: "memory");
}

static inline volatile lp_shared_mailbox_t *mailbox_ptr(void)
{
    return (volatile lp_shared_mailbox_t *)LP_MAILBOX_BASE;
}

#else

/* Host Emulation Environment */
static lp_shared_mailbox_t s_mock_mailbox;
static uint32_t s_mock_aon_store[POWER_AON_STORE_COUNT];

static inline void power_fence(void)
{
    __sync_synchronize();
}

static inline volatile lp_shared_mailbox_t *mailbox_ptr(void)
{
    return (volatile lp_shared_mailbox_t *)&s_mock_mailbox;
}

#endif /* __riscv */

/* ========================================================================= */
/* Power Subsystem Implementation                                            */
/* ========================================================================= */

int power_init(void)
{
    s_power_mode = PM_STATE_ACTIVE;
    s_last_cmd = LP_CMD_NONE;
    s_last_ack = LP_CMD_NONE;

    int mb_res = power_mailbox_init();
    if (mb_res != POWER_OK)
    {
        return mb_res;
    }

    s_power_initialized = true;
    return POWER_OK;
}

int power_mailbox_init(void)
{
    volatile lp_shared_mailbox_t *mb = mailbox_ptr();
    if (mb == NULL)
    {
        return POWER_ERR_NULL_PTR;
    }

    mb->magic               = LP_MAILBOX_MAGIC;
    mb->hp_to_lp_cmd        = LP_CMD_NONE;
    mb->lp_to_hp_ack        = LP_CMD_NONE;
    mb->wake_reason         = LP_WAKE_REASON_NONE;
    mb->periodic_wake_count = 0U;
    mb->sensor_telemetry_raw = 0U;
    mb->atomic_lock         = 0U;

    power_fence();
    return POWER_OK;
}

volatile lp_shared_mailbox_t *power_get_mailbox(void)
{
    return mailbox_ptr();
}

int power_send_cmd(uint32_t cmd, uint32_t timeout_cycles)
{
    if (cmd == LP_CMD_NONE)
    {
        return POWER_ERR_INVALID_CMD;
    }

    if (!lp_core_is_running())
    {
        return POWER_ERR_LP_NOT_RUNNING;
    }

    volatile lp_shared_mailbox_t *mb = mailbox_ptr();
    if (mb == NULL)
    {
        return POWER_ERR_NULL_PTR;
    }

#if defined(__riscv)
    /* Clear acknowledge and set command */
    mb->lp_to_hp_ack = LP_CMD_NONE;
    mb->hp_to_lp_cmd = cmd;
    power_fence();

    /* Trigger LP core coprocessor */
    lp_core_trigger_lp();

    /* Poll for acknowledgment with cycle timeout */
    for (uint32_t cycle = 0U; cycle < timeout_cycles; cycle++)
    {
        power_fence();
        uint32_t ack = mb->lp_to_hp_ack;
        if (ack == cmd)
        {
            s_last_cmd = cmd;
            s_last_ack = ack;
            return POWER_OK;
        }
        if ((ack & LP_ACK_ERROR_FLAG) != 0U)
        {
            s_last_cmd = cmd;
            s_last_ack = ack;
            return POWER_ERR_INVALID_CMD;
        }
        asm volatile("nop");
    }

    return POWER_ERR_TIMEOUT;

#else
    /* Host Mock Emulation */
    (void)timeout_cycles;
    if (cmd == LP_CMD_SAMPLE_TELEMETRY)
    {
        mb->periodic_wake_count++;
        mb->sensor_telemetry_raw = LP_TELEMETRY_HEADER_MASK | (mb->periodic_wake_count & 0xFFFFU);
        mb->lp_to_hp_ack = cmd;
        mb->hp_to_lp_cmd = LP_CMD_NONE;
        s_last_cmd = cmd;
        s_last_ack = cmd;
        return POWER_OK;
    }
    else if (cmd == LP_CMD_ENTER_SLEEP || cmd == LP_CMD_RESET_STATS)
    {
        mb->lp_to_hp_ack = cmd;
        mb->hp_to_lp_cmd = LP_CMD_NONE;
        s_last_cmd = cmd;
        s_last_ack = cmd;
        return POWER_OK;
    }
    return POWER_ERR_INVALID_CMD;
#endif
}

int power_sample_telemetry(uint32_t *out_telemetry, uint32_t timeout_cycles)
{
    if (out_telemetry == NULL)
    {
        return POWER_ERR_NULL_PTR;
    }

    int res = power_send_cmd(LP_CMD_SAMPLE_TELEMETRY, timeout_cycles);
    if (res != POWER_OK)
    {
        return res;
    }

    volatile lp_shared_mailbox_t *mb = mailbox_ptr();
    *out_telemetry = mb->sensor_telemetry_raw;
    return POWER_OK;
}

int power_set_mode(power_mode_t mode)
{
    switch (mode)
    {
        case PM_STATE_ACTIVE:
            s_power_mode = PM_STATE_ACTIVE;
            return POWER_OK;

        case PM_STATE_LIGHT_SLEEP:
            if (lp_core_is_running())
            {
                power_send_cmd(LP_CMD_ENTER_SLEEP, POWER_HANDSHAKE_TIMEOUT_CYCLES);
            }
            s_power_mode = PM_STATE_LIGHT_SLEEP;
            return POWER_OK;

        case PM_STATE_DEEP_SLEEP:
#if defined(__riscv)
            *PMU_SLP_HP_PERI_CONF_REG |= PMU_HP_PERI_PD_EN_BIT;
            power_fence();
#endif
            s_power_mode = PM_STATE_DEEP_SLEEP;
            return POWER_OK;

        default:
            return POWER_ERR_INVALID_ARG;
    }
}

power_mode_t power_get_mode(void)
{
    return s_power_mode;
}

int power_get_telemetry(power_telemetry_t *telem)
{
    if (telem == NULL)
    {
        return POWER_ERR_NULL_PTR;
    }

    volatile lp_shared_mailbox_t *mb = mailbox_ptr();
    telem->current_mode     = s_power_mode;
    telem->mailbox_magic    = mb ? mb->magic : 0U;
    telem->last_cmd         = s_last_cmd;
    telem->last_ack         = mb ? mb->lp_to_hp_ack : 0U;
    telem->wake_count       = mb ? mb->periodic_wake_count : 0U;
    telem->sensor_raw       = mb ? mb->sensor_telemetry_raw : 0U;
    telem->aon_store0_val   = power_read_retained_store(0U);
    telem->lp_running       = lp_core_is_running();

    return POWER_OK;
}

uint32_t power_read_retained_store(uint32_t store_idx)
{
    if (store_idx >= POWER_AON_STORE_COUNT)
    {
        return 0U;
    }

#if defined(__riscv)
    return *LP_AON_STORE_REG(store_idx);
#else
    return s_mock_aon_store[store_idx];
#endif
}

int power_write_retained_store(uint32_t store_idx, uint32_t val)
{
    if (store_idx >= POWER_AON_STORE_COUNT)
    {
        return POWER_ERR_INVALID_ARG;
    }

#if defined(__riscv)
    *LP_AON_STORE_REG(store_idx) = val;
    power_fence();
#else
    s_mock_aon_store[store_idx] = val;
#endif

    return POWER_OK;
}
