/*
 * src/lp_core.c
 *
 * ESP32-C6 Low-Power (LP) RISC-V Coprocessor Lifecycle & PMU Handshake Driver
 * TRM Chapter 3 (§3.1-§3.9 Low-Power CPU) & Chapter 12 (§12.4 PMU)
 *
 * Implements hardware firmware deployment to LP SRAM (0x50000000), clock and reset
 * orchestration via LP_PERI, memory barrier synchronization, and PMU trigger handshakes.
 */

#include "lp_core.h"
#include "lp_firmware_image.h"
#include "string.h"

/* ========================================================================= */
/* Default Firmware Packaging                                                */
/* ========================================================================= */
static const lp_firmware_header_t s_default_firmware_header = {
    .magic       = LP_FIRMWARE_HEADER_MAGIC,
    .version     = LP_FIRMWARE_VERSION_1_0,
    .entry_point = LP_SRAM_ENTRY_ADDR,
    .size_bytes  = (uint32_t)g_lp_firmware_bin_len,
    .binary      = g_lp_firmware_bin
};

/* ========================================================================= */
/* Hardware vs Host Unit-Testing Emulation Abstraction                       */
/* ========================================================================= */
#if defined(__riscv)

static inline void lp_fence(void)
{
    asm volatile("fence rw, rw" ::: "memory");
}

static inline void lp_delay(uint32_t count)
{
    for (volatile uint32_t i = 0; i < count; i++)
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
    lp_fence();
}

static inline void reg_set_bits(volatile uint32_t *addr, uint32_t mask)
{
    *addr |= mask;
    lp_fence();
}

static inline void reg_clear_bits(volatile uint32_t *addr, uint32_t mask)
{
    *addr &= ~mask;
    lp_fence();
}

static inline volatile uint32_t *sram_ptr(uint32_t addr)
{
    return (volatile uint32_t *)((uintptr_t)addr);
}

#else

/* Host Emulation Environment */
static uint32_t s_mock_lp_peri_clk_en = 0U;
static uint32_t s_mock_lp_peri_reset_en = LP_PERI_RESET_EN_LP_CPU_RESET_EN_BIT;
static uint32_t s_mock_lp_peri_cpu = 0U;
static uint32_t s_mock_lp_clkrst_lpmem = 0U;
static uint32_t s_mock_lp_clkrst_clk_en = 0U;
static uint32_t s_mock_lp_aon_lpbus = 0U;
static uint32_t s_mock_lp_apm_func_ctrl = 0U;
static uint32_t s_mock_lp_apm0_func_ctrl = 0U;
static uint32_t s_mock_pmu_int_raw = 0U;
static uint32_t s_mock_pmu_hp_int_clr = 0U;
static uint32_t s_mock_pmu_lp_cpu_pwr0 = 0U;
static uint32_t s_mock_pmu_lp_cpu_pwr1 = 0U;
static uint32_t s_mock_pmu_hp_lp_comm = 0U;
static uint8_t  s_mock_lp_sram[LP_SRAM_SIZE_BYTES];

static inline void lp_fence(void)
{
    /* Host memory barrier */
    __sync_synchronize();
}

static inline void lp_delay(uint32_t count)
{
    (void)count;
}

static inline uint32_t reg_read(volatile uint32_t *addr)
{
    uintptr_t a = (uintptr_t)addr;
    if (a == (uintptr_t)LP_PERI_CLK_EN_REG) return s_mock_lp_peri_clk_en;
    if (a == (uintptr_t)LP_PERI_RESET_EN_REG) return s_mock_lp_peri_reset_en;
    if (a == (uintptr_t)LP_PERI_CPU_REG) return s_mock_lp_peri_cpu;
    if (a == (uintptr_t)LP_CLKRST_LPMEM_FORCE_REG) return s_mock_lp_clkrst_lpmem;
    if (a == (uintptr_t)LP_CLKRST_LP_CLK_EN_REG) return s_mock_lp_clkrst_clk_en;
    if (a == (uintptr_t)LP_AON_LPBUS_REG) return s_mock_lp_aon_lpbus;
    if (a == (uintptr_t)LP_APM_FUNC_CTRL_REG) return s_mock_lp_apm_func_ctrl;
    if (a == (uintptr_t)LP_APM0_FUNC_CTRL_REG) return s_mock_lp_apm0_func_ctrl;
    if (a == (uintptr_t)PMU_INT_RAW_REG) return s_mock_pmu_int_raw;
    if (a == (uintptr_t)PMU_HP_INT_CLR_REG) return s_mock_pmu_hp_int_clr;
    if (a == (uintptr_t)PMU_LP_CPU_PWR0_REG) return s_mock_pmu_lp_cpu_pwr0;
    if (a == (uintptr_t)PMU_LP_CPU_PWR1_REG) return s_mock_pmu_lp_cpu_pwr1;
    if (a == (uintptr_t)PMU_HP_LP_CPU_COMM_REG) return s_mock_pmu_hp_lp_comm;
    return 0U;
}

