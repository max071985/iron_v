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

/* Route all test output to unified dual-console multiplexer */
#define uart_puts console_puts

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
    if (addr & 0x3)
    {
        return MEM_ACCESS_INVALID;
    }

    /* 2. Read-Only Code and Rodata range in HP SRAM */
    if (addr >= (uint32_t)_stext && addr < (uint32_t)_erodata)
    {
        return MEM_ACCESS_READONLY;
    }

    /* 3. Read-Write Data, BSS, Heap, and Stack range in HP SRAM */
    if (addr >= (uint32_t)_sdata && addr < (uint32_t)_stack_top)
    {
        return MEM_ACCESS_READWRITE;
    }

    /* 4. LP SRAM (16 KB @ 0x50000000) */
    if (addr >= LP_SRAM_START_ADDR && addr < LP_SRAM_END_ADDR)
    {
        return MEM_ACCESS_READWRITE;
    }

    /* 5. Memory-Mapped I/O Peripheral Space (0x60000000 - 0x600D0000) */
    if (addr >= PERIPHERAL_MMIO_START_ADDR && addr < PERIPHERAL_MMIO_END_ADDR)
    {
        return MEM_ACCESS_MMIO;
    }

    /* 6. Core-Local Interrupt & Timer Subsystem Space (PLIC/CLINT: 0x20000000 - 0x20002000) */
    if (addr >= CORE_LOCAL_PERI_START_ADDR && addr < CORE_LOCAL_PERI_END_ADDR)
    {
        return MEM_ACCESS_MMIO;
    }

    /* 7. Internal ROM (0x40000000 - 0x40050000) */
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

    int sp_aligned = ((current_sp & 0xFU) == 0);
    int sp_in_bounds = (current_sp > STACK_LIMIT_ADDR) && (current_sp <= STACK_TOP_ADDR);
    uint32_t stack_margin = GET_STACK_MARGIN(current_sp);

    uint32_t mstatus_val = 0;
    asm volatile("csrr %0, mstatus" : "=r"(mstatus_val));
    uint32_t mpp = (mstatus_val >> 11) & 0x3U;

    uint32_t mie_val = 0;
    asm volatile("csrr %0, mie" : "=r"(mie_val));

    uint32_t mtvec_val = 0;
    asm volatile("csrr %0, mtvec" : "=r"(mtvec_val));

    int mtvec_valid = ((mtvec_val & 0x1) == 1) && ((mtvec_val & 0xFEU) == 0);
    int mpp_valid = (mpp == 3 || mpp == 0);
    int mie_valid = (mie_val == 0 || (mie_val & (1U << UART0_CPU_INTR_CHANNEL)) != 0);

    uart_puts("  Expected:    SP aligned (16B), within DRAM bounds, M-Mode CSRs valid\r\n");
    uart_puts("  Actual:      SP=");
    put_hex(current_sp);
    uart_puts(" (aligned=");
    put_dec(sp_aligned);
    uart_puts(", margin=");
    put_dec(stack_margin);
    uart_puts(" B), MPP=");
    put_dec(mpp);
    uart_puts(" (");
    if (mpp == 3) uart_puts("M-Mode Initial");
    else if (mpp == 0) uart_puts("M-Mode Post-MRET");
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
    int mtvec_mode_vectored = (t14_mtvec & 0x1U) == 1U;
    int mtvec_aligned_256 = (t14_mtvec & 0xFEU) == 0U;
    int mtvec_base_matches = (t14_mtvec & ~0xFFU) == (expected_base & ~0xFFU);

    uart_puts("  Expected:    mtvec.MODE=1 (Vectored), BASE=");
    put_hex(expected_base & ~0xFFU);
    uart_puts(", align256=1\r\n");
    uart_puts("  Actual:      mtvec=");
    put_hex(t14_mtvec);
    uart_puts(" (MODE=");
    put_dec(t14_mtvec & 0x3U);
    uart_puts(", BASE=");
    put_hex(t14_mtvec & ~0xFFU);
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
    /* Re-arm mstatus.MPP to Machine Mode per bare-metal convention */
    asm volatile("csrs mstatus, %0" :: "r"(MSTATUS_MPP_MACHINE_MODE) : "memory");
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

    /* 6. Verify non-blocking timeout protection without CPU stall */
    int tx_res = usb_serial_putc_blocking('X');
    int timeout_guard_pass = (tx_res == USB_SERIAL_OK || tx_res == USB_SERIAL_ERR_TIMEOUT);
    if (tx_res == USB_SERIAL_OK)
    {
        usb_serial_flush();
    }

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
