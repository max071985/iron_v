/*
 * src/power.h
 *
 * ESP32-C6 Low-Power SRAM Shared Mailbox, Retention & Sleep State Machine
 * TRM Chapter 3 (§3.7.1 Memory Access) & Chapter 12 (§12.4.2 PMU)
 *
 * Provides bidirectional communication with the LP core coprocessor via
 * retained LP SRAM mailbox (0x50003000), power mode state management
 * (Active, Light Sleep, Deep Sleep), and retained scratchpad memory access.
 */

#ifndef POWER_H
#define POWER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================= */
/* Memory Cartography & Retained Mailbox Bases                               */
/* ========================================================================= */
#define LP_SHARED_MEM_BASE               0x50003000U
#define LP_MAILBOX_BASE                  0x50003000U
#define LP_MAILBOX_MAGIC                 0x49524F4EU  /* "IRON" */

/* Retained Always-On (LP_AON) Register Bases */
#define LP_AON_BASE_ADDR                 0x600B1000U
#define LP_AON_STORE0_OFFSET             0x00U
#define POWER_AON_STORE_COUNT            10U          /* STORE0 through STORE9 */

/* PMU Base & Power Configuration Register */
#define PMU_BASE_ADDR                    0x600B0000U
#define PMU_SLP_HP_PERI_CONF_OFFSET      0x018U
#define PMU_HP_PERI_PD_EN_BIT            (1U << 31)

/* ========================================================================= */
/* Parameterized Register Accessor Macros (AGENTS.md Compliance)             */
/* ========================================================================= */
#define LP_AON_STORE_REG(idx)            ((volatile uint32_t *)((uintptr_t)(LP_AON_BASE_ADDR + LP_AON_STORE0_OFFSET + ((idx) * 4U))))
#define LP_AON_STORE0_REG                LP_AON_STORE_REG(0U)
#define PMU_SLP_HP_PERI_CONF_REG         ((volatile uint32_t *)((uintptr_t)(PMU_BASE_ADDR + PMU_SLP_HP_PERI_CONF_OFFSET)))

/* ========================================================================= */
/* Command & Wakeup Opcodes                                                  */
/* ========================================================================= */
#define LP_CMD_NONE                      0x00000000U
#define LP_CMD_SAMPLE_TELEMETRY          0x00000001U
#define LP_CMD_ENTER_SLEEP               0x00000002U
#define LP_CMD_RESET_STATS               0x00000003U

#define LP_WAKE_REASON_NONE              0x00000000U
#define LP_WAKE_REASON_HP_TRIGGER        0x00000001U
#define LP_WAKE_REASON_TIMER             0x00000002U
#define LP_WAKE_REASON_IO                0x00000003U

#define LP_TELEMETRY_HEADER_MASK         0x49520000U
#define LP_ACK_ERROR_FLAG                0x80000000U

/* ========================================================================= */
/* Operational Timing & Timeout Limits                                       */
/* ========================================================================= */
#define POWER_HANDSHAKE_TIMEOUT_CYCLES   200000U      /* ~1.25 ms @ 160 MHz CPU PLL */
#define POWER_RETRY_CEILING              10U

/* ========================================================================= */
/* Driver Status & Error Enumeration                                         */
/* ========================================================================= */
typedef enum {
    POWER_OK                     = 0,
    POWER_ERR_NULL_PTR           = -1,
    POWER_ERR_TIMEOUT            = -2,
    POWER_ERR_INVALID_CMD        = -3,
    POWER_ERR_NOT_INITIALIZED    = -4,
    POWER_ERR_BUSY               = -5,
    POWER_ERR_LP_NOT_RUNNING     = -6,
    POWER_ERR_INVALID_ARG        = -7
} power_status_t;

/* ========================================================================= */
/* System Power Mode State Enumeration                                       */
/* ========================================================================= */
typedef enum {
    PM_STATE_ACTIVE              = 0,
    PM_STATE_LIGHT_SLEEP         = 1,
    PM_STATE_DEEP_SLEEP          = 2
} power_mode_t;

/* ========================================================================= */
/* Retained Shared Mailbox Structure (Single-Writer Protocol)                */
/* ========================================================================= */
typedef struct {
    volatile uint32_t magic;              /* 0x49524F4E ("IRON") */
    volatile uint32_t hp_to_lp_cmd;       /* Command opcode from HP to LP */
    volatile uint32_t lp_to_hp_ack;       /* Acknowledgment from LP to HP */
    volatile uint32_t wake_reason;        /* Wakeup reason code */
    volatile uint32_t periodic_wake_count;/* Autonomous wake count */
    volatile uint32_t sensor_telemetry_raw;/* Sensor telemetry */
    volatile uint32_t atomic_lock;        /* Synchronization spinlock */
} __attribute__((packed, aligned(4))) lp_shared_mailbox_t;

/* ========================================================================= */
/* Power Subsystem Telemetry Snapshot                                        */
/* ========================================================================= */
typedef struct {
    power_mode_t current_mode;
    uint32_t mailbox_magic;
    uint32_t last_cmd;
    uint32_t last_ack;
    uint32_t wake_count;
    uint32_t sensor_raw;
    uint32_t aon_store0_val;
    bool lp_running;
} power_telemetry_t;

/* ========================================================================= */
/* Public Driver API Declarations                                            */
/* ========================================================================= */

/*
 * power_init
 * Initializes the power management state machine, mailbox, and retained scratchpad.
 */
int power_init(void);

/*
 * power_mailbox_init
 * Initializes shared mailbox memory fields with the golden magic pattern.
 */
int power_mailbox_init(void);

/*
 * power_get_mailbox
 * Returns pointer to the hardware/emulated shared mailbox structure.
 */
volatile lp_shared_mailbox_t *power_get_mailbox(void);

/*
 * power_send_cmd
 * Sends a command opcode to the LP core coprocessor and awaits acknowledgment.
 */
int power_send_cmd(uint32_t cmd, uint32_t timeout_cycles);

/*
 * power_sample_telemetry
 * Requests a telemetry sample from the LP core and returns the raw telemetry word.
 */
int power_sample_telemetry(uint32_t *out_telemetry, uint32_t timeout_cycles);

/*
 * power_set_mode
 * Transitions the system power state (PM_STATE_ACTIVE, PM_STATE_LIGHT_SLEEP, PM_STATE_DEEP_SLEEP).
 */
int power_set_mode(power_mode_t mode);

/*
 * power_get_mode
 * Returns current system power mode.
 */
power_mode_t power_get_mode(void);

/*
 * power_get_telemetry
 * Populates snapshot of power subsystem and mailbox status.
 */
int power_get_telemetry(power_telemetry_t *telem);

/*
 * power_read_retained_store
 * Reads 32-bit value from retained LP_AON scratchpad register (STORE0 through STORE9).
 */
uint32_t power_read_retained_store(uint32_t store_idx);

/*
 * power_write_retained_store
 * Writes 32-bit value to retained LP_AON scratchpad register.
 */
int power_write_retained_store(uint32_t store_idx, uint32_t val);

#endif // POWER_H