static inline void reg_write(volatile uint32_t *addr, uint32_t val)
{
    uintptr_t a = (uintptr_t)addr;
    if (a == (uintptr_t)LP_PERI_CLK_EN_REG) s_mock_lp_peri_clk_en = val;
    else if (a == (uintptr_t)LP_PERI_RESET_EN_REG) s_mock_lp_peri_reset_en = val;
    else if (a == (uintptr_t)LP_PERI_CPU_REG) s_mock_lp_peri_cpu = val;
    else if (a == (uintptr_t)LP_CLKRST_LPMEM_FORCE_REG) s_mock_lp_clkrst_lpmem = val;
    else if (a == (uintptr_t)LP_CLKRST_LP_CLK_EN_REG) s_mock_lp_clkrst_clk_en = val;
    else if (a == (uintptr_t)LP_AON_LPBUS_REG) s_mock_lp_aon_lpbus = val;
    else if (a == (uintptr_t)LP_APM_FUNC_CTRL_REG) s_mock_lp_apm_func_ctrl = val;
    else if (a == (uintptr_t)LP_APM0_FUNC_CTRL_REG) s_mock_lp_apm0_func_ctrl = val;
    else if (a == (uintptr_t)PMU_INT_RAW_REG) s_mock_pmu_int_raw = val;
    else if (a == (uintptr_t)PMU_HP_INT_CLR_REG) {
        s_mock_pmu_hp_int_clr = val;
        if (val & PMU_HP_INT_CLR_SW_INT_CLR_BIT) s_mock_pmu_int_raw &= ~PMU_INT_RAW_SW_INT_RAW_BIT;
    }
    else if (a == (uintptr_t)PMU_LP_CPU_PWR0_REG) s_mock_pmu_lp_cpu_pwr0 = val;
    else if (a == (uintptr_t)PMU_LP_CPU_PWR1_REG) s_mock_pmu_lp_cpu_pwr1 = val;
    else if (a == (uintptr_t)PMU_HP_LP_CPU_COMM_REG) s_mock_pmu_hp_lp_comm = val;
    else if (a == (uintptr_t)PMU_LP_INT_CLR_REG) {
        if (val & PMU_LP_INT_CLR_HP_SW_TRIGGER_INT_CLR_BIT) s_mock_pmu_int_raw &= ~PMU_LP_INT_RAW_HP_SW_TRIGGER_INT_RAW_BIT;
    }
}

static inline void reg_set_bits(volatile uint32_t *addr, uint32_t mask)
{
    reg_write(addr, reg_read(addr) | mask);
}

static inline void reg_clear_bits(volatile uint32_t *addr, uint32_t mask)
{
    reg_write(addr, reg_read(addr) & ~mask);
}

static inline volatile uint32_t *sram_ptr(uint32_t addr)
{
    uint32_t offset = addr - LP_SRAM_BASE_ADDR;
    if (offset < LP_SRAM_SIZE_BYTES)
    {
        return (volatile uint32_t *)((uintptr_t)(s_mock_lp_sram + offset));
    }
    return (volatile uint32_t *)((uintptr_t)s_mock_lp_sram);
}

#endif /* __riscv */

/* ========================================================================= */
/* Driver Lifecycle Implementation                                           */
/* ========================================================================= */

int lp_core_stop(void);

