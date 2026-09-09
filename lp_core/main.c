/*
 * lp_core/main.c
 *
 * Standalone LP Core Coprocessor Firmware Entry & Handshake Worker
 * TRM Chapter 3 (§3.1-§3.9 Low-Power CPU) & Chapter 12 (§12.4 PMU)
 *
 * Bare-metal RISC-V RV32IMAC code executing in the 20 MHz Low-Power domain.
 * Hardware initialization facts:
 *   - Vector table base is initialized to 0x50000000 (TRM §3.3)
 *   - Hardware fetches instructions from 0x50000080 after reset (TRM §3.3)
 *   - Stack top is placed at 0x50003000
 *   - Handshake golden word 0xCAFEBABE is written to 0x50002FFC
 *   - Autonomous execution tick counter is maintained at 0x50002FF8
 *   - PMU LP->HP trigger (bit 30) is pulsed via PMU_HP_LP_CPU_COMM_REG (0x600B0184)
 */

#include <stdint.h>

/* Memory Cartography and Test Addresses (AGENTS.md Zero Magic Numbers) */
#define LP_STACK_TOP_ADDR                0x50002FF0U  /* Top of LP stack partition (below mailbox) */
#define LP_TEST_MAGIC_ADDR               0x50002FFCU  /* Diagnostic handshake word */
#define LP_TEST_COUNTER_ADDR             0x50002FF8U  /* Autonomous tick counter */
#define LP_TEST_MAGIC_VAL                0xCAFEBABEU

/* Shared Mailbox Mapping and Command Opcodes (Task 4.2) */
#define LP_MAILBOX_BASE_ADDR             0x50003000U
#define LP_MAILBOX_MAGIC_VAL             0x49524F4EU  /* "IRON" */

#define LP_CMD_NONE                      0x00000000U
#define LP_CMD_SAMPLE_TELEMETRY          0x00000001U
#define LP_CMD_ENTER_SLEEP               0x00000002U
#define LP_CMD_RESET_STATS               0x00000003U

#define LP_ACK_ERROR_FLAG                0x80000000U
#define LP_TELEMETRY_HEADER_MASK         0x49520000U

/* PMU Inter-Core Communications Register (Write-Trigger WT) */
#define LP_PMU_HP_LP_COMM_ADDR           0x600B0184U
#define LP_PMU_LP_TRIGGER_HP_BIT         (1U << 30)

/* Telemetry loop delay iterations */
#define LP_TICK_DELAY_CYCLES             500U

/* Shared Mailbox Structure matching Task 4.2 specification */
typedef struct {
    volatile uint32_t magic;              /* 0x49524F4E ("IRON") */
    volatile uint32_t hp_to_lp_cmd;       /* Command opcode from HP to LP */
    volatile uint32_t lp_to_hp_ack;       /* Acknowledgment from LP to HP */
    volatile uint32_t wake_reason;        /* Wakeup reason code */
    volatile uint32_t periodic_wake_count;/* Autonomous wake count */
    volatile uint32_t sensor_telemetry_raw;/* Sensor telemetry */
    volatile uint32_t atomic_lock;        /* Synchronization spinlock */
} __attribute__((packed, aligned(4))) lp_shared_mailbox_t;

void lp_main(void);
void _lp_reset(void);

/*
 * 128-byte Vector Table placed at 0x50000000
 * In Direct Mode, mtvec base = 0x50000000 (TRM §3.3).
 * Exception handler is at mtvec + 0x0.
 * Single interrupt vector is at mtvec + 4 * 30 = 0x50000078.
 * All entries safely route to _lp_reset to recover gracefully from spurious traps.
 */
void __attribute__((section(".vectors"), naked)) _lp_vectors(void)
{
    asm volatile(
        ".option push\n"
        ".option norvc\n"
        "j _lp_reset\n"  /* Entry 0:  0x50000000 Exception vector */
        "j _lp_reset\n"  /* Entry 1:  0x50000004 */
        "j _lp_reset\n"  /* Entry 2:  0x50000008 */
        "j _lp_reset\n"  /* Entry 3:  0x5000000C */
        "j _lp_reset\n"  /* Entry 4:  0x50000010 */
        "j _lp_reset\n"  /* Entry 5:  0x50000014 */
        "j _lp_reset\n"  /* Entry 6:  0x50000018 */
        "j _lp_reset\n"  /* Entry 7:  0x5000001C */
        "j _lp_reset\n"  /* Entry 8:  0x50000020 */
        "j _lp_reset\n"  /* Entry 9:  0x50000024 */
        "j _lp_reset\n"  /* Entry 10: 0x50000028 */
        "j _lp_reset\n"  /* Entry 11: 0x5000002C */
        "j _lp_reset\n"  /* Entry 12: 0x50000030 */
        "j _lp_reset\n"  /* Entry 13: 0x50000034 */
        "j _lp_reset\n"  /* Entry 14: 0x50000038 */
        "j _lp_reset\n"  /* Entry 15: 0x5000003C */
        "j _lp_reset\n"  /* Entry 16: 0x50000040 */
        "j _lp_reset\n"  /* Entry 17: 0x50000044 */
        "j _lp_reset\n"  /* Entry 18: 0x50000048 */
        "j _lp_reset\n"  /* Entry 19: 0x5000004C */
        "j _lp_reset\n"  /* Entry 20: 0x50000050 */
        "j _lp_reset\n"  /* Entry 21: 0x50000054 */
        "j _lp_reset\n"  /* Entry 22: 0x50000058 */
        "j _lp_reset\n"  /* Entry 23: 0x5000005C */
        "j _lp_reset\n"  /* Entry 24: 0x50000060 */
        "j _lp_reset\n"  /* Entry 25: 0x50000064 */
        "j _lp_reset\n"  /* Entry 26: 0x50000068 */
        "j _lp_reset\n"  /* Entry 27: 0x5000006C */
        "j _lp_reset\n"  /* Entry 28: 0x50000070 */
        "j _lp_reset\n"  /* Entry 29: 0x50000074 */
        "j _lp_reset\n"  /* Entry 30: 0x50000078 Interrupt vector */
        "j _lp_reset\n"  /* Entry 31: 0x5000007C */
        ".option pop\n"
    );
}

