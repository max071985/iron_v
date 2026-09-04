#include <stdint.h>
#include "io_constants.h"
#include "utils.h"
#include "string.h"
#include "test.h"
#include "clock.h"
#include "wdt.h"
#include "trap.h"
#include "interrupt.h"

/*
 * Disables hardware watchdogs safely with memory barriers during early bringup.
 * Ref: ESP32-C6 TRM Ch. 13.5 (TIMG WDT) & Ch. 14.3 (RTC WDT).
 */
void disable_wdt(void)
{
    // 1. TIMG0 Watchdog
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();
    *TIMG0_WDTCONFIG0 = 0;
    FENCE();
    *TIMG0_WDTWPROTECT = 0;
    FENCE();

    // 2. TIMG1 Watchdog
    *TIMG1_WDTWPROTECT = TIMG_WDT_WKEY;
    FENCE();
    *TIMG1_WDTCONFIG0 = 0;
    FENCE();
    *TIMG1_WDTWPROTECT = 0;
    FENCE();

    // 3. RTC / LP Watchdog
    *RTC_WDT_WPROTECT_REG = TIMG_WDT_WKEY;
    FENCE();
    *RTC_WDT_CONFIG0_REG = 0;
    FENCE();
    *RTC_WDT_WPROTECT_REG = 0;
    FENCE();

    // 4. Super Watchdog (SWD)
    *RTC_WDT_SWD_WPROTECT_REG = TIMG_WDT_WKEY;
    FENCE();
    *RTC_WDT_SWD_CONFIG_REG |= (1u << 30); // Disable SWD
    FENCE();
    *RTC_WDT_SWD_WPROTECT_REG = 0;
    FENCE();

    uart_puts("[WDT] Early bringup watchdogs disabled.\r\n");
}

static void print_help(void)
{
    uart_puts("Iron V Shell Commands:\r\n");
    uart_puts("  help                - Show available commands\r\n");
    uart_puts("  info                - Show system information\r\n");
    uart_puts("  peek <hex_addr>     - Read 32-bit word from hex address\r\n");
    uart_puts("  poke <addr> <val>   - Write 32-bit hex value to address\r\n");
    uart_puts("  ecall               - Trigger controlled M-mode software trap (ECALL)\r\n");
    uart_puts("  panic               - Trigger illegal instruction exception to test panic dump\r\n");
    uart_puts("  do-test             - Run full baseline validation test suite\r\n");
}

static void print_info(void)
{
    clock_config_t clk;
    clock_get_config(&clk);

    uart_puts("========================================\r\n");
    uart_puts(" Iron V Bare-Metal RISC-V Runtime\r\n");
    uart_puts(" Target:  ESP32-C6 (RV32IMAC)\r\n");
    uart_puts(" Mode:    Bare Metal / No ESP-IDF\r\n");
    uart_puts(" CPU:     ");
    put_dec(clk.cpu_mhz);
    uart_puts(" MHz (PLL 480M)\r\n");
    uart_puts(" APB:     ");
    put_dec(clk.apb_mhz);
    uart_puts(" MHz\r\n");
    uart_puts(" Memory:  HP SRAM 512KB (Harvard Split)\r\n");
    uart_puts(" Flash:   8 MB SPI NOR Flash (DIO @ 80M)\r\n");

    wdt_supervisor_t wdt;
    wdt_get_status(&wdt);
    uart_puts(" WDT:     ");
    if (wdt.active)
    {
        uart_puts("Active (");
        put_dec(wdt.feed_interval_ms);
        uart_puts(" ms timeout)\r\n");
    }
    else
    {
        uart_puts("Disabled\r\n");
    }

    soc_reset_cause_t rst_cause = wdt_get_reset_cause();
    uart_puts(" Reset:   ");
    uart_puts(wdt_get_reset_cause_desc(rst_cause));
    uart_puts(" [");
    put_hex(rst_cause);
    uart_puts("]\r\n");
    uart_puts("========================================\r\n");
}