int lp_core_init(void)
{
    /* 1. Ensure LP SRAM clock is forced active in LP_CLKRST */
    reg_set_bits(LP_CLKRST_LPMEM_FORCE_REG, LP_CLKRST_LPMEM_FORCE_LPMEM_CLK_FORCE_ON_BIT);

    /* 2. Force LP_FAST_CLK pass clock gate in LP_CLKRST */
    reg_set_bits(LP_CLKRST_LP_CLK_EN_REG, LP_CLKRST_FAST_ORI_GATE_BIT);

    /* 3. Disable LP_APM and LP_APM0 master filters to permit LP core SRAM and peripheral access */
    reg_write(LP_APM_FUNC_CTRL_REG, 0U);
    reg_write(LP_APM0_FUNC_CTRL_REG, 0U);

    /* 4. Switch LP SRAM to low-speed mode so LP CPU can access memory (TRM §5.3.2.2) */
    reg_clear_bits(LP_AON_LPBUS_REG, LP_AON_LPBUS_FAST_MEM_MUX_SEL_M);
    reg_set_bits(LP_AON_LPBUS_REG, LP_AON_LPBUS_FAST_MEM_MUX_SEL_UPDATE_M);

    /* 5. Force stall LP CPU and hold in reset */
    lp_core_stop();

    /* 6. Enable debug module on LP core */
    reg_clear_bits(LP_PERI_CPU_REG, LP_PERI_CPU_LPCORE_DBGM_BIT);

    /* 7. Clear PMU communication trigger bits and clear any latched PMU raw interrupts */
    reg_clear_bits(PMU_HP_LP_CPU_COMM_REG,
                   PMU_HP_LP_CPU_COMM_HP_TRIGGER_LP_BIT | PMU_HP_LP_CPU_COMM_LP_TRIGGER_HP_BIT);
    reg_write(PMU_HP_INT_CLR_REG, PMU_HP_INT_CLR_SW_INT_CLR_BIT | PMU_HP_INT_CLR_LP_CPU_EXC_INT_CLR_BIT);
    reg_write(PMU_LP_INT_CLR_REG, PMU_LP_INT_CLR_HP_SW_TRIGGER_INT_CLR_BIT | PMU_LP_INT_CLR_LP_CPU_WAKEUP_INT_CLR_BIT);

#if !defined(__riscv)
    s_mock_lp_peri_clk_en = 0U;
    s_mock_lp_peri_reset_en = LP_PERI_RESET_EN_LP_CPU_RESET_EN_BIT;
    s_mock_pmu_lp_cpu_pwr0 = PMU_LP_CPU_PWR0_FORCE_STALL_BIT;
    s_mock_pmu_lp_cpu_pwr1 = 0U;
    s_mock_pmu_hp_lp_comm = 0U;
    s_mock_pmu_int_raw = 0U;
    s_mock_pmu_hp_int_clr = 0U;
#endif

    return LP_CORE_OK;
}

int lp_core_load_firmware(const uint8_t *binary, uint32_t size)
{
    if (binary == NULL)
    {
        return LP_CORE_ERR_NULL_PTR;
    }
    if (size == 0U || size > (LP_SRAM_SIZE_BYTES / 2U))
    {
        return LP_CORE_ERR_INVALID_SIZE;
    }

    /* Enforce that LP core is stopped and stalled before overwriting memory */
    lp_core_stop();

    /* Clear handshake mailbox words */
    *sram_ptr(LP_TEST_MAGIC_ADDR) = 0U;
    *sram_ptr(LP_TEST_COUNTER_ADDR) = 0U;

#if defined(__riscv)
    memcpy((void *)LP_SRAM_BASE_ADDR, binary, size);
#else
    memcpy((void *)s_mock_lp_sram, binary, size);
#endif

    lp_fence();
    return LP_CORE_OK;
}

int lp_core_load_header(const lp_firmware_header_t *header)
{
    if (header == NULL)
    {
        return LP_CORE_ERR_NULL_PTR;
    }
    if (header->magic != LP_FIRMWARE_HEADER_MAGIC)
    {
        return LP_CORE_ERR_INVALID_MAGIC;
    }
    if (header->entry_point != LP_SRAM_ENTRY_ADDR)
    {
        return LP_CORE_ERR_INVALID_ENTRY;
    }

    return lp_core_load_firmware(header->binary, header->size_bytes);
}

