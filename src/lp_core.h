/*
 * src/lp_core.h
 *
 * ESP32-C6 Low-Power (LP) RISC-V Coprocessor Lifecycle & PMU Handshake Driver
 * TRM Chapter 3 (§3.1-§3.9 Low-Power CPU) & Chapter 12 (§12.4 PMU)
 *
 * Provides firmware loading into retained LP SRAM (0x50000000), clock ungating,
 * reset control via LP_PERI, and bidirectional PMU hardware trigger handshakes.
 */

#ifndef LP_CORE_H
#define LP_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================= */
/* Hardware Memory Cartography & Base Addresses                              */
/* ========================================================================= */
#define LP_SRAM_BASE_ADDR               0x50000000U
#define LP_SRAM_SIZE_BYTES              0x00004000U  /* 16 KB total LP SRAM */
#define LP_SRAM_ENTRY_ADDR              0x50000080U  /* Hardware reset fetch vector (TRM §3.3) */
#define LP_STACK_TOP_ADDR               0x50002FF0U  /* Top of LP stack partition (below mailbox) */
#define LP_SHARED_MEM_BASE_ADDR         0x50003000U  /* Retained mailbox boundary */

#define LP_TEST_MAGIC_ADDR              0x50002FFCU  /* Diagnostic handshake word */
#define LP_TEST_COUNTER_ADDR            0x50002FF8U  /* Autonomous tick counter */
#define LP_TEST_MAGIC_EXPECTED          0xCAFEBABEU  /* Golden boot handshake constant */

#define LP_FIRMWARE_HEADER_MAGIC        0x49524F4EU  /* "IRON" */
#define LP_FIRMWARE_VERSION_1_0         0x00010000U  /* Version 1.0 */

/* ========================================================================= */
/* Peripheral Register Bases                                                 */
/* ========================================================================= */
#define LP_PERI_BASE_ADDR               0x600B2800U
#define LP_CLKRST_BASE_ADDR             0x600B0400U
#define LP_AON_BASE_ADDR                0x600B1000U
#define PMU_BASE_ADDR                   0x600B0000U
#define LP_APM_BASE_ADDR                0x600B3800U
#define LP_APM0_BASE_ADDR               0x60099800U

/* ========================================================================= */
/* Register Offsets                                                          */
/* ========================================================================= */
#define LP_PERI_CLK_EN_OFFSET           0x00U
#define LP_PERI_RESET_EN_OFFSET         0x04U
#define LP_PERI_CPU_OFFSET              0x0CU

#define LP_CLKRST_LP_CLK_EN_OFFSET      0x08U
#define LP_CLKRST_LPMEM_FORCE_OFFSET    0x24U

#define LP_AON_LPBUS_OFFSET             0x48U

#define PMU_INT_RAW_OFFSET              0x15CU
#define PMU_HP_INT_CLR_OFFSET           0x168U
#define PMU_LP_INT_RAW_OFFSET           0x16CU
#define PMU_LP_INT_CLR_OFFSET           0x178U
#define PMU_LP_CPU_PWR0_OFFSET          0x17CU
#define PMU_LP_CPU_PWR1_OFFSET          0x180U
#define PMU_HP_LP_CPU_COMM_OFFSET       0x184U

#define LP_APM_FUNC_CTRL_OFFSET         0xC4U
#define LP_APM0_FUNC_CTRL_OFFSET        0xC4U

/* ========================================================================= */
/* Parameterized Register Accessor Macros (AGENTS.md Compliance)             */
/* ========================================================================= */
#define LP_PERI_REG(off)                ((volatile uint32_t *)((uintptr_t)(LP_PERI_BASE_ADDR + (off))))
#define LP_CLKRST_REG(off)              ((volatile uint32_t *)((uintptr_t)(LP_CLKRST_BASE_ADDR + (off))))
#define LP_AON_REG(off)                 ((volatile uint32_t *)((uintptr_t)(LP_AON_BASE_ADDR + (off))))
#define PMU_REG(off)                    ((volatile uint32_t *)((uintptr_t)(PMU_BASE_ADDR + (off))))
#define LP_APM_REG(off)                 ((volatile uint32_t *)((uintptr_t)(LP_APM_BASE_ADDR + (off))))
#define LP_APM0_REG(off)                ((volatile uint32_t *)((uintptr_t)(LP_APM0_BASE_ADDR + (off))))