/*
 * Reset Vector at 0x50000080
 * Hardware fetches instructions from address 0x50000080 after reset (ESP32-C6 TRM §3.3).
 */
void __attribute__((section(".text.entry"), naked)) _lp_reset(void)
{
    asm volatile(
        "la sp, _lp_stack_top\n"
        "la t0, _lp_vectors\n"
        "csrw mtvec, t0\n"
        "call lp_main\n"
        "1:\n"
        "wfi\n"
        "j 1b\n"
    );
}

void lp_main(void)
{
    volatile uint32_t *magic_ptr   = (volatile uint32_t *)LP_TEST_MAGIC_ADDR;
    volatile uint32_t *counter_ptr = (volatile uint32_t *)LP_TEST_COUNTER_ADDR;
    volatile uint32_t *comm_reg    = (volatile uint32_t *)LP_PMU_HP_LP_COMM_ADDR;
    volatile lp_shared_mailbox_t *mb = (volatile lp_shared_mailbox_t *)LP_MAILBOX_BASE_ADDR;

    /* 1. Write golden magic pattern to confirm successful boot (Task 4.1 Test 25 compat) */
    *magic_ptr = LP_TEST_MAGIC_VAL;

    /* 2. Initialize execution tick counter to 1 */
    *counter_ptr = 1U;

    /* 3. Initialize shared mailbox fields if uninitialized */
    if (mb->magic != LP_MAILBOX_MAGIC_VAL)
    {
        mb->magic = LP_MAILBOX_MAGIC_VAL;
        mb->hp_to_lp_cmd = LP_CMD_NONE;
        mb->lp_to_hp_ack = LP_CMD_NONE;
        mb->wake_reason = 0U;
        mb->periodic_wake_count = 0U;
        mb->sensor_telemetry_raw = 0U;
        mb->atomic_lock = 0U;
    }

    /* 4. Enforce memory barrier before signaling HP CPU */
    asm volatile("fence rw, rw" ::: "memory");

    /* 5. Pulse PMU LP_TRIGGER_HP (bit 30) - WT register write */
    *comm_reg = LP_PMU_LP_TRIGGER_HP_BIT;

    /* 6. Autonomous telemetry tick and mailbox servicing loop */
    while (1)
    {
        /* Check for pending command from HP CPU */
        uint32_t cmd = mb->hp_to_lp_cmd;
        if (cmd != LP_CMD_NONE)
        {
            if (cmd == LP_CMD_SAMPLE_TELEMETRY)
            {
                mb->periodic_wake_count++;
                mb->sensor_telemetry_raw = LP_TELEMETRY_HEADER_MASK | (mb->periodic_wake_count & 0xFFFFU);
                mb->lp_to_hp_ack = cmd;
                asm volatile("fence rw, rw" ::: "memory");
                *comm_reg = LP_PMU_LP_TRIGGER_HP_BIT;
                mb->hp_to_lp_cmd = LP_CMD_NONE;
            }
            else if (cmd == LP_CMD_ENTER_SLEEP)
            {
                mb->lp_to_hp_ack = cmd;
                asm volatile("fence rw, rw" ::: "memory");
                *comm_reg = LP_PMU_LP_TRIGGER_HP_BIT;
                mb->hp_to_lp_cmd = LP_CMD_NONE;
            }
            else if (cmd == LP_CMD_RESET_STATS)
            {
                mb->periodic_wake_count = 0U;
                mb->sensor_telemetry_raw = 0U;
                mb->lp_to_hp_ack = cmd;
                asm volatile("fence rw, rw" ::: "memory");
                *comm_reg = LP_PMU_LP_TRIGGER_HP_BIT;
                mb->hp_to_lp_cmd = LP_CMD_NONE;
            }
            else
            {
                mb->lp_to_hp_ack = LP_ACK_ERROR_FLAG | cmd;
                asm volatile("fence rw, rw" ::: "memory");
                *comm_reg = LP_PMU_LP_TRIGGER_HP_BIT;
                mb->hp_to_lp_cmd = LP_CMD_NONE;
            }
        }

        for (volatile uint32_t i = 0; i < LP_TICK_DELAY_CYCLES; i++)
        {
            asm volatile("nop");
        }
        (*counter_ptr)++;
    }
}
