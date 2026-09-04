#include "test.h"
#include "utils.h"
#include "string.h"
#include "io_constants.h"
#include "clock.h"
#include "wdt.h"
#include "trap.h"
#include "interrupt.h"

/* Designated static test variables */
static volatile uint32_t g_test_data_var = 0x12345678U; // Placed in .data
static volatile uint32_t g_test_bss_var;               // Placed in .bss (should be 0)
static const char g_test_rodata_str[] = "IRON_V_RODATA_TEST_PATTERN"; // Placed in .rodata
static volatile uint32_t g_test_isr_hit = 0;

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
    if (addr >= 0x50000000U && addr < 0x50004000U)
    {
        return MEM_ACCESS_READWRITE;
    }

    /* 5. Memory-Mapped I/O Peripheral Space (0x60000000 - 0x600D0000) */
    if (addr >= 0x60000000U && addr < 0x600D0000U)
    {
        return MEM_ACCESS_MMIO;
    }

    /* 6. Internal ROM (0x40000000 - 0x40050000) */
    if (addr >= 0x40000000U && addr < 0x40050000U)
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

    uart_puts("  Expected:    UART & TIMG in MMIO region (3), non-faulting read\r\n");
    uart_puts("  Actual:      UART_STATUS=");
    put_hex(uart_status);
    uart_puts(" (perm=");
    put_dec((uint32_t)uart_perm);
    uart_puts("), TIMG_WDTCONFIG0=");
    put_hex(timg_cfg);
    uart_puts(" (perm=");
    put_dec((uint32_t)timg_perm);
    uart_puts(")\r\n");

    int t9_pass = (uart_perm == MEM_ACCESS_MMIO) &&
                  (timg_perm == MEM_ACCESS_MMIO);
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

    uart_puts("  Expected:    SP aligned (16B), within DRAM bounds, M-Mode CSRs valid, MIE=0\r\n");
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

    int mtvec_valid = ((mtvec_val & 0x1) == 1) && ((mtvec_val & 0xFEU) == 0);
    int mpp_valid = (mpp == 3 || mpp == 0);
    int t10_pass = sp_aligned && sp_in_bounds && mpp_valid && (mie_val == 0) && mtvec_valid;
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
    uart_puts("\r\n");

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
    /* Re-arm mstatus.MPP to Machine Mode (0x1800) per bare-metal convention */
    asm volatile("csrs mstatus, %0" :: "r"(0x1800) : "memory");
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
    print_test_header(16, "INTMTX Routing & INTPRI Priority / Threshold Preempt",
                      "Verify UART0 routing to CPU channel 4, priority 10, and live SW interrupt preemption");

    /* 1. Test UART0 routing and priority configuration per roadmap T16 specification */
    int route_res = interrupt_route(INT_SRC_UART0, 4);
    uint32_t uart0_map = interrupt_get_map(INT_SRC_UART0);
    int pri_res = interrupt_set_priority(4, 10);
    uint32_t pri_val = interrupt_get_priority(4);

    int part1_pass = (route_res == 0) && (uart0_map == 4) && (pri_res == 0) && (pri_val == 10);

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
    interrupt_set_priority(4, 0);

    uart_puts("  Expected:    UART0_MAP=4, PRI_4=10, LiveDispatch=1, ThreshMask=1\r\n");
    uart_puts("  Actual:      UART0_MAP=");
    put_dec(uart0_map);
    uart_puts(", PRI_4=");
    put_dec(pri_val);
    uart_puts(", LiveDispatch=");
    put_dec(live_dispatch_pass);
    uart_puts(", ThreshMask=");
    put_dec(threshold_mask_pass);
    uart_puts("\r\n");

    int t16_pass = part1_pass && live_dispatch_pass && threshold_mask_pass;
    if (t16_pass) passed_tests++;
    print_result(t16_pass);

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
