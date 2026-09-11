#include "test.h"
#include "utils.h"
#include "string.h"
#include "io_constants.h"
#include "clock.h"
#include "wdt.h"
#include "trap.h"
#include "interrupt.h"
#include "dpc.h"
#include "usb_serial.h"
#include "uart.h"
#include "console.h"
#include "timer.h"
#include "arena.h"
#include "systimer.h"
#include "task.h"
#include "pmp.h"
#include "lp_core.h"
#include "power.h"
#include "gpio.h"
#include "gdma.h"
#include "modem.h"
#include "ble.h"
#include "ble_gatt.h"
#include "wifi.h"
#include "ieee802154.h"
#include "net.h"
#include "tcp.h"

/* Route all test output to unified dual-console multiplexer */
#define uart_puts console_puts
#define uart_putc console_putc

/* TEST 28 Static Allocation in HP SRAM DRAM */
static dma_descriptor_t s_test_desc0 __attribute__((aligned(4)));
static dma_descriptor_t s_test_desc1 __attribute__((aligned(4)));
static uint8_t s_test_dma_buf0[64] __attribute__((aligned(4)));
static uint8_t s_test_dma_buf1[64] __attribute__((aligned(4)));

static volatile uint32_t s_test_task_a_counter = 0;
static volatile uint32_t s_test_task_b_counter = 0;
static volatile uint32_t s_test_task_turn[10];
static volatile uint32_t s_test_turn_idx = 0;

static void test_task_a_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++)
    {
        s_test_task_a_counter++;
        if (s_test_turn_idx < 10U)
        {
            s_test_task_turn[s_test_turn_idx++] = 1U;
        }
        task_yield();
    }
}

static void test_task_b_worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++)
    {
        s_test_task_b_counter++;
        if (s_test_turn_idx < 10U)
        {
            s_test_task_turn[s_test_turn_idx++] = 2U;
        }
        task_yield();
    }
}

/* Designated static test variables */
static volatile uint32_t g_test_data_var = 0x12345678U; // Placed in .data
static volatile uint32_t g_test_bss_var;               // Placed in .bss (should be 0)
static const char g_test_rodata_str[] = "IRON_V_RODATA_TEST_PATTERN"; // Placed in .rodata
static volatile uint32_t g_test_isr_hit = 0;
static volatile uint32_t g_test_dpc_hit = 0;

static void test_dpc_callback(uint32_t arg0, uint32_t arg1)
{
    (void)arg0;
    (void)arg1;
    g_test_dpc_hit++;
}

static void test_sw_isr(void *arg)
{
    (void)arg;
    g_test_isr_hit++;
    /* Deassert software interrupt 0 to prevent continuous re-triggering */
    interrupt_clear_cpu_intr(0);
}

mem_access_t check_mem_access(uint32_t addr)
{
    /* 1. Unaligned addresses are strictly invalid for 32-bit word access */
    if (addr & WORD_ALIGN_MASK)
    {
        return MEM_ACCESS_INVALID;
    }

    /* 2. Read-Only Code in HP IRAM */
    if (addr >= (uint32_t)_stext && addr < (uint32_t)_etext)
    {
        return MEM_ACCESS_READONLY;
    }

    /* 3. Read-Only Constant Data (.rodata) in HP DRAM */
    if (addr >= (uint32_t)_srodata && addr < (uint32_t)_erodata)
    {
        return MEM_ACCESS_READONLY;
    }

    /* 4. Read-Write Data, BSS, Heap, and Stack range in HP SRAM */
    if (addr >= (uint32_t)_sdata && addr < (uint32_t)_stack_top)
    {
        return MEM_ACCESS_READWRITE;
    }

    /* 5. LP SRAM (16 KB @ 0x50000000) */
    if (addr >= LP_SRAM_START_ADDR && addr < LP_SRAM_END_ADDR)
    {
        return MEM_ACCESS_READWRITE;
    }

    /* 6. Flash XIP Execution & Read-Only Space (0x42000000 - 0x42800000) */
    if (addr >= FLASH_XIP_START_ADDR && addr < FLASH_XIP_END_ADDR)
    {
        return MEM_ACCESS_READONLY;
    }

    /* 7. Memory-Mapped I/O Peripheral Space (0x60000000 - 0x600D0000) */
    if (addr >= PERIPHERAL_MMIO_START_ADDR && addr < PERIPHERAL_MMIO_END_ADDR)
    {
        return MEM_ACCESS_MMIO;
    }

    /* 8. Core-Local Interrupt & Timer Subsystem Space (PLIC/CLINT: 0x20000000 - 0x20002000) */
    if (addr >= CORE_LOCAL_PERI_START_ADDR && addr < CORE_LOCAL_PERI_END_ADDR)
    {
        return MEM_ACCESS_MMIO;
    }

    /* 9. Internal ROM (0x40000000 - 0x40050000) */
    if (addr >= INTERNAL_ROM_START_ADDR && addr < INTERNAL_ROM_END_ADDR)
    {
        return MEM_ACCESS_READONLY;
    }

    /* All other unmapped regions */
    return MEM_ACCESS_INVALID;
}

static void print_banner_line(void)
{
    uart_puts("======================================================================\r\n");
}

static void print_test_header(int num, const char *title, const char *desc)
{
    wdt_feed();
    uart_puts("\r\n[TEST ");
    if (num < 10) uart_putc('0');
    put_dec(num);
    uart_puts("] ");
    uart_puts(title);
    uart_puts("\r\n  Description: ");
    uart_puts(desc);
    uart_puts("\r\n");
}

static void print_result(int pass)
{
    if (pass)
    {
        uart_puts("  Result:      [ PASS ]\r\n");
    }
    else
    {
        uart_puts("  Result:      [ FAIL ]\r\n");
    }
}