int lp_core_start(void)
{
    /* 1. Ensure LP SRAM low-speed mode is active (TRM §5.3.2.2) */
    reg_clear_bits(LP_AON_LPBUS_REG, LP_AON_LPBUS_FAST_MEM_MUX_SEL_M);
    reg_set_bits(LP_AON_LPBUS_REG, LP_AON_LPBUS_FAST_MEM_MUX_SEL_UPDATE_M);

    /* 2. Ungate LP CPU clock in LP_PERI */
    reg_set_bits(LP_PERI_CLK_EN_REG, LP_PERI_CLK_EN_LP_CPU_CK_EN_BIT);
    lp_delay(LP_CORE_RESET_HOLD_CYCLES);

    /* 3. Release LP CPU reset in LP_PERI */
    reg_clear_bits(LP_PERI_RESET_EN_REG, LP_PERI_RESET_EN_LP_CPU_RESET_EN_BIT);
    lp_delay(LP_CORE_RESET_HOLD_CYCLES);

    /* 4. Ensure PMU HP CPU wakeup trigger is enabled */
    reg_set_bits(PMU_LP_CPU_PWR1_REG, PMU_LP_CPU_PWR1_WAKEUP_EN_HP_BIT);

    /* 5. Clear force stall in PMU to allow LP CPU to fetch instructions */
    reg_clear_bits(PMU_LP_CPU_PWR0_REG, PMU_LP_CPU_PWR0_FORCE_STALL_BIT);

    /* 6. Pulse PMU HP->LP wake trigger (TRM §3.9.2) */
    reg_write(PMU_HP_LP_CPU_COMM_REG, PMU_HP_LP_CPU_COMM_HP_TRIGGER_LP_BIT);

#if !defined(__riscv)
    s_mock_lp_peri_clk_en |= LP_PERI_CLK_EN_LP_CPU_CK_EN_BIT;
    s_mock_lp_peri_reset_en &= ~LP_PERI_RESET_EN_LP_CPU_RESET_EN_BIT;
    s_mock_pmu_lp_cpu_pwr0 &= ~PMU_LP_CPU_PWR0_FORCE_STALL_BIT;
    s_mock_pmu_lp_cpu_pwr1 |= PMU_LP_CPU_PWR1_WAKEUP_EN_HP_BIT;

    /* Host Mock Emulation: simulate LP core boot & execution */
    *sram_ptr(LP_TEST_MAGIC_ADDR) = LP_TEST_MAGIC_EXPECTED;
    *sram_ptr(LP_TEST_COUNTER_ADDR) = 1U;
    s_mock_pmu_hp_lp_comm |= PMU_HP_LP_CPU_COMM_LP_TRIGGER_HP_BIT;
    s_mock_pmu_int_raw |= PMU_INT_RAW_SW_INT_RAW_BIT;
#endif

    return LP_CORE_OK;
}

int lp_core_stop(void)
{
    /* 1. Force stall LP CPU to halt execution immediately */
    reg_set_bits(PMU_LP_CPU_PWR0_REG, PMU_LP_CPU_PWR0_FORCE_STALL_BIT);
    lp_delay(LP_CORE_RESET_HOLD_CYCLES);

    /* 2. Assert LP CPU reset in LP_PERI */
    reg_set_bits(LP_PERI_RESET_EN_REG, LP_PERI_RESET_EN_LP_CPU_RESET_EN_BIT);
    lp_delay(LP_CORE_RESET_HOLD_CYCLES);

    /* 3. Gate off LP CPU clock */
    reg_clear_bits(LP_PERI_CLK_EN_REG, LP_PERI_CLK_EN_LP_CPU_CK_EN_BIT);

    /* 4. Disable PMU wakeup trigger */
    reg_clear_bits(PMU_LP_CPU_PWR1_REG, PMU_LP_CPU_PWR1_WAKEUP_EN_HP_BIT);

#if !defined(__riscv)
    s_mock_lp_peri_clk_en &= ~LP_PERI_CLK_EN_LP_CPU_CK_EN_BIT;
    s_mock_lp_peri_reset_en |= LP_PERI_RESET_EN_LP_CPU_RESET_EN_BIT;
    s_mock_pmu_lp_cpu_pwr0 |= PMU_LP_CPU_PWR0_FORCE_STALL_BIT;
    s_mock_pmu_lp_cpu_pwr1 &= ~PMU_LP_CPU_PWR1_WAKEUP_EN_HP_BIT;
#endif

    return LP_CORE_OK;
}