#ifndef LP_PERI_CLK_EN_REG
#define LP_PERI_CLK_EN_REG              LP_PERI_REG(LP_PERI_CLK_EN_OFFSET)
#endif
#ifndef LP_PERI_RESET_EN_REG
#define LP_PERI_RESET_EN_REG            LP_PERI_REG(LP_PERI_RESET_EN_OFFSET)
#endif
#ifndef LP_PERI_CPU_REG
#define LP_PERI_CPU_REG                 LP_PERI_REG(LP_PERI_CPU_OFFSET)
#endif
#ifndef LP_CLKRST_LP_CLK_EN_REG
#define LP_CLKRST_LP_CLK_EN_REG         LP_CLKRST_REG(LP_CLKRST_LP_CLK_EN_OFFSET)
#endif
#ifndef LP_CLKRST_LPMEM_FORCE_REG
#define LP_CLKRST_LPMEM_FORCE_REG       LP_CLKRST_REG(LP_CLKRST_LPMEM_FORCE_OFFSET)
#endif
#ifndef LP_AON_LPBUS_REG
#define LP_AON_LPBUS_REG                LP_AON_REG(LP_AON_LPBUS_OFFSET)
#endif
#ifndef PMU_INT_RAW_REG
#define PMU_INT_RAW_REG                 PMU_REG(PMU_INT_RAW_OFFSET)
#endif
#ifndef PMU_HP_INT_CLR_REG
#define PMU_HP_INT_CLR_REG              PMU_REG(PMU_HP_INT_CLR_OFFSET)
#endif
#ifndef PMU_LP_INT_RAW_REG
#define PMU_LP_INT_RAW_REG              PMU_REG(PMU_LP_INT_RAW_OFFSET)
#endif
#ifndef PMU_LP_INT_CLR_REG
#define PMU_LP_INT_CLR_REG              PMU_REG(PMU_LP_INT_CLR_OFFSET)
#endif
#ifndef PMU_LP_CPU_PWR0_REG
#define PMU_LP_CPU_PWR0_REG             PMU_REG(PMU_LP_CPU_PWR0_OFFSET)
#endif
#ifndef PMU_LP_CPU_PWR1_REG
#define PMU_LP_CPU_PWR1_REG             PMU_REG(PMU_LP_CPU_PWR1_OFFSET)
#endif
#ifndef PMU_HP_LP_CPU_COMM_REG
#define PMU_HP_LP_CPU_COMM_REG          PMU_REG(PMU_HP_LP_CPU_COMM_OFFSET)
#endif
#ifndef LP_APM_FUNC_CTRL_REG
#define LP_APM_FUNC_CTRL_REG            LP_APM_REG(LP_APM_FUNC_CTRL_OFFSET)
#endif
#ifndef LP_APM0_FUNC_CTRL_REG
#define LP_APM0_FUNC_CTRL_REG           LP_APM0_REG(LP_APM0_FUNC_CTRL_OFFSET)
#endif

/* ========================================================================= */
/* Architectural Bitfield Masks & Shifts                                     */
/* ========================================================================= */
/* LP_PERI_CLK_EN_REG: bit 31 LP_CPU_CK_EN */
#define LP_PERI_CLK_EN_LP_CPU_CK_EN_BIT (1U << 31)

/* LP_PERI_RESET_EN_REG: bit 31 LP_CPU_RESET_EN (1=Reset held, 0=Reset released) */
#define LP_PERI_RESET_EN_LP_CPU_RESET_EN_BIT (1U << 31)