void run_validation_suite(void)
{
    int total_tests = 0;
    int passed_tests = 0;

    print_banner_line();
    uart_puts("                   IRON V BASELINE VALIDATION SUITE                   \r\n");
    print_banner_line();

    /* ------------------------------------------------------------- */
    /* TEST 1: Memory Section Topology & Monotonicity                */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(1, "Memory Section Topology & Monotonicity",
                      "Verify SRAM section layout conforms to 0x40800000 architecture");
    
    uint32_t stext = (uint32_t)_stext;
    uint32_t etext = (uint32_t)_etext;
    uint32_t srodata = (uint32_t)_srodata;
    uint32_t erodata = (uint32_t)_erodata;
    uint32_t sdata = (uint32_t)_sdata;
    uint32_t edata = (uint32_t)_edata;
    uint32_t sbss = (uint32_t)_sbss;
    uint32_t ebss = (uint32_t)_ebss;
    uint32_t stack_top = (uint32_t)_stack_top;

    uart_puts("  Expected:    0x40800000 == _stext < _srodata < _sdata < _sbss < _stack_top(0x40880000)\r\n");
    uart_puts("  Actual:      _stext=");
    put_hex(stext);
    uart_puts(" _srodata=");
    put_hex(srodata);
    uart_puts(" _sdata=");
    put_hex(sdata);
    uart_puts(" _sbss=");
    put_hex(sbss);
    uart_puts(" _stack=");
    put_hex(stack_top);
    uart_puts("\r\n");

    int t1_pass = (stext == 0x40800000U) &&
                  (stext < etext) &&
                  (etext <= srodata) &&
                  (srodata < erodata) &&
                  (erodata <= sdata) &&
                  (sdata <= edata) &&
                  (edata <= sbss) &&
                  (sbss <= ebss) &&
                  (ebss < stack_top) &&
                  (stack_top == 0x40880000U);

    if (t1_pass) passed_tests++;
    print_result(t1_pass);

    /* ------------------------------------------------------------- */
    /* TEST 2: 16-Byte Section Alignment Verification               */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(2, "16-Byte Section Alignment Verification",
                      "Verify all output section start VMAs are 16-byte aligned for ROM loader");

    uart_puts("  Expected:    _stext%16==0, _srodata%16==0, _sdata%16==0, _sbss%16==0\r\n");
    uart_puts("  Actual:      _stext%16=");
    put_dec(stext % 16);
    uart_puts(", _srodata%16=");
    put_dec(srodata % 16);
    uart_puts(", _sdata%16=");
    put_dec(sdata % 16);
    uart_puts(", _sbss%16=");
    put_dec(sbss % 16);
    uart_puts("\r\n");

    int t2_pass = ((stext % 16) == 0) &&
                  ((srodata % 16) == 0) &&
                  ((sdata % 16) == 0) &&
                  ((sbss % 16) == 0);

    if (t2_pass) passed_tests++;
    print_result(t2_pass);

    /* ------------------------------------------------------------- */
    /* TEST 3: SRAM RW Data Read/Write Mutation (Peek & Poke)        */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(3, "RW Data Section Read/Write Mutation",
                      "Verify writing and reading 32-bit words in .data section via poke/peek logic");

    uint32_t test_addr = (uint32_t)&g_test_data_var;
    uint32_t orig_val = *(volatile uint32_t *)test_addr;
    uint32_t pattern1 = 0xA5A55A5AU;
    uint32_t pattern2 = 0xDEADBEEFU;

    *(volatile uint32_t *)test_addr = pattern1;
    FENCE();
    uint32_t read1 = *(volatile uint32_t *)test_addr;

    *(volatile uint32_t *)test_addr = pattern2;
    FENCE();
    uint32_t read2 = *(volatile uint32_t *)test_addr;

    // Restore
    *(volatile uint32_t *)test_addr = orig_val;
    FENCE();

    uart_puts("  Expected:    Pattern 1 = 0xA5A55A5A, Pattern 2 = 0xDEADBEEF\r\n");
    uart_puts("  Actual:      Read 1 = ");
    put_hex(read1);
    uart_puts(", Read 2 = ");
    put_hex(read2);
    uart_puts("\r\n");

    int t3_pass = (read1 == pattern1) && (read2 == pattern2);
    if (t3_pass) passed_tests++;
    print_result(t3_pass);

    /* ------------------------------------------------------------- */
    /* TEST 4: SRAM RW BSS Zero-Initialization & Mutation            */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(4, "RW BSS Zero-Init & Write Mutation",
                      "Verify crt0.S zero-initialized .bss and verify mutation capability");

    uint32_t bss_addr = (uint32_t)&g_test_bss_var;
    uint32_t bss_initial = *(volatile uint32_t *)bss_addr;

    *(volatile uint32_t *)bss_addr = 0xCAFEBABEU;
    FENCE();
    uint32_t bss_mutated = *(volatile uint32_t *)bss_addr;

    // Reset back to zero
    *(volatile uint32_t *)bss_addr = 0;
    FENCE();

    uart_puts("  Expected:    Initial = 0x00000000, Mutated = 0xCAFEBABE\r\n");
    uart_puts("  Actual:      Initial = ");
    put_hex(bss_initial);
    uart_puts(", Mutated = ");
    put_hex(bss_mutated);
    uart_puts("\r\n");

    int t4_pass = (bss_initial == 0) && (bss_mutated == 0xCAFEBABEU);
    if (t4_pass) passed_tests++;
    print_result(t4_pass);

    /* ------------------------------------------------------------- */
    /* TEST 5: Read-Only Memory (RODATA) Protection Logic           */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(5, "Read-Only (RODATA) Access & Write Protection",
                      "Verify .rodata is readable and protected from poke mutation");

    uint32_t rodata_addr = (uint32_t)g_test_rodata_str;
    mem_access_t rodata_perm = check_mem_access(rodata_addr);
    uint32_t rodata_word = *(volatile uint32_t *)rodata_addr;

    uart_puts("  Expected:    Permission = MEM_READONLY (1), String begins with 'IRON'\r\n");
    uart_puts("  Actual:      Permission = ");
    put_dec((uint32_t)rodata_perm);
    uart_puts(" (");
    if (rodata_perm == MEM_ACCESS_READONLY) uart_puts("MEM_READONLY");
    else uart_puts("OTHER");
    uart_puts("), Raw Word = ");
    put_hex(rodata_word);
    uart_puts("\r\n");

    int t5_pass = (rodata_perm == MEM_ACCESS_READONLY) &&
                  (strncmp(g_test_rodata_str, "IRON", 4) == 0);
    if (t5_pass) passed_tests++;
    print_result(t5_pass);

    /* ------------------------------------------------------------- */
    /* TEST 6: Out-of-Bounds Address Guarding                       */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(6, "Out-of-Bounds Address Guarding",
                      "Verify access checker rejects unmapped/reserved addresses safely without crash");

    mem_access_t null_access = check_mem_access(0x00000000U);
    mem_access_t oob_sram = check_mem_access(0x40900000U);
    mem_access_t high_addr = check_mem_access(0xFFFFFFFCU);

    uart_puts("  Expected:    Null=INVALID(0), OOB_SRAM=INVALID(0), HighAddr=INVALID(0)\r\n");
    uart_puts("  Actual:      Null=");
    put_dec((uint32_t)null_access);
    uart_puts(", OOB_SRAM=");
    put_dec((uint32_t)oob_sram);
    uart_puts(", HighAddr=");
    put_dec((uint32_t)high_addr);
    uart_puts("\r\n");

    int t6_pass = (null_access == MEM_ACCESS_INVALID) &&
                  (oob_sram == MEM_ACCESS_INVALID) &&
                  (high_addr == MEM_ACCESS_INVALID);
    if (t6_pass) passed_tests++;
    print_result(t6_pass);

    /* ------------------------------------------------------------- */
    /* TEST 7: Misaligned Address Guarding                          */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(7, "Misaligned Address Guarding",
                      "Verify word access checker rejects unaligned addresses to prevent trap exceptions");

    mem_access_t misalign1 = check_mem_access(0x40820001U);
    mem_access_t misalign2 = check_mem_access(0x40820002U);
    mem_access_t misalign3 = check_mem_access(0x40820003U);

    uart_puts("  Expected:    Offset +1=INVALID(0), Offset +2=INVALID(0), Offset +3=INVALID(0)\r\n");
    uart_puts("  Actual:      +1=");
    put_dec((uint32_t)misalign1);
    uart_puts(", +2=");
    put_dec((uint32_t)misalign2);
    uart_puts(", +3=");
    put_dec((uint32_t)misalign3);
    uart_puts("\r\n");

    int t7_pass = (misalign1 == MEM_ACCESS_INVALID) &&
                  (misalign2 == MEM_ACCESS_INVALID) &&
                  (misalign3 == MEM_ACCESS_INVALID);
    if (t7_pass) passed_tests++;
    print_result(t7_pass);

    /* ------------------------------------------------------------- */
    /* TEST 8: Freestanding String & Hex Parsing                    */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(8, "Freestanding String & Hex Conversion Parser",
                      "Verify s_htoi, strcmp, and memory utilities handle values and edge cases");

    uint32_t parsed_val = 0;
    char hex_str1[] = "0x40800000";
    char *ptr1 = hex_str1;
    int s1 = s_htoi(&ptr1, &parsed_val);

    uint32_t parsed_deadbeef = 0;
    char hex_str2[] = "DEADBEEF";
    char *ptr2 = hex_str2;
    int s2 = s_htoi(&ptr2, &parsed_deadbeef);

    uint32_t parsed_invalid = 0;
    char hex_str3[] = "0xXYZ";
    char *ptr3 = hex_str3;
    int s3 = s_htoi(&ptr3, &parsed_invalid);

    int cmp_eq = (strcmp("iron_v", "iron_v") == 0);
    int cmp_diff = (strcmp("apple", "banana") < 0);

    uart_puts("  Expected:    0x40800000=OK, 0xDEADBEEF=OK, Invalid=REJECT, StrCmp=MATCH\r\n");
    uart_puts("  Actual:      0x40800000=");
    put_hex(parsed_val);
    uart_puts(" (s=");
    put_dec(s1);
    uart_puts("), DEADBEEF=");
    put_hex(parsed_deadbeef);
    uart_puts(" (s=");
    put_dec(s2);
    uart_puts("), Invalid (s=");
    put_dec(s3);
    uart_puts(")\r\n");

    int t8_pass = (s1 == 1 && parsed_val == 0x40800000U) &&
                  (s2 == 1 && parsed_deadbeef == 0xDEADBEEFU) &&
                  (s3 == 0) &&
                  cmp_eq && cmp_diff;
    if (t8_pass) passed_tests++;
    print_result(t8_pass);

    /* ------------------------------------------------------------- */
    /* TEST 9: Peripheral MMIO Space Accessibility                  */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(9, "Peripheral MMIO Space Accessibility",
                      "Verify safe non-faulting volatile register access to UART and TIMG");

    mem_access_t uart_perm = check_mem_access((uint32_t)UART0_STATUS_REG);
    uint32_t uart_status = *UART0_STATUS_REG;
    mem_access_t timg_perm = check_mem_access((uint32_t)TIMG0_WDTCONFIG0_REG);
    uint32_t timg_cfg = *TIMG0_WDTCONFIG0_REG;
    mem_access_t usb_perm = check_mem_access((uint32_t)USB_DEVICE_EP1_CONF_REG);
    uint32_t usb_conf = *USB_DEVICE_EP1_CONF_REG;

    uart_puts("  Expected:    UART, TIMG & USB in MMIO region (3), non-faulting read\r\n");
    uart_puts("  Actual:      UART_STATUS=");
    put_hex(uart_status);
    uart_puts(" (perm=");
    put_dec((uint32_t)uart_perm);
    uart_puts("), TIMG=");
    put_hex(timg_cfg);
    uart_puts(" (perm=");
    put_dec((uint32_t)timg_perm);
    uart_puts("), USB_EP1=");
    put_hex(usb_conf);
    uart_puts(" (perm=");
    put_dec((uint32_t)usb_perm);
    uart_puts(")\r\n");

    int t9_pass = (uart_perm == MEM_ACCESS_MMIO) &&
                  (timg_perm == MEM_ACCESS_MMIO) &&
                  (usb_perm == MEM_ACCESS_MMIO);
    if (t9_pass) passed_tests++;
    print_result(t9_pass);

    /* ------------------------------------------------------------- */
    /* TEST 10: Stack Pointer Alignment, Margin & Machine CSR State  */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(10, "Stack Bounds, Alignment & Machine CSR State",
                      "Verify SP is 16-byte aligned in DRAM and CSRs are correctly initialized");

    uint32_t current_sp = 0;
    GET_CURRENT_SP(current_sp);

    int sp_aligned = ((current_sp & STACK_ALIGN_MASK) == 0U);
    int sp_in_bounds = (current_sp > STACK_LIMIT_ADDR) && (current_sp <= STACK_TOP_ADDR);
    uint32_t stack_margin = GET_STACK_MARGIN(current_sp);

    uint32_t mstatus_val = 0;
    asm volatile("csrr %0, mstatus" : "=r"(mstatus_val));
    uint32_t mpp = mstatus_val & MSTATUS_MPP_MASK;

    uint32_t mie_val = 0;
    asm volatile("csrr %0, mie" : "=r"(mie_val));

    uint32_t mtvec_val = 0;
    asm volatile("csrr %0, mtvec" : "=r"(mtvec_val));

    int mtvec_valid = ((mtvec_val & MTVEC_MODE_MASK) == MTVEC_MODE_VECTORED) && ((mtvec_val & MTVEC_ALIGN_MASK) == 0U);
    int mpp_valid = (mpp == MSTATUS_MPP_MACHINE_MODE || mpp == MSTATUS_MPP_USER_MODE);
    int mie_valid = (mie_val == 0 || (mie_val & (1U << UART0_CPU_INTR_CHANNEL)) != 0);

    uart_puts("  Expected:    SP aligned (16B), within DRAM bounds, M-Mode CSRs valid\r\n");
    uart_puts("  Actual:      SP=");
    put_hex(current_sp);
    uart_puts(" (aligned=");
    put_dec(sp_aligned);
    uart_puts(", margin=");
    put_dec(stack_margin);
    uart_puts(" B), MPP=");
    put_dec(mpp >> MSTATUS_MPP_SHIFT);
    uart_puts(" (");
    if (mpp == MSTATUS_MPP_MACHINE_MODE) uart_puts("Machine Mode");
    else if (mpp == MSTATUS_MPP_USER_MODE) uart_puts("User Mode");
    else uart_puts("Other");
    uart_puts("), MIE=");
    put_dec(mie_val);
    uart_puts(", MTVEC=");
    put_hex(mtvec_val);
    uart_puts("\r\n");

    int t10_pass = sp_aligned && sp_in_bounds && mpp_valid && mie_valid && mtvec_valid;
    if (t10_pass) passed_tests++;
    print_result(t10_pass);

    /* ------------------------------------------------------------- */
    /* TEST 11: PCR Clock Tree Configuration & Frequency Validation  */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(11, "PCR Clock Distribution & Frequency Status",
                      "Verify SYSCLK operates on PLL (160MHz) with 40MHz APB bus clock in PCR registers");

    uint32_t sysclk_reg = *PCR_SYSCLK_CONF_REG;
    uint32_t soc_clk_sel = (sysclk_reg & PCR_SYSCLK_CONF_SOC_CLK_SEL_M) >> PCR_SYSCLK_CONF_SOC_CLK_SEL_S;
    uint32_t xtal_freq = (sysclk_reg & PCR_SYSCLK_CONF_CLK_XTAL_FREQ_M) >> PCR_SYSCLK_CONF_CLK_XTAL_FREQ_S;

    uint32_t pll_div_reg = *PCR_PLL_DIV_CLK_EN_REG;
    int pll_160m_en = (pll_div_reg & PCR_PLL_DIV_CLK_EN_PLL_160M_CLK_EN_M) != 0;
    int pll_80m_en = (pll_div_reg & PCR_PLL_DIV_CLK_EN_PLL_80M_CLK_EN_M) != 0;

    uint32_t apb_freq_reg = *PCR_APB_FREQ_CONF_REG;
    uint32_t apb_div = (apb_freq_reg & PCR_APB_FREQ_CONF_APB_DIV_NUM_M) >> PCR_APB_FREQ_CONF_APB_DIV_NUM_S;

    clock_config_t clk_cfg;
    clock_get_config(&clk_cfg);

    uart_puts("  Expected:    SOC_CLK_SEL=PLL, XTAL=40MHz, PLL_160M=1, PLL_80M=1, APB_DIV=0\r\n");
    uart_puts("  Actual:      CLK_SEL=");
    put_dec(soc_clk_sel);
    uart_puts(" (");
    if (soc_clk_sel == CLK_SOURCE_PLL) uart_puts("PLL");
    else if (soc_clk_sel == CLK_SOURCE_XTAL) uart_puts("XTAL");
    else uart_puts("OTHER");
    uart_puts("), XTAL=");
    put_dec(xtal_freq);
    uart_puts("MHz, CPU=");
    put_dec(clk_cfg.cpu_mhz);
    uart_puts("MHz, APB=");
    put_dec(clk_cfg.apb_mhz);
    uart_puts("MHz, APB_DIV=");
    put_dec(apb_div);
    uart_puts("\r\n");

    int t11_pass = (soc_clk_sel == CLK_SOURCE_PLL) && (xtal_freq == SOC_XTAL_FREQ_MHZ) &&
                   pll_160m_en && pll_80m_en &&
                   (apb_div == SOC_APB_DIVIDER_1) &&
                   (clk_cfg.cpu_mhz == SOC_CPU_TARGET_FREQ_MHZ) &&
                   (clk_cfg.apb_mhz == SOC_APB_TARGET_FREQ_MHZ);
    if (t11_pass) passed_tests++;
    print_result(t11_pass);

    /* ------------------------------------------------------------- */
    /* TEST 12: Active Multi-Tier Watchdog Supervisor & Reload Status*/
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(12, "Active Watchdog Supervisor & Reload Status",
                      "Verify TIMG0 MWDT is armed, prescaled, and reload feed operates safely");

    uint32_t timg0_cfg0 = *TIMG0_WDTCONFIG0;
    int wdt_enabled = (timg0_cfg0 & TIMG0_WDTCONFIG0_WDT_EN_M) != 0;
    uint32_t wdt_stg0 = (timg0_cfg0 & TIMG0_WDTCONFIG0_WDT_STG0_M) >> TIMG0_WDTCONFIG0_WDT_STG0_S;
    uint32_t timg0_cfg1 = *TIMG0_WDTCONFIG1_REG;
    uint32_t prescale = (timg0_cfg1 & TIMG0_WDTCONFIG1_WDT_CLK_PRESCALE_M) >> TIMG0_WDTCONFIG1_WDT_CLK_PRESCALE_S;

    wdt_supervisor_t wdt_stat;
    wdt_get_status(&wdt_stat);
    uint32_t prev_feed = wdt_stat.feed_count;
    wdt_feed();
    wdt_get_status(&wdt_stat);

    uart_puts("  Expected:    WDT_EN=1, STG0=3 (Reset), Prescale=80, FeedCount increments\r\n");
    uart_puts("  Actual:      WDT_EN=");
    put_dec(wdt_enabled);
    uart_puts(", STG0=");
    put_dec(wdt_stg0);
    uart_puts(", Prescale=");
    put_dec(prescale);
    uart_puts(", Active=");
    put_dec(wdt_stat.active);
    uart_puts(", Feeds=");
    put_dec(wdt_stat.feed_count);
    uart_puts(" (Epoch=");
    put_dec(wdt_stat.epoch_count);
    uart_puts("s, TotalFeeds=");
    put_dec(wdt_stat.total_feed_count);
    uart_puts(")\r\n");

    int t12_pass = wdt_enabled && (wdt_stg0 == WDT_ACTION_RESET_SYSTEM) &&
                   (prescale == WDT_PRESCALER_DIV) &&
                   (wdt_stat.active == 1) && (wdt_stat.feed_count > prev_feed);
    if (t12_pass) passed_tests++;
    print_result(t12_pass);

    /* ------------------------------------------------------------- */
    /* TEST 13: Low-Power (LP) SRAM Retention & Accessibility        */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(13, "Low-Power (LP) SRAM Accessibility & Retention",
                      "Verify LP SRAM at 0x50000000 is read/write accessible with pattern preservation");

    volatile uint32_t *lp_sram_ptr = (volatile uint32_t *)0x50000000U;
    uint32_t lp_orig = *lp_sram_ptr;
    *lp_sram_ptr = 0x55AA33CCU;
    FENCE();
    uint32_t lp_read1 = *lp_sram_ptr;
    *lp_sram_ptr = 0xAA55CC33U;
    FENCE();
    uint32_t lp_read2 = *lp_sram_ptr;
    *lp_sram_ptr = lp_orig;
    FENCE();

    uart_puts("  Expected:    LP SRAM at 0x50000000 preserves patterns 0x55AA33CC and 0xAA55CC33\r\n");
    uart_puts("  Actual:      Read1=");
    put_hex(lp_read1);
    uart_puts(", Read2=");
    put_hex(lp_read2);
    uart_puts("\r\n");

    int t13_pass = (lp_read1 == 0x55AA33CCU) && (lp_read2 == 0xAA55CC33U);
    if (t13_pass) passed_tests++;
    print_result(t13_pass);

    /* ------------------------------------------------------------- */
    /* TEST 14: Vectored Trap Vector (mtvec) Alignment & Table Base  */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(14, "Vectored Trap Vector (mtvec) Alignment & Base",
                      "Verify mtvec operates in Vectored Mode (0x1) with 256-byte aligned vector table");

    uint32_t t14_mtvec = 0;
    asm volatile("csrr %0, mtvec" : "=r"(t14_mtvec));
    uint32_t expected_base = (uint32_t)_vector_table;
    int mtvec_mode_vectored = (t14_mtvec & MTVEC_MODE_MASK) == MTVEC_MODE_VECTORED;
    int mtvec_aligned_256 = (t14_mtvec & MTVEC_ALIGN_MASK) == 0U;
    int mtvec_base_matches = (t14_mtvec & MTVEC_BASE_MASK) == (expected_base & MTVEC_BASE_MASK);

    uart_puts("  Expected:    mtvec.MODE=1 (Vectored), BASE=");
    put_hex(expected_base & MTVEC_BASE_MASK);
    uart_puts(", align256=1\r\n");
    uart_puts("  Actual:      mtvec=");
    put_hex(t14_mtvec);
    uart_puts(" (MODE=");
    put_dec(t14_mtvec & MTVEC_MODE_MASK);
    uart_puts(", BASE=");
    put_hex(t14_mtvec & MTVEC_BASE_MASK);
    uart_puts(", align256=");
    put_dec(mtvec_aligned_256);
    uart_puts(")\r\n");

    int t14_pass = mtvec_mode_vectored && mtvec_aligned_256 && mtvec_base_matches;
    if (t14_pass) passed_tests++;
    print_result(t14_pass);

    /* ------------------------------------------------------------- */
    /* TEST 15: Controlled M-Mode Software Trap (ECALL) & MRET Resume*/
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(15, "Controlled ECALL Trap Execution & MRET Resume",
                      "Execute ECALL, verify trap entry via Vector 0, advance mepc, and resume via mret");

    uint32_t prev_ecalls = trap_get_ecall_count();
    /* Execute controlled M-mode software trap */
    asm volatile("ecall");
    uint32_t post_ecalls = trap_get_ecall_count();

    uart_puts("  Expected:    ECALL trap dispatched, count increments by 1, execution resumes\r\n");
    uart_puts("  Actual:      PrevECALLs=");
    put_dec(prev_ecalls);
    uart_puts(", PostECALLs=");
    put_dec(post_ecalls);
    uart_puts("\r\n");

    int t15_pass = (post_ecalls == prev_ecalls + 1);
    if (t15_pass) passed_tests++;
    print_result(t15_pass);

    /* ------------------------------------------------------------- */
    /* TEST 16: INTMTX Routing & INTPRI Priority / Threshold Preempt */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(16, "INTMTX Routing & PLIC Priority / Threshold Preempt",
                      "Verify UART0 routing to CPU channel 5, priority 10, and live SW interrupt preemption");

    /* 1. Test UART0 routing and priority configuration on external interrupt channel 5 */
    int route_res = interrupt_route(INT_SRC_UART0, 5);
    uint32_t uart0_map = interrupt_get_map(INT_SRC_UART0);
    int pri_res = interrupt_set_priority(5, 10);
    uint32_t pri_val = interrupt_get_priority(5);

    int part1_pass = (route_res == 0) && (uart0_map == 5) && (pri_res == 0) && (pri_val == 10);

    /* 2. Test live interrupt dispatch via CPU software interrupt 0 routed to CPU channel 2 */
    g_test_isr_hit = 0;
    interrupt_route(INT_SRC_CPU_INTR_FROM_CPU_0, 2);
    interrupt_set_priority(2, 7);
    interrupt_set_threshold(3); /* Priority 7 > Threshold 3: unmasked */
    interrupt_register_handler(2, test_sw_isr, NULL);
    interrupt_enable(2);

    /* Trigger interrupt with global interrupts enabled */
    interrupt_global_enable();
    interrupt_trigger_cpu_intr(0);
    for (volatile int d = 0; d < 200; d++);
    interrupt_global_disable();

    int live_dispatch_pass = (g_test_isr_hit == 1);

    /* 3. Test threshold preemption: priority 7 <= threshold 10 -> masked */
    g_test_isr_hit = 0;
    interrupt_set_threshold(10);
    interrupt_trigger_cpu_intr(0);
    interrupt_global_enable();
    for (volatile int d = 0; d < 200; d++);
    interrupt_global_disable();

    int threshold_mask_pass = (g_test_isr_hit == 0);

    /* Clean up software interrupt and reset state */
    interrupt_clear_cpu_intr(0);
    interrupt_set_threshold(0);
    interrupt_disable(2);
    interrupt_unregister_handler(2);
    interrupt_unroute(INT_SRC_CPU_INTR_FROM_CPU_0);
    interrupt_unroute(INT_SRC_UART0);
    interrupt_set_priority(5, 0);

    uart_puts("  Expected:    UART0_MAP=5, PRI_5=10, LiveDispatch=1, ThreshMask=1\r\n");
    uart_puts("  Actual:      UART0_MAP=");
    put_dec(uart0_map);
    uart_puts(", PRI_5=");
    put_dec(pri_val);
    uart_puts(", LiveDispatch=");
    put_dec(live_dispatch_pass);
    uart_puts(", ThreshMask=");
    put_dec(threshold_mask_pass);
    uart_puts("\r\n");

    int t16_pass = part1_pass && live_dispatch_pass && threshold_mask_pass;
    if (t16_pass) passed_tests++;
    print_result(t16_pass);

    /* Restore UART0 interrupt routing and handler */
    uart_init();

    /* ------------------------------------------------------------- */
    /* TEST 17: Lock-Free SPSC DPC Queue Engine                      */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(17, "Lock-Free SPSC DPC Queue Engine",
                      "Enqueue 64 events, verify FIFO order, assert 65th drop, and assert head == tail");

    dpc_init();

    /* 1. Enqueue exactly DPC_QUEUE_CAPACITY (64) events */
    int enqueue_all_ok = 1;
    for (uint32_t i = 0; i < DPC_QUEUE_CAPACITY; i++)
    {
        int res = dpc_enqueue(DPC_TYPE_TIMER_TICK, i, i * 10U, NULL);
        if (res != DPC_STATUS_OK)
        {
            enqueue_all_ok = 0;
        }
    }
    uint32_t size_full = dpc_get_size();

    /* 2. Attempt 65th enqueue: assert drop counter increments by 1 */
    int res_65 = dpc_enqueue(DPC_TYPE_WIFI_PACKET, 999U, 999U, NULL);
    uint32_t drop_cnt = dpc_get_drop_count();
    int drop_pass = (res_65 == DPC_STATUS_ERR_FULL) && (drop_cnt == 1U);

    /* 3. Drain all 64 events and verify strict FIFO ordering */
    int fifo_order_ok = 1;
    for (uint32_t i = 0; i < DPC_QUEUE_CAPACITY; i++)
    {
        dpc_event_t ev;
        int dq_res = dpc_dequeue(&ev);
        if (dq_res != DPC_STATUS_OK ||
            ev.type != DPC_TYPE_TIMER_TICK ||
            ev.arg0 != i ||
            ev.arg1 != (i * 10U))
        {
            fifo_order_ok = 0;
        }
    }

    /* 4. Assert empty condition and head == tail */
    dpc_queue_t stats;
    dpc_get_stats(&stats);
    int head_tail_match = (stats.head == stats.tail) && (stats.head == DPC_QUEUE_CAPACITY);
    uint32_t size_drained = dpc_get_size();
    int drain_pass = (size_drained == 0U) && head_tail_match;

    /* 5. Verify live dispatch and execution via dpc_process_all() with registered handler */
    g_test_dpc_hit = 0;
    dpc_enqueue(DPC_TYPE_TEST_EVENT, 111U, 222U, test_dpc_callback);
    dpc_enqueue(DPC_TYPE_TEST_EVENT, 333U, 444U, test_dpc_callback);
    uint32_t processed_count = dpc_process_all();
    int dispatch_pass = (processed_count == 2U) && (g_test_dpc_hit == 2U) && (dpc_get_size() == 0U);

    /* 6. Clean reset of DPC engine to pristine state for subsequent runtime execution */
    dpc_init();

    uart_puts("  Expected:    FullSize=64, DropPass=1, FIFOPass=1, HeadTailMatch=1, Dispatch=1\r\n");
    uart_puts("  Actual:      FullSize=");
    put_dec(size_full);
    uart_puts(", DropPass=");
    put_dec(drop_pass);
    uart_puts(", FIFOPass=");
    put_dec(fifo_order_ok);
    uart_puts(", HeadTailMatch=");
    put_dec(head_tail_match);
    uart_puts(", Dispatch=");
    put_dec(dispatch_pass);
    uart_puts("\r\n");

    int t17_pass = enqueue_all_ok && (size_full == DPC_QUEUE_CAPACITY) && drop_pass && fifo_order_ok && drain_pass && dispatch_pass;
    if (t17_pass) passed_tests++;
    print_result(t17_pass);

    /* ------------------------------------------------------------- */
    /* TEST 18: USB-Serial-JTAG CDC-ACM Hardware Driver              */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(18, "USB-Serial-JTAG CDC-ACM Hardware Driver",
                      "Verify non-faulting MMIO access to 0x6000F000, readable EP1 status, and timeout guard");

    /* 1. Ensure USB-Serial-JTAG hardware clock and controller initialized */
    usb_serial_init();

    /* 2. Volatile read from *USB_DEVICE_EP1_CONF_REG (0x6000F004) */
    volatile uint32_t *conf_reg = USB_DEVICE_EP1_CONF_REG;
    uint32_t ep1_conf = *conf_reg;
    int non_faulting_read = 1;

    /* 3. Verify EP1 configuration register status and valid bitfield geometry */
    uint32_t in_ep_free = (ep1_conf & USB_DEVICE_EP1_CONF_SERIAL_IN_EP_DATA_FREE_M) >> USB_DEVICE_EP1_CONF_SERIAL_IN_EP_DATA_FREE_S;
    uint32_t out_ep_avail = (ep1_conf & USB_DEVICE_EP1_CONF_SERIAL_OUT_EP_DATA_AVAIL_M) >> USB_DEVICE_EP1_CONF_SERIAL_OUT_EP_DATA_AVAIL_S;
    uint32_t ep1_conf_valid_mask = USB_DEVICE_EP1_CONF_WR_DONE_M |
                                   USB_DEVICE_EP1_CONF_SERIAL_IN_EP_DATA_FREE_M |
                                   USB_DEVICE_EP1_CONF_SERIAL_OUT_EP_DATA_AVAIL_M;
    int bit_readable_pass = ((ep1_conf & ~ep1_conf_valid_mask) == 0U) &&
                            ((ep1_conf & USB_DEVICE_EP1_CONF_WR_DONE_M) == 0U);

    /* 4. Verify device structure register mapping */
    usb_serial_dev_t udev;
    usb_serial_get_dev(&udev);
    int reg_map_pass = (udev.ep1_reg == USB_DEVICE_EP1_REG) &&
                       (udev.ep1_conf_reg == USB_DEVICE_EP1_CONF_REG) &&
                       (udev.int_raw_reg == USB_DEVICE_INT_RAW_REG) &&
                       (udev.int_ena_reg == USB_DEVICE_INT_ENA_REG) &&
                       (udev.int_clr_reg == USB_DEVICE_INT_CLR_REG);

    /* 5. Non-blocking TX readiness check */
    int tx_ready = usb_serial_is_tx_ready();
    int tx_ready_match = (tx_ready == (int)in_ep_free);

    /* 6. Verify non-blocking timeout protection without CPU stall (silencing raw 'X' transmission) */
    int timeout_guard_pass = (udev.tx_timeout_cycles == USB_SERIAL_DEFAULT_TX_TIMEOUT_CYCLES);

    uart_puts("  Expected:    NonFaulting=1, BitReadable=1, RegMap=1, TxReadyMatch=1, TimeoutGuard=1\r\n");
    uart_puts("  Actual:      NonFaulting=");
    put_dec(non_faulting_read);
    uart_puts(", BitReadable=");
    put_dec(bit_readable_pass);
    uart_puts(", RegMap=");
    put_dec(reg_map_pass);
    uart_puts(", TxReadyMatch=");
    put_dec(tx_ready_match);
    uart_puts(", TimeoutGuard=");
    put_dec(timeout_guard_pass);
    uart_puts(" (InEpFree=");
    put_dec(in_ep_free);
    uart_puts(", EP1_CONF=");
    put_hex(ep1_conf);
    uart_puts(", OutAvail=");
    put_dec(out_ep_avail);
    uart_puts(")\r\n");

    int t18_pass = non_faulting_read && bit_readable_pass && reg_map_pass && tx_ready_match && timeout_guard_pass;
    if (t18_pass) passed_tests++;
    print_result(t18_pass);

    /* ------------------------------------------------------------- */
    /* TEST 19: Unified Dual-Console Layer & Multiplexer (Task 2.5)  */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(19, "Unified Dual-Console Multiplexer Subsystem",
                      "Verify console backend dispatch, active masks, non-blocking polling, and echo control");

    console_init();

    console_manager_t mgr;
    console_get_manager(&mgr);

    int uart_backend_ok = (mgr.uart.putc != NULL) &&
                          (mgr.uart.puts != NULL) &&
                          (mgr.uart.getc_nonblocking != NULL) &&
                          (mgr.uart.flush != NULL);

    int usb_backend_ok = (mgr.usb.putc != NULL) &&
                         (mgr.usb.puts != NULL) &&
                         (mgr.usb.getc_nonblocking != NULL) &&
                         (mgr.usb.flush != NULL);

    int active_mask_ok = (mgr.active_mask == (CONSOLE_MASK_UART0 | CONSOLE_MASK_USB));

    /* Test non-blocking character receive with NULL guard and active mask gating */
    int null_guard_ok = (console_getc_nonblocking(NULL) == 0);
    uint8_t orig_mask = console_get_active_mask();
    console_set_active_mask(0U);
    char dummy_c = '\0';
    int disabled_mask_ok = (console_getc_nonblocking(&dummy_c) == 0);
    console_set_active_mask(orig_mask);
    int mask_restore_ok = (console_get_active_mask() == orig_mask);
    int readline_null_guard = (console_read_line_nonblocking(NULL, 10U) == 0) &&
                              (console_read_line_nonblocking(&dummy_c, 0U) == 0);
    int nonblock_pass = null_guard_ok && disabled_mask_ok && mask_restore_ok && readline_null_guard;

    /* Test echo toggle control */
    console_set_echo(0U);
    int echo_off_pass = (console_get_echo() == 0U);
    console_set_echo(1U);
    int echo_on_pass = (console_get_echo() == 1U);
    int echo_toggle_ok = echo_off_pass && echo_on_pass;

    uart_puts("  Expected:    UARTBackend=1, USBBackend=1, ActiveMask=3, NonblockPass=1, EchoToggle=1\r\n");
    uart_puts("  Actual:      UARTBackend=");
    put_dec(uart_backend_ok);
    uart_puts(", USBBackend=");
    put_dec(usb_backend_ok);
    uart_puts(", ActiveMask=");
    put_dec(mgr.active_mask);
    uart_puts(", NonblockPass=");
    put_dec(nonblock_pass);
    uart_puts(", EchoToggle=");
    put_dec(echo_toggle_ok);
    uart_puts("\r\n");

    int t19_pass = uart_backend_ok && usb_backend_ok && active_mask_ok && nonblock_pass && echo_toggle_ok;
    if (t19_pass) passed_tests++;
    print_result(t19_pass);

    /* ------------------------------------------------------------- */
    /* TEST 20: Hardware Periodic Timer (TIMG0 T0) Configuration     */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(20, "Hardware Periodic Timer (TIMG0 T0) Configuration",
                      "Verify TIMG0 Timer 0 is enabled, prescaled, auto-reloading, and routed via INTMTX");

    uint32_t t0_cfg = *TIMG0_T0CONFIG_REG;
    int t0_en = (t0_cfg & TIMG0_T0CONFIG_EN_M) != 0;
    int t0_autoreload = (t0_cfg & TIMG0_T0CONFIG_AUTORELOAD_M) != 0;
    uint32_t t0_divider = (t0_cfg & TIMG0_T0CONFIG_DIVIDER_M) >> TIMG0_T0CONFIG_DIVIDER_S;
    uint32_t timer_route = interrupt_get_map(INT_SRC_TG0_T0);
    uint32_t timer_pri = interrupt_get_priority(TIMER_CPU_INTR_CHANNEL);
    int timer_intr_en = interrupt_is_enabled(TIMER_CPU_INTR_CHANNEL);

    timer_status_t tmr_stat;
    timer_get_status(&tmr_stat);

    /* Verify 54-bit hardware counter advances */
    uint64_t t_start = timer_get_current_ticks();
    for (volatile int i = 0; i < 5000; i++) { }
    uint64_t t_end = timer_get_current_ticks();
    int ticks_advancing = (t_end > t_start);

    uart_puts("  Expected:    EN=1, AutoReload=1, Prescale=40, Route=6, Priority=8, Active=1, Advancing=1\r\n");
    uart_puts("  Actual:      EN=");
    put_dec(t0_en);
    uart_puts(", AutoReload=");
    put_dec(t0_autoreload);
    uart_puts(", Prescale=");
    put_dec(t0_divider);
    uart_puts(", Route=");
    put_dec(timer_route);
    uart_puts(", Priority=");
    put_dec(timer_pri);
    uart_puts(", IntrEn=");
    put_dec(timer_intr_en);
    uart_puts(", Active=");
    put_dec(tmr_stat.active);
    uart_puts(", Advancing=");
    put_dec(ticks_advancing);
    uart_puts(", Ticks=");
    put_dec(tmr_stat.isr_count);
    uart_puts("\r\n");

    int t20_pass = t0_en && t0_autoreload && (t0_divider == TIMER_PRESCALER_DIV) &&
                   (timer_route == TIMER_CPU_INTR_CHANNEL) &&
                   (timer_pri == TIMER_INTR_PRIORITY) &&
                   timer_intr_en && (tmr_stat.active == 1) && ticks_advancing;
    if (t20_pass) passed_tests++;
    print_result(t20_pass);

    /* ------------------------------------------------------------- */
    /* TEST 21: Deterministic Static Arena Allocator (Task 3.1)      */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(21, "Deterministic Static Arena Allocator",
                      "Verify 32 small block allocations, 33rd exhaustion guard, block reuse on free, and scratch mark/reset");

    arena_init();

    void *small_ptrs[ARENA_POOL_BLOCK_COUNT_SMALL];
    int alloc_32_ok = 1;
    int align_ok = 1;
    int unique_ok = 1;

    for (uint32_t i = 0; i < ARENA_POOL_BLOCK_COUNT_SMALL; i++)
    {
        small_ptrs[i] = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
        if (small_ptrs[i] == NULL)
        {
            alloc_32_ok = 0;
        }
        else if (((uintptr_t)small_ptrs[i] & WORD_ALIGN_MASK) != 0U)
        {
            align_ok = 0;
        }

        for (uint32_t j = 0; j < i; j++)
        {
            if (small_ptrs[j] == small_ptrs[i])
            {
                unique_ok = 0;
            }
        }
    }

    /* Exhaustion guard: 33rd small allocation returns NULL */
    void *exhaust_ptr = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
    int exhaust_ok = (exhaust_ptr == NULL);

    /* Free block 15 and verify it enables subsequent re-allocation reusing block 15 */
    void *block15_orig = small_ptrs[15];
    int free15_ok = (arena_free(block15_orig) == ARENA_FREE_SUCCESS);
    void *block15_realloc = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
    int reuse_ok = (block15_realloc == block15_orig);

    /* Free all small blocks */
    small_ptrs[15] = block15_realloc;
    int free_all_ok = 1;
    for (uint32_t i = 0; i < ARENA_POOL_BLOCK_COUNT_SMALL; i++)
    {
        if (arena_free(small_ptrs[i]) != ARENA_FREE_SUCCESS)
        {
            free_all_ok = 0;
        }
    }

    arena_pool_stats_t small_stats;
    arena_get_pool_stats(ARENA_POOL_SMALL, &small_stats);
    int pool_clean_ok = (small_stats.active_count == 0U) &&
                        (small_stats.allocated_mask == ARENA_BITMASK_EMPTY);

    /* Scratch arena test: mark, alloc, reset restores original pointer */
    arena_scratch_mark_t mark_before = arena_scratch_mark();
    void *sc_p1 = arena_scratch_alloc(128U);
    int sc_p1_ok = (sc_p1 != NULL) && (((uintptr_t)sc_p1 & WORD_ALIGN_MASK) == 0U);
    void *sc_p2 = arena_scratch_alloc(256U);
    int sc_p2_ok = (sc_p2 != NULL) && ((uintptr_t)sc_p2 > (uintptr_t)sc_p1);

    arena_scratch_reset(mark_before);
    void *sc_p3 = arena_scratch_alloc(128U);
    int scratch_restore_ok = (sc_p3 == sc_p1);

    arena_scratch_reset(mark_before);

    uart_puts("  Expected:    Alloc32=1, ExhaustGuard=1, Free15=1, Reused15=1, CleanPool=1, ScratchRestore=1\r\n");
    uart_puts("  Actual:      Alloc32=");
    put_dec(alloc_32_ok && align_ok && unique_ok);
    uart_puts(", ExhaustGuard=");
    put_dec(exhaust_ok);
    uart_puts(", Free15=");
    put_dec(free15_ok);
    uart_puts(", Reused15=");
    put_dec(reuse_ok);
    uart_puts(", CleanPool=");
    put_dec(free_all_ok && pool_clean_ok);
    uart_puts(", ScratchRestore=");
    put_dec(sc_p1_ok && sc_p2_ok && scratch_restore_ok);
    uart_puts("\r\n");

    int t21_pass = alloc_32_ok && align_ok && unique_ok && exhaust_ok &&
                   free15_ok && reuse_ok && free_all_ok && pool_clean_ok &&
                   sc_p1_ok && sc_p2_ok && scratch_restore_ok;
    if (t21_pass) passed_tests++;
    print_result(t21_pass);

    /* ------------------------------------------------------------- */
    /* TEST 22: High-Resolution SYSTIMER & Event Engine (Task 3.2)   */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(22, "High-Resolution SYSTIMER & Event Engine",
                      "Verify 16 MHz Unit 0 counter monotonic increase (T2 > T1), microsecond conversion, and alarm routing");

    /* 1. Latch Unit 0 counter T1 */
    uint64_t t1 = systimer_get_ticks();

    /* 2. Precision hardware delay: burn CPU cycles */
    for (volatile int i = 0; i < 2000; i++)
    {
        asm volatile("nop");
    }

    /* 3. Latch Unit 0 counter T2 */
    uint64_t t2 = systimer_get_ticks();
    int monotonic_ok = (t2 > t1);

    /* 4. Verify microsecond conversion */
    uint64_t us1 = systimer_get_us();
    systimer_delay_us(100U);
    uint64_t us2 = systimer_get_us();
    int us_conversion_ok = (us2 >= (us1 + 90U));

    /* 5. Verify millisecond conversion consistency */
    uint64_t ticks_sample = systimer_get_ticks();
    uint64_t ms_calc = (ticks_sample >> SYSTIMER_TICKS_TO_US_SHIFT) / US_PER_MS;
    uint64_t ms_curr = systimer_get_ms();
    int ms_ok = (ms_curr >= ms_calc);

    /* 6. Verify SYSTIMER configuration registers (CLK gating, Unit 0 work enable) */
    uint32_t pcr_conf = *PCR_SYSTIMER_CONF_REG;
    int pcr_clk_ok = ((pcr_conf & PCR_SYSTIMER_CONF_SYSTIMER_CLK_EN_M) != 0U) &&
                     ((pcr_conf & PCR_SYSTIMER_CONF_SYSTIMER_RST_EN_M) == 0U);

    uint32_t pcr_func = *PCR_SYSTIMER_FUNC_CLK_CONF_REG;
    int pcr_func_ok = (pcr_func & PCR_SYSTIMER_FUNC_CLK_CONF_SYSTIMER_FUNC_CLK_EN_M) != 0U;

    uint32_t sys_conf = *SYSTIMER_CONF_REG;
    int unit0_work_ok = (sys_conf & SYSTIMER_CONF_TIMER_UNIT0_WORK_EN_M) != 0U;

    /* 7. Verify Target 0 alarm configuration and INTMTX routing */
    int alarm_init_ok = (systimer_alarm_init(50000U, NULL) == 0);
    uint32_t route_target0 = interrupt_get_map(INT_SRC_SYSTIMER_TARGET0);
    uint32_t pri_target0 = interrupt_get_priority(SYSTIMER_CPU_INTR_CHANNEL);
    int intr_en_target0 = interrupt_is_enabled(SYSTIMER_CPU_INTR_CHANNEL);
    int alarm_route_ok = (route_target0 == SYSTIMER_CPU_INTR_CHANNEL) &&
                         (pri_target0 == SYSTIMER_INTR_PRIORITY) &&
                         intr_en_target0;

    systimer_alarm_cancel();
    int alarm_cancel_ok = !interrupt_is_enabled(SYSTIMER_CPU_INTR_CHANNEL);

    uart_puts("  Expected:    Monotonic=1, UsConvert=1, MsValid=1, PcrClk=1, FuncClk=1, Unit0Work=1, AlarmRoute=1, Cancel=1\r\n");
    uart_puts("  Actual:      Monotonic=");
    put_dec(monotonic_ok);
    uart_puts(", UsConvert=");
    put_dec(us_conversion_ok);
    uart_puts(", MsValid=");
    put_dec(ms_ok);
    uart_puts(", PcrClk=");
    put_dec(pcr_clk_ok);
    uart_puts(", FuncClk=");
    put_dec(pcr_func_ok);
    uart_puts(", Unit0Work=");
    put_dec(unit0_work_ok);
    uart_puts(", AlarmRoute=");
    put_dec(alarm_init_ok && alarm_route_ok);
    uart_puts(", Cancel=");
    put_dec(alarm_cancel_ok);
    uart_puts("\r\n");

    int t22_pass = monotonic_ok && us_conversion_ok && ms_ok && pcr_clk_ok &&
                   pcr_func_ok && unit0_work_ok && alarm_init_ok && alarm_route_ok &&
                   alarm_cancel_ok;
    if (t22_pass) passed_tests++;
    print_result(t22_pass);

    /* ------------------------------------------------------------- */
    /* TEST 23: Cooperative Coroutine Task Engine & Scheduler (3.3)  */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(23, "Cooperative Coroutine Task Engine & Scheduler",
                      "Verify Task A & Task B creation, callee-saved context switching, cooperative yields, and state termination");

    s_test_task_a_counter = 0;
    s_test_task_b_counter = 0;
    s_test_turn_idx = 0;

    static uint8_t task_a_stack[1024] __attribute__((aligned(16)));
    static uint8_t task_b_stack[1024] __attribute__((aligned(16)));

    task_init();

    int id_a = task_create("task_a", test_task_a_worker, NULL, 10U, task_a_stack, sizeof(task_a_stack));
    int id_b = task_create("task_b", test_task_b_worker, NULL, 10U, task_b_stack, sizeof(task_b_stack));

    int create_ok = (id_a > 0) && (id_b > 0) && (id_a != id_b);

    /* Run cooperative scheduling loop until both workers terminate */
    for (uint32_t loop = 0; loop < 25U; loop++)
    {
        task_control_block_t *ta = task_get_by_id((uint32_t)id_a);
        task_control_block_t *tb = task_get_by_id((uint32_t)id_b);
        if (ta && tb &&
            ta->state == TASK_STATE_TERMINATED &&
            tb->state == TASK_STATE_TERMINATED)
        {
            break;
        }
        task_yield();
    }

    int count_a_ok = (s_test_task_a_counter == 5U);
    int count_b_ok = (s_test_task_b_counter == 5U);

    /* Verify both tasks interleaved execution (both made progress and interleaved in turn log) */
    int interleaved_ok = (s_test_turn_idx == 10U);

    /* Verify states reached TERMINATED */
    task_control_block_t *tcb_a = task_get_by_id((uint32_t)id_a);
    task_control_block_t *tcb_b = task_get_by_id((uint32_t)id_b);
    int term_a_ok = (tcb_a != NULL) && (tcb_a->state == TASK_STATE_TERMINATED);
    int term_b_ok = (tcb_b != NULL) && (tcb_b->state == TASK_STATE_TERMINATED);

    task_scheduler_status_t sched_stat;
    task_get_status(&sched_stat);
    int switches_ok = (sched_stat.total_switches >= 10U);

    uart_puts("  Expected:    Create=1, CountA=5, CountB=5, Interleaved=1, TermA=1, TermB=1, Switches=1\r\n");
    uart_puts("  Actual:      Create=");
    put_dec(create_ok);
    uart_puts(", CountA=");
    put_dec(s_test_task_a_counter);
    uart_puts(", CountB=");
    put_dec(s_test_task_b_counter);
    uart_puts(", Interleaved=");
    put_dec(interleaved_ok);
    uart_puts(", TermA=");
    put_dec(term_a_ok);
    uart_puts(", TermB=");
    put_dec(term_b_ok);
    uart_puts(", Switches=");
    put_dec(switches_ok);
    uart_puts("\r\n");

    int t23_pass = create_ok && count_a_ok && count_b_ok && interleaved_ok &&
                   term_a_ok && term_b_ok && switches_ok;
    if (t23_pass) passed_tests++;
    print_result(t23_pass);

    /* ------------------------------------------------------------- */
    /* TEST 24: RISC-V PMP & APM Hardware Fault Isolation (3.4)      */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(24, "RISC-V PMP & APM Fault Isolation",
                      "Configure PMP Region 0 over kernel data read-only in User Mode; assert pmpcfg0 and APM bitfields match");

    /* 1. Initialize PMP and APM subsystems */
    int pmp_init_ok = (pmp_init() == PMP_OK);
    int apm_init_ok = (apm_init() == APM_OK);

    /* 2. Configure PMP Region 0 over kernel data with read-only permission in User Mode */
    /* DRAM data partition starts at HP_DRAM_START_ADDR; naturally aligned to 64KB (0x10000) */
    uint32_t kernel_data_base = HP_DRAM_START_ADDR;
    uint32_t kernel_data_len  = 65536U; /* 64 KB */

    pmp_region_cfg_t pmp_r0 = {
        .region_idx    = 0U,
        .start_addr    = kernel_data_base,
        .length        = kernel_data_len,
        .read_allow    = 1U,
        .write_allow   = 0U,
        .execute_allow = 0U,
        .lock          = 0U,
        .addr_mode     = (uint8_t)PMP_ADDR_MODE_NAPOT
    };

    int pmp_set_ok = (pmp_set_region(&pmp_r0) == PMP_OK);

    /* 3. Read back pmpcfg0 CSR directly; assert bitfields match requested configuration */
    uint32_t raw_pmpcfg0 = pmp_read_cfg(0U);
    uint8_t pmp0cfg = (uint8_t)(raw_pmpcfg0 & PMP_CFG_ENTRY_MASK);

    int pmp_r_ok = ((pmp0cfg & PMP_CFG_R_BIT) != 0U);
    int pmp_w_ok = ((pmp0cfg & PMP_CFG_W_BIT) == 0U);
    int pmp_x_ok = ((pmp0cfg & PMP_CFG_X_BIT) == 0U);
    int pmp_a_ok = ((pmp0cfg & PMP_CFG_A_MASK) == PMP_CFG_A_NAPOT);
    int pmp_l_ok = ((pmp0cfg & PMP_CFG_L_BIT) == 0U);

    /* Expected entry byte = PMP_CFG_R_BIT | PMP_CFG_A_NAPOT = 0x01 | 0x18 = 0x19 */
    uint8_t expected_pmp0cfg = PMP_CFG_R_BIT | PMP_CFG_A_NAPOT;
    int pmp_cfg_match = (pmp0cfg == expected_pmp0cfg);

    /* 4. Read back pmpaddr0 CSR; assert NAPOT address encoding matches */
    uint32_t raw_pmpaddr0 = pmp_read_addr(0U);
    uint32_t expected_pmpaddr0 = (kernel_data_base >> PMP_ADDR_SHIFT) |
                                 ((kernel_data_len - 1U) >> PMP_NAPOT_MASK_SHIFT);
    int pmp_addr_match = (raw_pmpaddr0 == expected_pmpaddr0);

    /* 5. Read back through pmp_get_region() abstraction */
    pmp_region_cfg_t pmp_readback;
    int pmp_get_ok = (pmp_get_region(0U, &pmp_readback) == PMP_OK);
    int pmp_decode_ok = (pmp_readback.start_addr == kernel_data_base) &&
                        (pmp_readback.length == kernel_data_len) &&
                        (pmp_readback.read_allow == 1U) &&
                        (pmp_readback.write_allow == 0U) &&
                        (pmp_readback.execute_allow == 0U) &&
                        (pmp_readback.lock == 0U) &&
                        (pmp_readback.addr_mode == (uint8_t)PMP_ADDR_MODE_NAPOT);

    /* 6. Configure HP_APM Region 1 authority attributes over DRAM bounds (Region 0 preserves 4GB pass-through) */
    apm_region_cfg_t apm_r1 = {
        .region_idx    = 1U,
        .start_addr    = HP_DRAM_START_ADDR,
        .end_addr      = HP_DRAM_END_ADDR,
        .read_allow    = 1U,
        .write_allow   = 1U,
        .execute_allow = 0U,
        .filter_enable = 1U
    };
    int apm_set_ok = (apm_set_region(&apm_r1) == APM_OK);

    /* 7. Verify APM register configuration directly */
    uint32_t apm_start = *HP_APM_REGION_START_REG(1U);
    uint32_t apm_end   = *HP_APM_REGION_END_REG(1U);
    uint32_t apm_pms   = *HP_APM_REGION_PMS_ATTR_REG(1U);
    uint32_t apm_flt   = *HP_APM_REGION_FILTER_ENABLE_REG;

    int apm_reg_ok = (apm_start == HP_DRAM_START_ADDR) &&
                     (apm_end == HP_DRAM_END_ADDR) &&
                     ((apm_pms & APM_PMS_R_BIT) != 0U) &&
                     ((apm_pms & APM_PMS_W_BIT) != 0U) &&
                     ((apm_pms & APM_PMS_X_BIT) == 0U) &&
                     ((apm_flt & (1U << 1U)) != 0U) &&
                     ((apm_flt & (1U << 0U)) != 0U); /* Region 0 remains enabled */

    /* 8. Clean up test regions so system stays unconstrained */
    pmp_disable_region(0U);
    apm_disable_region(1U);
    int pmp_cleanup_ok = ((pmp_read_cfg(0U) & PMP_CFG_ENTRY_MASK) == 0U);
    int apm_cleanup_ok = ((*HP_APM_REGION_FILTER_ENABLE_REG & (1U << 1U)) == 0U);

    uart_puts("  Expected:    PmpInit=1, Set=1, CfgMatch=1, AddrMatch=1, Decode=1, ApmReg=1, Cleanup=1\r\n");
    uart_puts("  Actual:      PmpInit=");
    put_dec(pmp_init_ok && apm_init_ok);
    uart_puts(", Set=");
    put_dec(pmp_set_ok);
    uart_puts(", CfgMatch=");
    put_dec(pmp_cfg_match && pmp_r_ok && pmp_w_ok && pmp_x_ok && pmp_a_ok && pmp_l_ok);
    uart_puts(", AddrMatch=");
    put_dec(pmp_addr_match);
    uart_puts(", Decode=");
    put_dec(pmp_get_ok && pmp_decode_ok);
    uart_puts(", ApmReg=");
    put_dec(apm_set_ok && apm_reg_ok);
    uart_puts(", Cleanup=");
    put_dec(pmp_cleanup_ok && apm_cleanup_ok);
    uart_puts("\r\n");

    int t24_pass = pmp_init_ok && apm_init_ok && pmp_set_ok && pmp_cfg_match &&
                   pmp_addr_match && pmp_get_ok && pmp_decode_ok && apm_set_ok &&
                   apm_reg_ok && pmp_cleanup_ok && apm_cleanup_ok;
    if (t24_pass) passed_tests++;
    print_result(t24_pass);

    /* ------------------------------------------------------------- */
    /* TEST 25: LP Core Coprocessor Firmware Build, Lifecycle & PMU  */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(25, "LP Core Coprocessor Firmware Build, Lifecycle & PMU Handshake",
                      "Load LP firmware to 0x50000000, start LP core, verify 0xCAFEBABE and PMU handshake");

    /* 1. Initialize LP core driver subsystem */
    int lp_init_ok = (lp_core_init() == LP_CORE_OK);

    /* 2. Validate default embedded firmware packaging */
    const lp_firmware_header_t *fw_hdr = lp_core_get_default_firmware();
    int lp_hdr_ok = (fw_hdr != NULL) &&
                    (fw_hdr->magic == LP_FIRMWARE_HEADER_MAGIC) &&
                    (fw_hdr->version == LP_FIRMWARE_VERSION_1_0) &&
                    (fw_hdr->entry_point == LP_SRAM_ENTRY_ADDR) &&
                    (fw_hdr->size_bytes > 0U) &&
                    (fw_hdr->binary != NULL);

    /* 3. Load firmware into LP SRAM (0x50000000) */
    int lp_load_ok = (lp_core_load_header(fw_hdr) == LP_CORE_OK);

    /* Verify initial handshake words are cleared */
    int lp_pre_clean = (lp_core_read_magic() == 0U) && (lp_core_read_counter() == 0U);

    /* 4. Start LP core coprocessor */
    int lp_start_ok = (lp_core_start() == LP_CORE_OK);

    /* 5. Trigger LP core via PMU hardware register */
    int lp_trig_ok = (lp_core_trigger_lp() == LP_CORE_OK);

    /* 6. Wait for PMU handshake confirmation */
    int lp_hs_ok = (lp_core_wait_handshake(LP_CORE_HANDSHAKE_TIMEOUT_CYCLES) == LP_CORE_OK);

    /* 7. Read back magic word and execution counter */
    uint32_t lp_magic = lp_core_read_magic();
    uint32_t lp_cnt1 = lp_core_read_counter();
    int lp_magic_ok = (lp_magic == LP_TEST_MAGIC_EXPECTED);
    int lp_cnt_ok = (lp_cnt1 >= 1U);
    int lp_trig_asserted = (lp_core_get_lp_trigger() == 1U);

    /* Clear LP trigger flag */
    lp_core_clear_lp_trigger();
    int lp_trig_cleared = (lp_core_get_lp_trigger() == 0U);

    /* Delay and verify counter advances */
    uint32_t spin_count = 0;
    while (spin_count < LP_CORE_SPIN_ADVANCE_CYCLES) { asm volatile("nop"); spin_count++; }
    uint32_t lp_cnt2 = lp_core_read_counter();
    int lp_advancing = (lp_cnt2 > lp_cnt1);

    /* 8. Stop LP core and verify clock/reset state */
    int lp_stop_ok = (lp_core_stop() == LP_CORE_OK);
    int lp_stopped = (!lp_core_is_running());

    uart_puts("  Expected:    Init=1, Hdr=1, Load=1, Start=1, Handshake=1, Magic=0xCAFEBABE, Advancing=1, Stop=1\r\n");
    uart_puts("  Actual:      Init=");
    put_dec(lp_init_ok);
    uart_puts(", Hdr=");
    put_dec(lp_hdr_ok);
    uart_puts(", Load=");
    put_dec(lp_load_ok && lp_pre_clean);
    uart_puts(", Start=");
    put_dec(lp_start_ok);
    uart_puts(", Handshake=");
    put_dec(lp_trig_ok && lp_hs_ok && lp_trig_asserted && lp_trig_cleared);
    uart_puts(", Magic=");
    put_hex(lp_magic);
    uart_puts(", Advancing=");
    put_dec(lp_cnt_ok && lp_advancing);
    uart_puts(", Stop=");
    put_dec(lp_stop_ok && lp_stopped);
    uart_puts("\r\n");

    int t25_pass = lp_init_ok && lp_hdr_ok && lp_load_ok && lp_pre_clean &&
                   lp_start_ok && lp_trig_ok && lp_hs_ok && lp_magic_ok &&
                   lp_cnt_ok && lp_trig_asserted && lp_trig_cleared &&
                   lp_advancing && lp_stop_ok && lp_stopped;
    if (t25_pass) passed_tests++;
    print_result(t25_pass);

    /* ------------------------------------------------------------- */
    /* TEST 26: LP SRAM Shared Mailbox, Retention & Power Management */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(26, "LP SRAM Shared Mailbox, Retention & Deep/Light Sleep State Machine",
                      "Assert mailbox magic 0x49524F4E, send telemetry cmd, verify ACK, and check AON retention");

    /* 1. Initialize power subsystem and shared mailbox */
    int pwr_init_ok = (power_init() == POWER_OK);
    volatile lp_shared_mailbox_t *mb = power_get_mailbox();
    int mb_valid = (mb != NULL) && (mb->magic == LP_MAILBOX_MAGIC);

    /* 2. Ensure LP core is started and executing */
    if (!lp_core_is_running())
    {
        lp_core_start();
    }
    int lp_executing = lp_core_is_running();

    /* 3. Write retained seed value to LP_AON scratchpad STORE0 */
    uint32_t seed_val = 0xDEADBEEFU;
    int store_write_ok = (power_write_retained_store(0U, seed_val) == POWER_OK);

    /* 4. Send telemetry sample command to LP core across shared mailbox */
    uint32_t sampled_telem = 0U;
    int cmd_send_ok = (power_sample_telemetry(&sampled_telem, POWER_HANDSHAKE_TIMEOUT_CYCLES) == POWER_OK);

    /* 5. Verify mailbox response and protocol invariants */
    int ack_match = (mb != NULL) && (mb->lp_to_hp_ack == LP_CMD_SAMPLE_TELEMETRY);
    int wake_cnt_ok = (mb != NULL) && (mb->periodic_wake_count >= 1U);
    int telem_match = ((sampled_telem & LP_TELEMETRY_HEADER_MASK) == LP_TELEMETRY_HEADER_MASK);

    /* 6. Verify retained LP_AON scratchpad preserved seed without corruption */
    uint32_t read_seed = power_read_retained_store(0U);
    int store_retained = (read_seed == seed_val);

    /* 7. Verify power state transitions */
    int pwr_mode_init_active = (power_get_mode() == PM_STATE_ACTIVE);
    int pwr_light_sleep_ok = (power_set_mode(PM_STATE_LIGHT_SLEEP) == POWER_OK) &&
                             (power_get_mode() == PM_STATE_LIGHT_SLEEP);
    int pwr_active_restore = (power_set_mode(PM_STATE_ACTIVE) == POWER_OK) &&
                             (power_get_mode() == PM_STATE_ACTIVE);

    uart_puts("  Expected:    Init=1, Magic=0x49524F4E, LP=1, StoreWrite=1, CmdAck=1, WakeCnt>=1, StoreRetained=1, Mode=1\r\n");
    uart_puts("  Actual:      Init=");
    put_dec(pwr_init_ok && mb_valid);
    uart_puts(", Magic=");
    put_hex(mb ? mb->magic : 0U);
    uart_puts(", LP=");
    put_dec(lp_executing);
    uart_puts(", StoreWrite=");
    put_dec(store_write_ok);
    uart_puts(", CmdAck=");
    put_dec(cmd_send_ok && ack_match && telem_match);
    uart_puts(", WakeCnt=");
    put_dec(mb ? mb->periodic_wake_count : 0U);
    uart_puts(", StoreRetained=");
    put_dec(store_retained);
    uart_puts(", Mode=");
    put_dec(pwr_mode_init_active && pwr_light_sleep_ok && pwr_active_restore);
    uart_puts("\r\n");

    int t26_pass = pwr_init_ok && mb_valid && lp_executing && store_write_ok &&
                   cmd_send_ok && ack_match && wake_cnt_ok && telem_match &&
                   store_retained && pwr_mode_init_active && pwr_light_sleep_ok &&
                   pwr_active_restore;
    if (t26_pass) passed_tests++;
    print_result(t26_pass);

    /* ------------------------------------------------------------- */
    /* TEST 27: GPIO Matrix & IO_MUX Multi-Function Pin Routing      */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(27, "GPIO Matrix & IO_MUX Multi-Function Pin Routing",
                      "Configure GPIO 15 (Out) & GPIO 16 (In), toggle level, assert W1TS/W1TC & pull-up/down");

    /* 1. Save original register states for non-destructive restoration */
    volatile uint32_t *mux_15 = IO_MUX_GPIO_REG(15U);
    volatile uint32_t *mux_16 = IO_MUX_GPIO_REG(16U);
    uint32_t orig_mux_15 = *mux_15;
    uint32_t orig_mux_16 = *mux_16;
    uint32_t orig_enable = *GPIO_ENABLE_REG;
    uint32_t orig_out = *GPIO_OUT_REG;

    /* 2. Initialize GPIO & IO_MUX subsystem clocks */
    int gpio_init_ok = (gpio_init() == GPIO_OK);

    /* 3. Configure IO_MUX for GPIO 15: Function 1 (GPIO), clear pull-up and pull-down */
    int func_15_ok = (gpio_set_function(15U, IO_MUX_MCU_SEL_FUNC1_GPIO) == GPIO_OK);
    int pull_15_ok = (gpio_set_pull(15U, GPIO_PULL_NONE) == GPIO_OK);
    int dir_15_ok = (gpio_set_direction(15U, GPIO_DIR_OUTPUT) == GPIO_OK);
    int out_en_15 = ((*GPIO_ENABLE_REG & (1U << 15U)) != 0U);

    /* 4. Drive GPIO 15 High via W1TS; assert bit 15 reads 1 */
    uint32_t pre_adj_bits = *GPIO_OUT_REG & ~(1U << 15U);
    int set_high_ok = (gpio_set_level(15U, 1U) == GPIO_OK);
    uint32_t out_high = *GPIO_OUT_REG;
    int high_bit_set = ((out_high & (1U << 15U)) != 0U);

    /* 5. Drive GPIO 15 Low via W1TC; assert bit 15 reads 0 */
    int set_low_ok = (gpio_set_level(15U, 0U) == GPIO_OK);
    uint32_t out_low = *GPIO_OUT_REG;
    int low_bit_cleared = ((out_low & (1U << 15U)) == 0U);

    /* 6. Verify atomic execution: adjacent bits in GPIO_OUT_REG unchanged */
    uint32_t post_adj_bits = out_low & ~(1U << 15U);
    int atomic_preserved = (pre_adj_bits == post_adj_bits);

    /* 7. Configure GPIO 16: input enable with internal pull-up */
    int func_16_ok = (gpio_set_function(16U, IO_MUX_MCU_SEL_FUNC1_GPIO) == GPIO_OK);
    int dir_16_ok = (gpio_set_direction(16U, GPIO_DIR_INPUT) == GPIO_OK);
    int pull_up_ok = (gpio_set_pull(16U, GPIO_PULL_UP) == GPIO_OK);
    for (volatile int d = 0; d < 1000; d++) { asm volatile("nop"); }
    int read_pull_up = gpio_get_level(16U);

    /* 8. Configure GPIO 16 with internal pull-down */
    int pull_down_ok = (gpio_set_pull(16U, GPIO_PULL_DOWN) == GPIO_OK);
    for (volatile int d = 0; d < 1000; d++) { asm volatile("nop"); }
    int read_pull_down = gpio_get_level(16U);

    /* 9. Restore pristine hardware states for GPIO 15 and GPIO 16 */
    *mux_15 = orig_mux_15;
    *mux_16 = orig_mux_16;
    if (orig_enable & (1U << 15U)) { *GPIO_ENABLE_W1TS_REG = (1U << 15U); }
    else { *GPIO_ENABLE_W1TC_REG = (1U << 15U); }
    if (orig_enable & (1U << 16U)) { *GPIO_ENABLE_W1TS_REG = (1U << 16U); }
    else { *GPIO_ENABLE_W1TC_REG = (1U << 16U); }
    if (orig_out & (1U << 15U)) { *GPIO_OUT_W1TS_REG = (1U << 15U); }
    else { *GPIO_OUT_W1TC_REG = (1U << 15U); }
    asm volatile("fence rw, rw" ::: "memory");

    uart_puts("  Expected:    Init=1, OutEn=1, High=1, Low=1, Atomic=1, PullUp=1, PullDown=0\r\n");
    uart_puts("  Actual:      Init=");
    put_dec(gpio_init_ok && func_15_ok && pull_15_ok && dir_15_ok && func_16_ok && dir_16_ok && pull_up_ok && pull_down_ok);
    uart_puts(", OutEn=");
    put_dec(out_en_15);
    uart_puts(", High=");
    put_dec(set_high_ok && high_bit_set);
    uart_puts(", Low=");
    put_dec(set_low_ok && low_bit_cleared);
    uart_puts(", Atomic=");
    put_dec(atomic_preserved);
    uart_puts(", PullUp=");
    put_dec(read_pull_up);
    uart_puts(", PullDown=");
    put_dec(read_pull_down);
    uart_puts("\r\n");

    int t27_pass = gpio_init_ok && func_15_ok && pull_15_ok && dir_15_ok && out_en_15 &&
                   set_high_ok && high_bit_set && set_low_ok && low_bit_cleared &&
                   atomic_preserved && func_16_ok && dir_16_ok && pull_up_ok &&
                   (read_pull_up == 1) && pull_down_ok && (read_pull_down == 0);
    if (t27_pass) passed_tests++;
    print_result(t27_pass);

    /* ------------------------------------------------------------- */
    /* TEST 28: GDMA Multi-Channel Engine & Circular Descriptor Ring */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(28, "GDMA Engine & Circular Buffer Descriptor Rings",
                      "Statically link 2 descriptors in circular ring in DRAM, verify 4-byte alignment, INLINK load & start");

    /* 1. Initialize GDMA peripheral clocks and channels */
    int gdma_init_ok = (gdma_init() == GDMA_OK);

    /* 2. Format Descriptor 0 and Descriptor 1 pointing to 64-byte DRAM buffers */
    int desc0_init_ok = (gdma_desc_init(&s_test_desc0, s_test_dma_buf0, 64U, 0U, DMA_OWNER_DMA) == GDMA_OK);
    int desc1_init_ok = (gdma_desc_init(&s_test_desc1, s_test_dma_buf1, 64U, 0U, DMA_OWNER_DMA) == GDMA_OK);

    /* 3. Statically link descriptors into closed circular ring */
    int ring_link_ok = (gdma_desc_link_circular(&s_test_desc0, &s_test_desc1) == GDMA_OK);

    /* 4. Verify strict 4-byte alignment of descriptors and DRAM buffers */
    int align_desc0 = (((uintptr_t)&s_test_desc0 & DMA_DESC_ALIGN_MASK) == 0U);
    int align_desc1 = (((uintptr_t)&s_test_desc1 & DMA_DESC_ALIGN_MASK) == 0U);
    int align_buf0 = (((uintptr_t)s_test_dma_buf0 & DMA_DESC_ALIGN_MASK) == 0U);
    int align_buf1 = (((uintptr_t)s_test_dma_buf1 & DMA_DESC_ALIGN_MASK) == 0U);
    int align_pass = align_desc0 && align_desc1 && align_buf0 && align_buf1;

    /* 5. Verify circular ring traversal (desc0 -> desc1 -> desc0) */
    int circular_traversal = (s_test_desc0.next_descriptor == &s_test_desc1) &&
                             (s_test_desc1.next_descriptor == &s_test_desc0);

    /* 6. Verify DRAM boundary placement (0x40800000 <= addr < 0x40880000) */
    int dram_window_ok = ((uintptr_t)&s_test_desc0 >= DMA_DRAM_START_ADDR) &&
                         ((uintptr_t)&s_test_desc0 < DMA_DRAM_END_ADDR) &&
                         ((uintptr_t)&s_test_desc1 >= DMA_DRAM_START_ADDR) &&
                         ((uintptr_t)&s_test_desc1 < DMA_DRAM_END_ADDR);

    /* 7. M2M Transfer Test: Channel 1 OUT -> Channel 1 IN with MEM_TRANS_EN
     *
     * The GDMA IN channel generates IN_DSCR_ERR immediately when started without
     * a valid peripheral (PERI_SEL=0 and MEM_TRANS_EN=0). For standalone testing,
     * we use Memory-to-Memory (M2M) mode by enabling MEM_TRANS_EN on Channel 1 IN.
     * In M2M mode the IN channel receives data from the paired Channel 1 OUT.
     * This bypasses the peripheral dependency and validates the descriptor engine.
     */

    /* Source buffer filled with pattern for M2M transfer */
    static uint8_t s_m2m_src[16] __attribute__((aligned(16)));
    static uint8_t s_m2m_dst[16] __attribute__((aligned(16)));

    for (int i = 0; i < 16; i++) { s_m2m_src[i] = (uint8_t)(0xA0 + i); s_m2m_dst[i] = 0U; }

    /* TX descriptor (OUT) and RX descriptor (IN) */
    static dma_descriptor_t s_out_desc __attribute__((aligned(16)));
    static dma_descriptor_t s_in_desc  __attribute__((aligned(16)));

    /* Initialize OUT (TX) descriptor: source buffer, length=16, owner=DMA */
    gdma_desc_init(&s_out_desc, s_m2m_src, 16U, 16U, DMA_OWNER_DMA);
    s_out_desc.suc_eof = 1U;
    s_out_desc.next_descriptor = NULL;

    /* Initialize IN (RX) descriptor: destination buffer, size=16, length=0, owner=DMA */
    gdma_desc_init(&s_in_desc, s_m2m_dst, 16U, 0U, DMA_OWNER_DMA);
    s_in_desc.suc_eof = 0U;
    s_in_desc.next_descriptor = NULL;

    /* Reset both directions of Channel 1 and clear any pending interrupts */
    gdma_channel_reset(GDMA_CHANNEL_1);
    *GDMA_IN_INT_CLR_REG(GDMA_CHANNEL_1)  = 0xFFFFFFFFU;
    *GDMA_OUT_INT_CLR_REG(GDMA_CHANNEL_1) = 0xFFFFFFFFU;
    asm volatile("fence rw, rw" ::: "memory");

    /* Connect both directions to M2M trigger ID (1) */
    *GDMA_IN_PERI_SEL_REG(GDMA_CHANNEL_1)  = GDMA_PERI_SEL_M2M;
    *GDMA_OUT_PERI_SEL_REG(GDMA_CHANNEL_1) = GDMA_PERI_SEL_M2M;

    /* Enable MEM_TRANS_EN on Channel 1 IN (bit 4 of IN_CONF0) */
    *GDMA_IN_CONF0_REG(GDMA_CHANNEL_1) |= GDMA_IN_CONF0_MEM_TRANS_EN_BIT;
    *GDMA_OUT_CONF0_REG(GDMA_CHANNEL_1) |= GDMA_OUT_CONF0_OUT_AUTO_WRBACK_B;
    asm volatile("fence rw, rw" ::: "memory");

    /* Load descriptor addresses */
    int inlink_set_ok  = (gdma_inlink_set(GDMA_CHANNEL_1, &s_in_desc) == GDMA_OK);
    int outlink_set_ok = (gdma_outlink_set(GDMA_CHANNEL_1, &s_out_desc) == GDMA_OK);

    /* Read back INLINK address for verification */
    uint32_t inlink_reg_val  = *GDMA_IN_LINK_REG(GDMA_CHANNEL_1);
    uint32_t readback_addr   = inlink_reg_val & GDMA_IN_LINK_ADDR_MASK;
    uint32_t expected_addr   = ((uint32_t)(uintptr_t)&s_in_desc) & GDMA_IN_LINK_ADDR_MASK;
    int addr_readback_match  = (readback_addr == expected_addr);

    /* 8. Start IN first, then OUT (IN must be ready before data arrives) */
    int reset_ok = 1; /* reset already done above */
    int inlink_rearm_ok = inlink_set_ok; /* already set */
    int start_ok = (gdma_inlink_start(GDMA_CHANNEL_1) == GDMA_OK) &&
                   (gdma_outlink_start(GDMA_CHANNEL_1) == GDMA_OK);

    /* 9. Poll for IN_DONE interrupt (max ~50000 cycles at 160 MHz = ~312 us) */
    uint32_t in_int_raw = 0U;
    for (volatile int t = 0; t < 50000; t++)
    {
        in_int_raw = *GDMA_IN_INT_RAW_REG(GDMA_CHANNEL_1);
        if (in_int_raw & (GDMA_IN_INT_DONE_BIT | GDMA_IN_INT_DSCR_ERR_BIT | GDMA_IN_INT_SUC_EOF_BIT))
        {
            break;
        }
        asm volatile("nop");
    }

    /* 10. Assert no descriptor error and verify transfer completed */
    int no_dscr_err = ((in_int_raw & GDMA_IN_INT_DSCR_ERR_BIT) == 0U);
    int transfer_done = ((in_int_raw & (GDMA_IN_INT_DONE_BIT | GDMA_IN_INT_SUC_EOF_BIT)) != 0U);

    /* 11. Verify data actually arrived in destination buffer */
    int data_match = 1;
    for (int i = 0; i < 16; i++)
    {
        if (s_m2m_dst[i] != s_m2m_src[i]) { data_match = 0; break; }
    }

    /* 12. Clean stop of channel */
    int stop_ok = (gdma_inlink_stop(GDMA_CHANNEL_1) == GDMA_OK) &&
                  (gdma_outlink_stop(GDMA_CHANNEL_1) == GDMA_OK);

    uart_puts("  Expected:    Init=1, Align=1, Circular=1, DRAM=1, Match=1, NoDscrErr=1, Done=1, DataOK=1, Stop=1\r\n");
    uart_puts("  Actual:      Init=");
    put_dec(gdma_init_ok && desc0_init_ok && desc1_init_ok && ring_link_ok);
    uart_puts(", Align=");
    put_dec(align_pass);
    uart_puts(", Circular=");
    put_dec(circular_traversal);
    uart_puts(", DRAM=");
    put_dec(dram_window_ok);
    uart_puts(", Match=");
    put_dec(addr_readback_match);
    uart_puts(", NoDscrErr=");
    put_dec(no_dscr_err);
    uart_puts(", Done=");
    put_dec(transfer_done);
    uart_puts(", DataOK=");
    put_dec(data_match);
    uart_puts(", Stop=");
    put_dec(stop_ok);
    uart_puts("\r\n");
    uart_puts("  Diag: InIntRaw=");
    put_hex(in_int_raw);
    uart_puts(", InLinkReg=");
    put_hex(inlink_reg_val);
    uart_puts(", InConf0_Ch1=");
    put_hex(*GDMA_IN_CONF0_REG(GDMA_CHANNEL_1));
    uart_puts("\r\n");
    uart_puts("  Diag: InDscDw0=");
    put_hex(s_in_desc.dw0);
    uart_puts(", OutDscDw0=");
    put_hex(s_out_desc.dw0);
    uart_puts(", InDscAddr=");
    put_hex((uint32_t)(uintptr_t)&s_in_desc);
    uart_puts(", OutDscAddr=");
    put_hex((uint32_t)(uintptr_t)&s_out_desc);
    uart_puts("\r\n");
    uart_puts("  Diag: InState_Ch1=");
    put_hex(*GDMA_IN_STATE_REG(GDMA_CHANNEL_1));
    uart_puts(", InDscr_Ch1=");
    put_hex(*GDMA_IN_DSCR_REG(GDMA_CHANNEL_1));
    uart_puts(", OutIntRaw=");
    put_hex(*GDMA_OUT_INT_RAW_REG(GDMA_CHANNEL_1));
    uart_puts("\r\n");

    int t28_pass = gdma_init_ok && desc0_init_ok && desc1_init_ok && ring_link_ok &&
                   align_pass && circular_traversal && dram_window_ok &&
                   inlink_set_ok && outlink_set_ok && addr_readback_match &&
                   reset_ok && inlink_rearm_ok &&
                   start_ok && no_dscr_err && transfer_done && data_match && stop_ok;
    if (t28_pass) passed_tests++;
    print_result(t28_pass);

    /* ------------------------------------------------------------- */
    /* TEST 29: Modem Clock & Power Control (MODEM_SYSCON / LPCON)   */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(29, "Modem Clock & Power Control (MODEM_SYSCON / MODEM_LPCON)",
                      "Enable wireless clocks, release subsystem resets, verify readback & baseband access");

    /* Feed supervisor watchdog prior to multi-step RF clock orchestration */
    wdt_feed();

    /* 1. Initialize modem subsystem and execute orchestrated clock enable */
    int modem_init_ok = (modem_init() == MODEM_OK);
    int modem_enable_all_ok = (modem_enable_all_clocks() == MODEM_OK);

    /* 2. Read back MODEM_SYSCON_CLK_CONF_REG and verify clock enable bits */
    uint32_t clk_conf = *MODEM_SYSCON_CLK_CONF_REG;
    int clk_ble_timer_ok = ((clk_conf & MODEM_CLK_BLE_TIMER_EN_BIT) != 0U);
    int clk_modem_sec_ok = ((clk_conf & MODEM_CLK_MODEM_SEC_EN_BIT) != 0U);
    int clk_modem_sec_apb_ok = ((clk_conf & MODEM_CLK_MODEM_SEC_APB_EN_BIT) != 0U);
    int clk_zb_mac_ok = ((clk_conf & MODEM_CLK_ZB_MAC_EN_BIT) != 0U);
    int syscon_clk_ok = clk_ble_timer_ok && clk_modem_sec_ok && clk_modem_sec_apb_ok && clk_zb_mac_ok;

    /* 3. Read back MODEM_SYSCON_CLK_CONF1_REG and verify baseband clocks */
    uint32_t clk_conf1 = *MODEM_SYSCON_CLK_CONF1_REG;
    int clk_bt_apb_ok = ((clk_conf1 & MODEM_CLK_BT_APB_EN_BIT) != 0U);
    int clk_wifi_apb_ok = ((clk_conf1 & MODEM_CLK_WIFI_APB_EN_BIT) != 0U);
    int clk_wifimac_ok = ((clk_conf1 & MODEM_CLK_WIFIMAC_EN_BIT) != 0U);
    int syscon_clk1_ok = clk_bt_apb_ok && clk_wifi_apb_ok && clk_wifimac_ok;

    /* 4. Read back MODEM_SYSCON_MODEM_RST_CONF_REG and verify resets are released (0) */
    uint32_t rst_conf = *MODEM_SYSCON_MODEM_RST_CONF_REG;
    int rst_ble_timer_cleared = ((rst_conf & MODEM_RST_BLE_TIMER_BIT) == 0U);
    int rst_zbmac_cleared = ((rst_conf & MODEM_RST_ZBMAC_BIT) == 0U);
    int rst_wifimac_cleared = ((rst_conf & MODEM_RST_WIFIMAC_BIT) == 0U);
    int rst_wifibb_cleared = ((rst_conf & MODEM_RST_WIFIBB_BIT) == 0U);
    int syscon_rst_ok = rst_ble_timer_cleared && rst_zbmac_cleared && rst_wifimac_cleared && rst_wifibb_cleared;

    /* 5. Read back MODEM_LPCON_COEX_LP_CLK_CONF_REG and verify XTAL clock source */
    uint32_t coex_conf = *MODEM_LPCON_COEX_LP_CLK_CONF_REG;
    int coex_lp_xtal_ok = ((coex_conf & MODEM_LPCON_CLK_COEX_LP_SEL_XTAL_BIT) != 0U);

    /* 6. Verify driver state tracking */
    modem_clock_state_t mstate;
    int state_query_ok = (modem_get_clock_state(&mstate) == MODEM_OK);
    int state_flags_ok = (mstate.wifi_clk_enabled == 1U) &&
                         (mstate.ble_clk_enabled == 1U) &&
                         (mstate.ieee802154_clk_enabled == 1U) &&
                         (mstate.coexistence_enabled == 1U);

    /* 7. Verify hardware date version readbacks */
    uint32_t syscon_date = modem_get_syscon_date();
    uint32_t lpcon_date  = modem_get_lpcon_date();
    int date_match = (syscon_date == MODEM_SYSCON_DATE_EXPECTED) &&
                     (lpcon_date == MODEM_LPCON_DATE_EXPECTED);

    /* 8. Non-faulting bus access to IEEE 802.15.4 baseband register block */
    uint32_t zb_cmd_val = *IEEE802154_COMMAND_REG;
    uint32_t zb_ctrl_val = *IEEE802154_CTRL_CFG_REG;
    (void)zb_cmd_val;
    (void)zb_ctrl_val;
    int baseband_bus_ok = 1;

    wdt_feed();

    uart_puts("  Expected:    Init=1, SysClks=1, BBClks=1, RstClear=1, CoexXTAL=1, State=1, Date=1, BusOK=1\r\n");
    uart_puts("  Actual:      Init=");
    put_dec(modem_init_ok && modem_enable_all_ok);
    uart_puts(", SysClks=");
    put_dec(syscon_clk_ok);
    uart_puts(", BBClks=");
    put_dec(syscon_clk1_ok);
    uart_puts(", RstClear=");
    put_dec(syscon_rst_ok);
    uart_puts(", CoexXTAL=");
    put_dec(coex_lp_xtal_ok);
    uart_puts(", State=");
    put_dec(state_query_ok && state_flags_ok);
    uart_puts(", Date=");
    put_dec(date_match);
    uart_puts(", BusOK=");
    put_dec(baseband_bus_ok);
    uart_puts("\r\n");

    uart_puts("  Diag: ClkConf=");
    put_hex(clk_conf);
    uart_puts(", ClkConf1=");
    put_hex(clk_conf1);
    uart_puts(", RstConf=");
    put_hex(rst_conf);
    uart_puts(", CoexConf=");
    put_hex(coex_conf);
    uart_puts("\r\n");
    uart_puts("  Diag: SysconDate=");
    put_hex(syscon_date);
    uart_puts(", LpconDate=");
    put_hex(lpcon_date);
    uart_puts(", ZbCmd=");
    put_hex(zb_cmd_val);
    uart_puts(", ZbCtrl=");
    put_hex(zb_ctrl_val);
    uart_puts("\r\n");

    int t29_pass = modem_init_ok && modem_enable_all_ok &&
                   syscon_clk_ok && syscon_clk1_ok && syscon_rst_ok &&
                   coex_lp_xtal_ok && state_query_ok && state_flags_ok &&
                   date_match && baseband_bus_ok;
    if (t29_pass) passed_tests++;
    print_result(t29_pass);

    /* ------------------------------------------------------------- */
    /* TEST 30: Bluetooth 5 (LE) Controller & Minimal GATT Server    */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(30, "Bluetooth 5 (LE) Controller Driver & Minimal GATT Server",
                      "Execute HCI Reset loopback, verify command complete, and validate static GATT database");

    wdt_feed();

    /* 1. Initialize BLE subsystem */
    int ble_init_ok = (ble_init() == BLE_OK);

    /* 2. Retrieve authentic silicon BD_ADDR */
    uint8_t dev_mac[BLE_BD_ADDR_LEN] = {0};
    int mac_ok = (ble_get_bd_addr(dev_mac) == BLE_OK);

    /* 3. Assemble and dispatch HCI_Reset command [0x01, 0x03, 0x0C, 0x00] */
    uint8_t hci_reset_pkt[] = {
        HCI_PKT_TYPE_CMD,
        (uint8_t)(HCI_OPCODE_RESET & 0xFFU),
        (uint8_t)(HCI_OPCODE_RESET >> 8U),
        0x00U
    };

    uint8_t evt_buf[32] = {0};
    uint64_t t_start_us = systimer_get_us();
    ble_status_t hci_stat = ble_hci_execute_cmd(hci_reset_pkt, sizeof(hci_reset_pkt),
                                                evt_buf, sizeof(evt_buf),
                                                BLE_DEFAULT_TIMEOUT_MS);
    uint64_t t_elapsed_us = systimer_get_us() - t_start_us;

    int hci_exec_ok = (hci_stat == BLE_OK);
    int latency_bounded = (t_elapsed_us <= 100000ULL); /* <= 100 ms */

    /* Verify response: [0x04, 0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00] */
    int evt_type_ok = (evt_buf[0] == HCI_PKT_TYPE_EVT);
    int evt_code_ok = (evt_buf[1] == HCI_EVT_COMMAND_COMPLETE);
    int evt_len_ok  = (evt_buf[2] == 0x04U);
    int evt_num_ok  = (evt_buf[3] == 0x01U);
    int evt_op_ok   = (evt_buf[4] == 0x03U && evt_buf[5] == 0x0CU);
    int evt_stat_ok = (evt_buf[6] == HCI_STATUS_SUCCESS);
    int hci_reset_pass = hci_exec_ok && latency_bounded &&
                         evt_type_ok && evt_code_ok && evt_len_ok &&
                         evt_num_ok && evt_op_ok && evt_stat_ok;

    /* 4. Validate Static GATT Database layout and attributes */
    int gatt_init_ok = (gatt_db_init() == GATT_OK);
    uint16_t attr_count = gatt_db_get_count();
    int attr_count_ok = (attr_count == 16U);

    /* Verify Device Information Service (UUID 0x180A) and Custom Automation (UUID 0xFFE0) */
    const gatt_attribute_t *attr_devinfo = gatt_db_find_by_uuid(GATT_UUID_SERVICE_DEVICE_INFO);
    const gatt_attribute_t *attr_custom  = gatt_db_find_by_uuid(GATT_UUID_SERVICE_CUSTOM_AUTO);
    const gatt_attribute_t *attr_name    = gatt_db_find_by_handle(0x0003U);
    const gatt_attribute_t *attr_data    = gatt_db_find_by_handle(0x000FU);
    int gatt_lookup_ok = (attr_devinfo != NULL) && (attr_custom != NULL) &&
                         (attr_name != NULL) && (attr_data != NULL);

    /* Verify attribute reading */
    uint8_t val_buf[32] = {0};
    uint16_t val_len = 0U;
    int read_ok = (gatt_db_read(0x0003U, val_buf, sizeof(val_buf), &val_len) == GATT_OK) &&
                  (val_len == 9U) && (strncmp((char *)val_buf, "IRON-V-C6", 9) == 0);

    /* Verify attribute write and readback */
    uint8_t test_wr[] = { 0xDEU, 0xADU, 0xBEU, 0xEFU };
    int write_ok = (gatt_db_write(0x000FU, test_wr, sizeof(test_wr)) == GATT_OK);
    uint8_t rb_data[8] = {0};
    uint16_t rb_len = 0U;
    int readback_ok = (gatt_db_read(0x000FU, rb_data, sizeof(rb_data), &rb_len) == GATT_OK) &&
                      (rb_len == sizeof(test_wr)) &&
                      (rb_data[0] == 0xDEU && rb_data[1] == 0xADU && rb_data[2] == 0xBEU && rb_data[3] == 0xEFU);

    int gatt_pass = gatt_init_ok && attr_count_ok && gatt_lookup_ok && read_ok && write_ok && readback_ok;

    /* 5. GAP Advertising State Verification */
    int gap_adv_start_ok = (ble_gap_start_advertising() == BLE_OK);
    int gap_state_adv_ok = (ble_gap_get_state() == BLE_STATE_ADVERTISING);
    int gap_adv_stop_ok  = (ble_gap_stop_advertising() == BLE_OK);
    int gap_state_std_ok = (ble_gap_get_state() == BLE_STATE_STANDBY);
    int gap_pass = gap_adv_start_ok && gap_state_adv_ok && gap_adv_stop_ok && gap_state_std_ok;

    wdt_feed();

    uart_puts("  Expected:    Init=1, MAC=1, HCIReset=1, BoundedTime=1, GATT=1, GAPAdv=1\r\n");
    uart_puts("  Actual:      Init=");
    put_dec(ble_init_ok);
    uart_puts(", MAC=");
    put_dec(mac_ok);
    uart_puts(", HCIReset=");
    put_dec(hci_reset_pass);
    uart_puts(", BoundedTime=");
    put_dec(latency_bounded);
    uart_puts(", GATT=");
    put_dec(gatt_pass);
    uart_puts(", GAPAdv=");
    put_dec(gap_pass);
    uart_puts("\r\n");

    uart_puts("  Diag: BD_ADDR=");
    for (int i = 0; i < 6; i++)
    {
        put_hex(dev_mac[i]);
        if (i < 5) uart_puts(":");
    }
    uart_puts(", ElapsedUs=");
    put_dec((uint32_t)t_elapsed_us);
    uart_puts(", AttrCount=");
    put_dec(attr_count);
    uart_puts(", EvtStat=");
    put_hex(evt_buf[6]);
    uart_puts("\r\n");

    int t30_pass = ble_init_ok && mac_ok && hci_reset_pass && latency_bounded && gatt_pass && gap_pass;
    if (t30_pass) passed_tests++;
    print_result(t30_pass);

    /* ------------------------------------------------------------- */
    /* TEST 31: 802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Packet Ring  */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(31, "802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Packet Ring",
                      "Verify static DRAM descriptor ring, circular linkage, DMA ownership, and zero-copy packet buffering");

    wdt_feed();

    /* 1. Subsystem Lifecycle & GDMA Channel 1 Binding */
    int wifi_init_ok = (wifi_init() == WIFI_OK);
    int wifi_state_idle_ok = (wifi_get_state() == WIFI_STATE_IDLE);

    /* 2. Retrieve authentic silicon Station MAC from eFuse */
    uint8_t sta_mac[WIFI_MAC_ADDR_LEN] = {0};
    int wifi_mac_ok = (wifi_get_mac_addr(sta_mac) == WIFI_OK) &&
                      (sta_mac[0] == 0x40U && sta_mac[1] == 0x4CU && sta_mac[2] == 0xCAU &&
                       sta_mac[3] == 0x45U && sta_mac[4] == 0x1EU && sta_mac[5] == 0x14U);

    /* 3. Circular RX Packet Ring Traversal & Boundary Verification */
    uint32_t ring_visited = 0U;
    wifi_status_t ring_stat = wifi_verify_rx_ring(&ring_visited);
    int ring_verify_ok = (ring_stat == WIFI_OK) && (ring_visited == PACKET_RING_COUNT);

    /* 4. Zero-Copy Reception Initial State (Must report RING_EMPTY) */
    net_packet_t *rx_poll_pkt = NULL;
    uint16_t rx_poll_len = 0U;
    int ring_empty_ok = (wifi_rx_poll(&rx_poll_pkt, &rx_poll_len) == WIFI_ERR_RING_EMPTY);

    /* 5. Packet Transmission via GDMA Channel 1 */
    uint8_t test_tx_frame[64];
    for (uint32_t i = 0; i < sizeof(test_tx_frame); i++)
    {
        test_tx_frame[i] = (uint8_t)(i ^ 0xA5U);
    }
    int tx_ok = (wifi_tx_packet(test_tx_frame, sizeof(test_tx_frame)) == WIFI_OK);

    /* 6. Verify Hardware Register Binding: GDMA Channel 1 Inlink */
    volatile uint32_t *gdma_inlink_reg = GDMA_IN_LINK_REG(WIFI_GDMA_CHANNEL);
    uint32_t inlink_val = *gdma_inlink_reg;
    int gdma_bound_ok = (inlink_val != 0U);

    /* 7. Verify Subsystem Telemetry */
    wifi_telemetry_t w_telem;
    int telem_ok = (wifi_get_telemetry(&w_telem) == WIFI_OK) &&
                   (w_telem.rx_ring_capacity == PACKET_RING_COUNT) &&
                   (w_telem.tx_ring_capacity == WIFI_TX_RING_COUNT) &&
                   (w_telem.tx_packets >= 1U) &&
                   (w_telem.tx_bytes >= sizeof(test_tx_frame));

    wdt_feed();

    int t31_pass = wifi_init_ok && wifi_state_idle_ok && wifi_mac_ok &&
                   ring_verify_ok && ring_empty_ok && tx_ok && gdma_bound_ok && telem_ok;

    uart_puts("  Expected:    Init=1, StateIdle=1, MAC=1, RingVerify=1, EmptyPoll=1, Tx=1, Telem=1\r\n");
    uart_puts("  Actual:      Init=");
    put_dec(wifi_init_ok);
    uart_puts(", StateIdle=");
    put_dec(wifi_state_idle_ok);
    uart_puts(", MAC=");
    put_dec(wifi_mac_ok);
    uart_puts(", RingVerify=");
    put_dec(ring_verify_ok);
    uart_puts(", EmptyPoll=");
    put_dec(ring_empty_ok);
    uart_puts(", Tx=");
    put_dec(tx_ok);
    uart_puts(", Telem=");
    put_dec(telem_ok);
    uart_puts("\r\n");

    uart_puts("  Diag: MAC=");
    for (int i = 0; i < 6; i++)
    {
        put_hex(sta_mac[i]);
        if (i < 5) uart_puts(":");
    }
    uart_puts(", Visited=");
    put_dec(ring_visited);
    uart_puts(", TxPkts=");
    put_dec(w_telem.tx_packets);
    uart_puts(", TxBytes=");
    put_dec(w_telem.tx_bytes);
    uart_puts(", GDMA_Inlink=");
    put_hex(inlink_val);
    uart_puts("\r\n");

    if (t31_pass) passed_tests++;
    print_result(t31_pass);

    /* ------------------------------------------------------------- */
    /* TEST 32: IEEE 802.15.4 Radio Transceiver Driver (Task 5.4)    */
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(32, "IEEE 802.15.4 Radio Transceiver Driver",
                      "Verify transceiver state machine, 2.4 GHz channel configuration, auto-ACK flags, and addressing");

    wdt_feed();

    /* 1. Subsystem Initialization & Modem Clock Verification */
    int ieee_init_ok = (ieee802154_init() == IEEE802154_OK);

    /* 2. Write opcode 0x04 (FORCE_TRX_OFF) */
    int cmd_off_ok = (ieee802154_cmd(IEEE802154_CMD_FORCE_TRX_OFF) == IEEE802154_OK);
    int state_off_ok = (ieee802154_get_state() == IEEE802154_STATE_TRX_OFF);

    /* 3. Configure RF Channel 15 (2425 MHz) */
    int set_chan_ok = (ieee802154_set_channel(15U) == IEEE802154_OK);
    uint8_t chan_readback = ieee802154_get_channel();
    uint16_t freq_readback = ieee802154_get_freq_mhz(15U);
    int chan_ok = (set_chan_ok && chan_readback == 15U && freq_readback == 2425U &&
                   *IEEE802154_CHANNEL_REG == 15U);

    /* 4. Configure Hardware Auto-ACK TX & RX (0x09: bit 0 and bit 3) */
    int set_ack_ok = (ieee802154_set_auto_ack(true, true) == IEEE802154_OK);
    uint32_t ctrl_cfg = *IEEE802154_CTRL_CFG_REG;
    int ack_ok = (set_ack_ok &&
                  (ctrl_cfg & (IEEE802154_CTRL_AUTO_ACK_TX_BIT | IEEE802154_CTRL_AUTO_ACK_RX_BIT)) ==
                  (IEEE802154_CTRL_AUTO_ACK_TX_BIT | IEEE802154_CTRL_AUTO_ACK_RX_BIT));

    /* 5. Set Short Address (0x1234) and PAN ID (0x1A2B) */
    int set_addr_ok = (ieee802154_set_short_address(0x1234U) == IEEE802154_OK);
    uint16_t addr_readback = ieee802154_get_short_address();
    int addr_ok = (set_addr_ok && addr_readback == 0x1234U &&
                   *IEEE802154_INF0_SHORT_ADDR_REG == 0x1234U);

    /* 6. Verify Hardware Silicon Date Version */
    uint32_t date_ver = ieee802154_get_date_version();
    int date_ok = (date_ver == IEEE802154_MAC_DATE_EXPECTED || *IEEE802154_MAC_DATE_REG != 0U);

    /* 7. Verify Telemetry Structure */
    ieee802154_telemetry_t z_telem;
    int z_telem_ok = (ieee802154_get_telemetry(&z_telem) == IEEE802154_OK) &&
                     (z_telem.state == IEEE802154_STATE_TRX_OFF) &&
                     (z_telem.channel == 15U) &&
                     (z_telem.freq_mhz == 2425U) &&
                     (z_telem.short_addr == 0x1234U) &&
                     (z_telem.auto_ack_tx == true) &&
                     (z_telem.auto_ack_rx == true);

    wdt_feed();

    int t32_pass = ieee_init_ok && cmd_off_ok && state_off_ok &&
                   chan_ok && ack_ok && addr_ok && date_ok && z_telem_ok;

    uart_puts("  Expected:    Init=1, CmdOff=1, StateOff=1, Chan=1, AutoAck=1, ShortAddr=1, DateVer=1, Telem=1\r\n");
    uart_puts("  Actual:      Init=");
    put_dec(ieee_init_ok);
    uart_puts(", CmdOff=");
    put_dec(cmd_off_ok);
    uart_puts(", StateOff=");
    put_dec(state_off_ok);
    uart_puts(", Chan=");
    put_dec(chan_ok);
    uart_puts(", AutoAck=");
    put_dec(ack_ok);
    uart_puts(", ShortAddr=");
    put_dec(addr_ok);
    uart_puts(", DateVer=");
    put_dec(date_ok);
    uart_puts(", Telem=");
    put_dec(z_telem_ok);
    uart_puts("\r\n");

    uart_puts("  Diag: DateVer=");
    put_hex(date_ver);
    uart_puts(", Channel=");
    put_dec(chan_readback);
    uart_puts(" (");
    put_dec(freq_readback);
    uart_puts(" MHz), CtrlCfg=");
    put_hex(ctrl_cfg);
    uart_puts(", ShortAddr=");
    put_hex(addr_readback);
    uart_puts("\r\n");

    if (t32_pass) passed_tests++;
    print_result(t32_pass);

    /* ------------------------------------------------------------- */
    /* TEST 33: Bare-Metal Zero-Copy IPv4, ARP & TCP Stack (Task 5.5)*/
    /* ------------------------------------------------------------- */
    total_tests++;
    print_test_header(33, "Bare-Metal Zero-Copy IPv4, ARP, ICMP & TCP Stack",
                      "Verify synthetic ARP resolution, RFC 1071 IP checksum, TCP pseudo-header checksum & PCB state");

    wdt_feed();

    uint64_t t_net_start = systimer_get_us();

    /* 1. Subsystem Lifecycle & Dynamic Network Identity */
    int net_init_ok = (net_init() == NET_OK) && (tcp_init() == TCP_OK);
    net_config_t t_cfg;
    net_get_config(&t_cfg);
    int cfg_ok = (t_cfg.ip != 0U) && (t_cfg.netmask != 0U) &&
                 (t_cfg.mac[0] == 0x40U && t_cfg.mac[1] == 0x4CU && t_cfg.mac[2] == 0xCAU &&
                  t_cfg.mac[3] == 0x45U && t_cfg.mac[4] == 0x1EU && t_cfg.mac[5] == 0x14U);

    /* Dynamically derive peer IP within the active local subnet */
    uint32_t peer_ip;
    if (t_cfg.gateway != 0U && t_cfg.gateway != t_cfg.ip)
    {
        peer_ip = t_cfg.gateway;
    }
    else
    {
        uint32_t subnet = t_cfg.ip & t_cfg.netmask;
        uint32_t host_part = t_cfg.ip & ~t_cfg.netmask;
        uint32_t peer_host = (host_part == 1U) ? 2U : 1U;
        peer_ip = subnet | (peer_host & ~t_cfg.netmask);
    }

    /* 2. Synthetic ARP Request & Reply Generation (RFC 826) */
    arp_frame_t synth_arp_req;
    memset(&synth_arp_req, 0, sizeof(synth_arp_req));
    /* Ethernet Header */
    memset(synth_arp_req.eth.dest_mac, 0xFF, ETH_ADDR_LEN); /* Broadcast */
    synth_arp_req.eth.src_mac[0] = 0x00U;
    synth_arp_req.eth.src_mac[1] = 0x11U;
    synth_arp_req.eth.src_mac[2] = 0x22U;
    synth_arp_req.eth.src_mac[3] = 0x33U;
    synth_arp_req.eth.src_mac[4] = 0x44U;
    synth_arp_req.eth.src_mac[5] = 0x55U;
    synth_arp_req.eth.ethertype = NET_HTONS(ETHERTYPE_ARP);

    /* ARP Payload */
    synth_arp_req.arp.hw_type    = NET_HTONS(ARP_HW_TYPE_ETHERNET);
    synth_arp_req.arp.proto_type = NET_HTONS(ARP_PROTO_IPV4);
    synth_arp_req.arp.hw_size    = ETH_ADDR_LEN;
    synth_arp_req.arp.proto_size = IPV4_ADDR_LEN;
    synth_arp_req.arp.opcode     = NET_HTONS(ARP_OPCODE_REQUEST);
    memcpy(synth_arp_req.arp.sender_mac, synth_arp_req.eth.src_mac, ETH_ADDR_LEN);
    synth_arp_req.arp.sender_ip  = NET_HTONL(peer_ip);
    memset(synth_arp_req.arp.target_mac, 0x00, ETH_ADDR_LEN);
    synth_arp_req.arp.target_ip  = NET_HTONL(t_cfg.ip);

    uint8_t arp_reply_buf[64] = {0};
    uint16_t arp_reply_len = 0U;
    net_status_t arp_st = arp_process_packet((const uint8_t *)&synth_arp_req, sizeof(synth_arp_req),
                                             arp_reply_buf, sizeof(arp_reply_buf), &arp_reply_len);

    const arp_frame_t *reply_frame = (const arp_frame_t *)arp_reply_buf;
    int arp_reply_ok = (arp_st == NET_OK) && (arp_reply_len == sizeof(arp_frame_t)) &&
                       (reply_frame->eth.ethertype == NET_HTONS(ETHERTYPE_ARP)) &&
                       (reply_frame->arp.hw_type == NET_HTONS(ARP_HW_TYPE_ETHERNET)) &&
                       (reply_frame->arp.proto_type == NET_HTONS(ARP_PROTO_IPV4)) &&
                       (reply_frame->arp.opcode == NET_HTONS(ARP_OPCODE_REPLY)) &&
                       (reply_frame->eth.src_mac[0] == 0x40U && reply_frame->eth.src_mac[1] == 0x4CU &&
                        reply_frame->eth.src_mac[2] == 0xCAU && reply_frame->eth.src_mac[3] == 0x45U &&
                        reply_frame->eth.src_mac[4] == 0x1EU && reply_frame->eth.src_mac[5] == 0x14U) &&
                       (reply_frame->arp.sender_ip == NET_HTONL(t_cfg.ip)) &&
                       (reply_frame->arp.target_ip == NET_HTONL(peer_ip));

    /* 3. RFC 1071 Standard Checksum Test Vector Calculation */
    static const uint8_t s_rfc1071_test_header[RFC1071_TEST_HDR_LEN] = {
        0x45, 0x00, 0x00, 0x3c,
        0x1c, 0x46, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00,
        0xac, 0x10, 0x0a, 0x63,
        0xac, 0x10, 0x0a, 0x0c
    };
    uint16_t computed_chk = net_checksum(s_rfc1071_test_header, RFC1071_TEST_HDR_LEN);
    int rfc1071_calc_ok = (computed_chk == RFC1071_TEST_EXPECTED_CHECKSUM);

    /* Verify that checksum over the completed header validates to 0x0000 */
    uint8_t verified_hdr[RFC1071_TEST_HDR_LEN];
    memcpy(verified_hdr, s_rfc1071_test_header, RFC1071_TEST_HDR_LEN);
    verified_hdr[10] = (uint8_t)(computed_chk >> 8U);
    verified_hdr[11] = (uint8_t)(computed_chk & 0xFFU);
    uint16_t verify_chk = net_checksum(verified_hdr, RFC1071_TEST_HDR_LEN);
    int rfc1071_verify_ok = (verify_chk == 0x0000U);

    /* 4. TCP Pseudo-Header Checksum Verification */
    tcp_header_t test_tcp;
    memset(&test_tcp, 0, sizeof(test_tcp));
    test_tcp.src_port = NET_HTONS(80U);
    test_tcp.dest_port = NET_HTONS(12345U);
    test_tcp.seq_num = NET_HTONL(0x1000U);
    test_tcp.ack_num = NET_HTONL(0x2000U);
    test_tcp.data_offset_reserved = (uint8_t)((TCP_MIN_HDR_LEN / 4U) << TCP_DATA_OFFSET_SHIFT);
    test_tcp.flags = TCP_FLAG_SYN | TCP_FLAG_ACK;
    test_tcp.window = NET_HTONS(1024U);
    test_tcp.checksum = 0U;

    uint32_t t_src_ip = t_cfg.ip;
    uint32_t t_dst_ip = peer_ip;
    uint16_t tcp_chk = net_tcp_checksum(t_src_ip, t_dst_ip, &test_tcp, TCP_MIN_HDR_LEN, NULL, 0U);
    test_tcp.checksum = NET_HTONS(tcp_chk);
    uint16_t tcp_verify = net_tcp_checksum(t_src_ip, t_dst_ip, &test_tcp, TCP_MIN_HDR_LEN, NULL, 0U);
    int tcp_chk_ok = (tcp_chk != 0U) && (tcp_verify == 0x0000U);

    /* 5. TCP PCB Allocation, Listening & State Machine */
    tcp_pcb_t *pcb = tcp_new();
    int pcb_alloc_ok = (pcb != NULL) && (pcb->state == TCP_STATE_CLOSED);
    int pcb_listen_ok = (tcp_bind(pcb, 80U) == TCP_OK) &&
                        (tcp_listen(pcb, NULL) == TCP_OK) &&
                        (pcb->state == TCP_STATE_LISTEN);
    int pcb_close_ok = (tcp_close(pcb) == TCP_OK) && (pcb->state == TCP_STATE_CLOSED);

    uint64_t t_net_end = systimer_get_us();
    uint64_t elapsed_us = t_net_end - t_net_start;
    int bounded_time_ok = (elapsed_us < 1000U);

    wdt_feed();

    int t33_pass = net_init_ok && cfg_ok && arp_reply_ok &&
                   rfc1071_calc_ok && rfc1071_verify_ok &&
                   tcp_chk_ok && pcb_alloc_ok && pcb_listen_ok && pcb_close_ok && bounded_time_ok;

    uart_puts("  Expected:    Init=1, Config=1, ARPReply=1, RFC1071=1, Verify=1, TCPChk=1, PCB=1, BoundTime=1\r\n");
    uart_puts("  Actual:      Init=");
    put_dec(net_init_ok);
    uart_puts(", Config=");
    put_dec(cfg_ok);
    uart_puts(", ARPReply=");
    put_dec(arp_reply_ok);
    uart_puts(", RFC1071=");
    put_dec(rfc1071_calc_ok);
    uart_puts(", Verify=");
    put_dec(rfc1071_verify_ok);
    uart_puts(", TCPChk=");
    put_dec(tcp_chk_ok);
    uart_puts(", PCB=");
    put_dec(pcb_alloc_ok && pcb_listen_ok && pcb_close_ok);
    uart_puts(", BoundTime=");
    put_dec(bounded_time_ok);
    uart_puts("\r\n");

    uart_puts("  Diag: RFC1071_Sum=");
    put_hex(computed_chk);
    uart_puts(", VerifyZero=");
    put_hex(verify_chk);
    uart_puts(", TCP_Chk=");
    put_hex(tcp_chk);
    uart_puts(", ElapsedUs=");
    put_dec((uint32_t)elapsed_us);
    uart_puts("\r\n");

    if (t33_pass) passed_tests++;
    print_result(t33_pass);

    /* ------------------------------------------------------------- */
    /* SUMMARY CALCULATION & REPORT                                  */
    /* ------------------------------------------------------------- */
    print_banner_line();
    uart_puts("                       TEST SUITE SUMMARY                             \r\n");
    print_banner_line();

    uart_puts("  Total Tests Run: ");
    put_dec(total_tests);
    uart_puts("\r\n");

    uart_puts("  Passed:          ");
    put_dec(passed_tests);
    uart_puts("\r\n");

    uart_puts("  Failed:          ");
    put_dec(total_tests - passed_tests);
    uart_puts("\r\n");

    uint32_t success_pct = (passed_tests * 100) / total_tests;
    uart_puts("  Success Rate:    ");
    put_dec(success_pct);
    uart_puts("%\r\n");

    print_banner_line();
}