int lp_core_trigger_lp(void)
{
    /* Pulse HP_TRIGGER_LP (bit 31) - WT register write */
    reg_write(PMU_HP_LP_CPU_COMM_REG, PMU_HP_LP_CPU_COMM_HP_TRIGGER_LP_BIT);
    return LP_CORE_OK;
}

uint32_t lp_core_get_lp_trigger(void)
{
    uint32_t comm = reg_read(PMU_HP_LP_CPU_COMM_REG);
    uint32_t raw = reg_read(PMU_INT_RAW_REG);
    return ((comm & PMU_HP_LP_CPU_COMM_LP_TRIGGER_HP_BIT) != 0U ||
            (raw & PMU_INT_RAW_SW_INT_RAW_BIT) != 0U) ? 1U : 0U;
}

void lp_core_clear_lp_trigger(void)
{
    reg_clear_bits(PMU_HP_LP_CPU_COMM_REG, PMU_HP_LP_CPU_COMM_LP_TRIGGER_HP_BIT);
    reg_write(PMU_HP_INT_CLR_REG, PMU_HP_INT_CLR_SW_INT_CLR_BIT);
}

int lp_core_wait_handshake(uint32_t timeout_cycles)
{
    for (uint32_t cycle = 0; cycle < timeout_cycles; cycle++)
    {
        uint32_t magic = lp_core_read_magic();
        uint32_t trigger = lp_core_get_lp_trigger();

        if (magic == LP_TEST_MAGIC_EXPECTED || trigger != 0U)
        {
            return LP_CORE_OK;
        }

#if defined(__riscv)
        asm volatile("nop");
#endif
    }

    return LP_CORE_ERR_TIMEOUT;
}

bool lp_core_is_running(void)
{
    uint32_t clk = reg_read(LP_PERI_CLK_EN_REG);
    uint32_t rst = reg_read(LP_PERI_RESET_EN_REG);
    uint32_t pwr0 = reg_read(PMU_LP_CPU_PWR0_REG);

    bool clk_en = (clk & LP_PERI_CLK_EN_LP_CPU_CK_EN_BIT) != 0U;
    bool rst_held = (rst & LP_PERI_RESET_EN_LP_CPU_RESET_EN_BIT) != 0U;
    bool force_stalled = (pwr0 & PMU_LP_CPU_PWR0_FORCE_STALL_BIT) != 0U;

    return clk_en && (!rst_held) && (!force_stalled);
}

uint32_t lp_core_read_counter(void)
{
    return *sram_ptr(LP_TEST_COUNTER_ADDR);
}

uint32_t lp_core_read_magic(void)
{
    return *sram_ptr(LP_TEST_MAGIC_ADDR);
}

int lp_core_get_telemetry(lp_core_telemetry_t *telem)
{
    if (telem == NULL)
    {
        return LP_CORE_ERR_NULL_PTR;
    }

    uint32_t clk = reg_read(LP_PERI_CLK_EN_REG);
    uint32_t rst = reg_read(LP_PERI_RESET_EN_REG);
    uint32_t pwr0 = reg_read(PMU_LP_CPU_PWR0_REG);
    uint32_t comm = reg_read(PMU_HP_LP_CPU_COMM_REG);

    telem->clock_enabled     = (clk & LP_PERI_CLK_EN_LP_CPU_CK_EN_BIT) != 0U;
    telem->in_reset          = ((rst & LP_PERI_RESET_EN_LP_CPU_RESET_EN_BIT) != 0U) ||
                               ((pwr0 & PMU_LP_CPU_PWR0_FORCE_STALL_BIT) != 0U);
    telem->is_running        = telem->clock_enabled && (!telem->in_reset);
    telem->hp_trigger_active = (comm & PMU_HP_LP_CPU_COMM_HP_TRIGGER_LP_BIT) != 0U;
    telem->lp_trigger_active = (lp_core_get_lp_trigger() != 0U);
    telem->magic_readback    = lp_core_read_magic();
    telem->counter_readback  = lp_core_read_counter();

    return LP_CORE_OK;
}

const lp_firmware_header_t *lp_core_get_default_firmware(void)
{
    return &s_default_firmware_header;
}