/* LP_PERI_CPU_REG: bit 31 LPCORE_DBGM_UNAVALIABLE (0=available, 1=unavailable) */
#define LP_PERI_CPU_LPCORE_DBGM_BIT     (1U << 31)

/* LP_CLKRST_LP_CLK_EN_REG: bit 31 FAST_ORI_GATE */
#define LP_CLKRST_FAST_ORI_GATE_BIT     (1U << 31)

/* LP_CLKRST_LPMEM_FORCE_REG: bit 31 LPMEM_CLK_FORCE_ON */
#define LP_CLKRST_LPMEM_FORCE_LPMEM_CLK_FORCE_ON_BIT (1U << 31)

/* LP_AON_LPBUS_REG: bit 31 FAST_MEM_MUX_SEL (0=low-speed for LP core, 1=high-speed) */
#define LP_AON_LPBUS_FAST_MEM_MUX_SEL_M (1U << 31)
/* LP_AON_LPBUS_REG: bit 30 FAST_MEM_MUX_SEL_UPDATE (pulse 1 to apply) */
#define LP_AON_LPBUS_FAST_MEM_MUX_SEL_UPDATE_M (1U << 30)

/* PMU_INT_RAW_REG: bit 29 SW_INT_RAW, bit 27 LP_CPU_EXC_INT_RAW */
#define PMU_INT_RAW_SW_INT_RAW_BIT      (1U << 29)
#define PMU_INT_RAW_LP_CPU_EXC_INT_RAW_BIT (1U << 27)

/* PMU_LP_INT_RAW_REG: bit 31 HP_SW_TRIGGER_INT_RAW, bit 20 LP_CPU_WAKEUP_INT_RAW */
#define PMU_LP_INT_RAW_HP_SW_TRIGGER_INT_RAW_BIT (1U << 31)
#define PMU_LP_INT_RAW_LP_CPU_WAKEUP_INT_RAW_BIT (1U << 20)

/* PMU_LP_INT_CLR_REG: bit 31 HP_SW_TRIGGER_INT_CLR, bit 20 LP_CPU_WAKEUP_INT_CLR */
#define PMU_LP_INT_CLR_HP_SW_TRIGGER_INT_CLR_BIT (1U << 31)
#define PMU_LP_INT_CLR_LP_CPU_WAKEUP_INT_CLR_BIT (1U << 20)

/* PMU_HP_INT_CLR_REG: bit 29 SW_INT_CLR, bit 27 LP_CPU_EXC_INT_CLR */
#define PMU_HP_INT_CLR_SW_INT_CLR_BIT   (1U << 29)
#define PMU_HP_INT_CLR_LP_CPU_EXC_INT_CLR_BIT (1U << 27)

/* PMU_LP_CPU_PWR0_REG */
#define PMU_LP_CPU_PWR0_FORCE_STALL_BIT (1U << 18)
#define PMU_LP_CPU_PWR0_SLP_STALL_EN_BIT (1U << 29)
#define PMU_LP_CPU_PWR0_SLP_RESET_EN_BIT (1U << 30)

/* PMU_LP_CPU_PWR1_REG */
#define PMU_LP_CPU_PWR1_WAKEUP_EN_HP_BIT (1U << 0)

/* PMU_HP_LP_CPU_COMM_REG */
#define PMU_HP_LP_CPU_COMM_HP_TRIGGER_LP_BIT (1U << 31)
#define PMU_HP_LP_CPU_COMM_LP_TRIGGER_HP_BIT (1U << 30)

/* ========================================================================= */
/* Operational Timing & Timeout Limits                                       */
/* ========================================================================= */
#define LP_CORE_HANDSHAKE_TIMEOUT_CYCLES 200000U  /* ~1.25 ms @ 160 MHz CPU PLL */
#define LP_CORE_RESET_HOLD_CYCLES        1000U    /* ~6.25 us settling margin */
#define LP_CORE_SPIN_ADVANCE_CYCLES      100000U  /* ~625 us counter advance spin */