void shell(char *input_buffer)
{
    uart_puts("iron_v> ");
    read_line(input_buffer, MAX_CMD_LEN);

    if (input_buffer[0] == '\0') return;

    if (strcmp(input_buffer, "help") == 0)
    {
        print_help();
    }
    else if (strcmp(input_buffer, "info") == 0)
    {
        print_info();
    }
    else if (strcmp(input_buffer, "do-test") == 0 || strcmp(input_buffer, "test") == 0)
    {
        run_validation_suite();
    }
    else if (strncmp(input_buffer, "peek", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        uint32_t addr = 0;
        char *arg = input_buffer + 4;
        if (s_htoi(&arg, &addr))
        {
            mem_access_t access = check_mem_access(addr);
            if (access == MEM_ACCESS_INVALID)
            {
                uart_puts("ERROR: Address ");
                put_hex(addr);
                uart_puts(" is out of bounds or not 4-byte aligned. Read rejected.\r\n");
            }
            else
            {
                uint32_t val = *(volatile uint32_t *)addr;
                uart_puts("[");
                put_hex(addr);
                uart_puts("] = ");
                put_hex(val);
                if (access == MEM_ACCESS_READONLY)
                {
                    uart_puts(" (READ-ONLY)");
                }
                else if (access == MEM_ACCESS_MMIO)
                {
                    uart_puts(" (MMIO)");
                }
                uart_puts("\r\n");
            }
        }
        else
        {
            uart_puts("Usage: peek <hex_address>\r\n");
        }
    }
    else if (strncmp(input_buffer, "poke", 4) == 0 && (input_buffer[4] == ' ' || input_buffer[4] == '\0'))
    {
        uint32_t addr = 0, val = 0;
        char *arg = input_buffer + 4;
        if (s_htoi(&arg, &addr) && s_htoi(&arg, &val))
        {
            mem_access_t access = check_mem_access(addr);
            if (access == MEM_ACCESS_INVALID)
            {
                uart_puts("ERROR: Address ");
                put_hex(addr);
                uart_puts(" is out of bounds or not 4-byte aligned. Write rejected.\r\n");
            }
            else if (access == MEM_ACCESS_READONLY)
            {
                uart_puts("ERROR: Address ");
                put_hex(addr);
                uart_puts(" is in READ-ONLY memory. Write prohibited to prevent crash/corruption.\r\n");
            }
            else
            {
                *(volatile uint32_t *)addr = val;
                FENCE();
                uart_puts("Written [");
                put_hex(addr);
                uart_puts("] = ");
                put_hex(val);
                uart_puts("\r\n");
            }
        }
        else
        {
            uart_puts("Usage: poke <hex_address> <hex_value>\r\n");
        }
    }
    else if (strcmp(input_buffer, "ecall") == 0)
    {
        uart_puts("Executing controlled M-mode software trap (ECALL)...\r\n");
        asm volatile("ecall");
        /* Re-arm mstatus.MPP to Machine Mode per bare-metal convention */
        asm volatile("csrs mstatus, %0" :: "r"(MSTATUS_MPP_MACHINE_MODE) : "memory");
        uart_puts("Successfully resumed from ECALL trap! Total ECALLs: ");
        put_dec(trap_get_ecall_count());
        uart_puts("\r\n");
    }
    else if (strcmp(input_buffer, "panic") == 0)
    {
        uart_puts("Triggering illegal instruction (0x00000000) to demonstrate panic dump...\r\n");
        asm volatile(".word 0x00000000");
    }
    else
    {
        uart_puts("Unknown command. Type 'help' for available commands.\r\n");
    }
}

void main(void)
{
    char input_buffer[MAX_CMD_LEN];

    /* Initialize PCR clock tree to 160 MHz CPU PLL and 40 MHz APB */
    clock_init();

    /* Initialize active multi-tier watchdog supervisor */
    wdt_init(WDT_DEFAULT_TIMEOUT_MS);

    /* Initialize RISC-V machine-mode trap vector table and handler */
    trap_init();

    /* Initialize Interrupt Matrix (INTMTX) and Core Interrupt Controller (INTPRI) */
    interrupt_init();

    uart_puts("\r\n");
    print_info();

    uart_puts("Ready. Type 'do-test' for validation suite or 'help' for command list.\r\n");

    while (1)
    {
        wdt_supervisor_tick();
        shell(input_buffer);
    }
}
