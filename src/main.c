#include <stdint.h>
#include "io_constants.h"
#include "utils.h"
#include "string.h"
#include "test.h"
#include "clock.h"

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
    else
    {
        uart_puts("Unknown command. Type 'help' for available commands.\r\n");
    }
}

void main(void)
{
    char input_buffer[MAX_CMD_LEN];

    /* Initialize PCR clock tree to 160 MHz CPU PLL and 80 MHz APB */
    clock_init();

    uart_puts("\r\n");
    print_info();
    disable_wdt();

    uart_puts("Ready. Type 'do-test' for validation suite or 'help' for command list.\r\n");

    while (1)
    {
        shell(input_buffer);
    }
}