/* ========================================================================= */
/* Driver Status & Error Enumeration                                         */
/* ========================================================================= */
typedef enum {
    LP_CORE_OK                  = 0,
    LP_CORE_ERR_NULL_PTR        = -1,
    LP_CORE_ERR_INVALID_SIZE    = -2,
    LP_CORE_ERR_INVALID_MAGIC   = -3,
    LP_CORE_ERR_TIMEOUT         = -4,
    LP_CORE_ERR_NOT_RUNNING     = -5,
    LP_CORE_ERR_ALREADY_RUNNING = -6,
    LP_CORE_ERR_INVALID_ENTRY   = -7
} lp_core_status_t;

/* ========================================================================= */
/* Firmware Image Header Structure                                           */
/* ========================================================================= */
typedef struct {
    uint32_t magic;           /* 0x49524F4E ("IRON") */
    uint32_t version;         /* Schema version (0x00010000) */
    uint32_t entry_point;     /* 0x50000000 */
    uint32_t size_bytes;      /* Binary image size in bytes */
    const uint8_t *binary;    /* Pointer to binary image in Flash/DRAM */
} lp_firmware_header_t;

/* ========================================================================= */
/* LP Core Telemetry Snapshot                                                */
/* ========================================================================= */
typedef struct {
    bool is_running;
    bool clock_enabled;
    bool in_reset;
    bool hp_trigger_active;
    bool lp_trigger_active;
    uint32_t magic_readback;
    uint32_t counter_readback;
} lp_core_telemetry_t;

/* ========================================================================= */
/* Public Driver API Declarations                                            */
/* ========================================================================= */

/*
 * lp_core_init
 * Disables LP core clock, asserts reset, and forces LP SRAM clock active.
 */
int lp_core_init(void);

/*
 * lp_core_load_firmware
 * Writes binary image into LP SRAM starting at 0x50000000 with boundary verification.
 */
int lp_core_load_firmware(const uint8_t *binary, uint32_t size);

/*
 * lp_core_load_header
 * Verifies magic, version, and entry point before loading binary payload.
 */
int lp_core_load_header(const lp_firmware_header_t *header);

/*
 * lp_core_start
 * Ungates LP CPU clock and releases reset, allowing LP execution from 0x50000000.
 */
int lp_core_start(void);

/*
 * lp_core_stop
 * Asserts LP CPU reset and disables LP CPU clock.
 */
int lp_core_stop(void);

/*
 * lp_core_trigger_lp
 * Asserts and clears PMU_HP_TRIGGER_LP (bit 31) to pulse-notify the LP core.
 */
int lp_core_trigger_lp(void);

/*
 * lp_core_get_lp_trigger
 * Returns 1 if LP core has asserted PMU_LP_TRIGGER_HP (bit 30), 0 otherwise.
 */
uint32_t lp_core_get_lp_trigger(void);

/*
 * lp_core_clear_lp_trigger
 * Clears PMU_LP_TRIGGER_HP (bit 30) acknowledge flag.
 */
void lp_core_clear_lp_trigger(void);

/*
 * lp_core_wait_handshake
 * Polls for LP core boot confirmation (magic word + PMU trigger) with cycle ceiling.
 */
int lp_core_wait_handshake(uint32_t timeout_cycles);

/*
 * lp_core_is_running
 * Returns true if LP core is clocked and out of reset.
 */
bool lp_core_is_running(void);

/*
 * lp_core_read_counter
 * Reads 32-bit execution tick counter maintained by LP core at 0x50002FF8.
 */
uint32_t lp_core_read_counter(void);

/*
 * lp_core_read_magic
 * Reads handshake magic word at 0x50002FFC.
 */
uint32_t lp_core_read_magic(void);

/*
 * lp_core_get_telemetry
 * Populates snapshot of LP control register states and SRAM indicators.
 */
int lp_core_get_telemetry(lp_core_telemetry_t *telem);

/*
 * lp_core_get_default_firmware
 * Returns pointer to embedded default diagnostic firmware header.
 */
const lp_firmware_header_t *lp_core_get_default_firmware(void);

#endif // LP_CORE_H
