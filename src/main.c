#include <stdint.h>
#include "io_constants.h"
#include "utils.h"
#include "string.h"
#include "test.h"
#include "clock.h"
#include "wdt.h"
#include "trap.h"
#include "interrupt.h"
#include "dpc.h"
#include "usb_serial.h"
#include "uart.h"
#include "console.h"

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
    console_puts("Iron V Shell Commands:\r\n");
    console_puts("  help                - Show available commands\r\n");
    console_puts("  info                - Show system information\r\n");
    console_puts("  peek <hex_addr>     - Read 32-bit word from hex address\r\n");
    console_puts("  poke <addr> <val>   - Write 32-bit hex value to address\r\n");
    console_puts("  ecall               - Trigger controlled M-mode software trap (ECALL)\r\n");
    console_puts("  panic               - Trigger illegal instruction exception to test panic dump\r\n");
    console_puts("  do-test             - Run full baseline validation test suite\r\n");
}

static void print_info(void)
{
    clock_config_t clk;
    clock_get_config(&clk);

    console_puts("========================================\r\n");
    console_puts(" Iron V Bare-Metal RISC-V Runtime\r\n");
    console_puts(" Target:  ESP32-C6 (RV32IMAC)\r\n");
    console_puts(" Mode:    Bare Metal / No ESP-IDF\r\n");
    console_puts(" CPU:     ");
    put_dec(clk.cpu_mhz);
    console_puts(" MHz (PLL 480M)\r\n");
    console_puts(" APB:     ");
    put_dec(clk.apb_mhz);
    console_puts(" MHz\r\n");
    console_puts(" Memory:  HP SRAM 512KB (Harvard Split)\r\n");
    console_puts(" Flash:   8 MB SPI NOR Flash (DIO @ 80M)\r\n");

    wdt_supervisor_t wdt;
    wdt_get_status(&wdt);
    console_puts(" WDT:     ");
    if (wdt.active)
    {
        console_puts("Active (");
        put_dec(wdt.feed_interval_ms);
        console_puts(" ms timeout, 1s epoch window)\r\n");
        console_puts(" Uptime:  ");
        put_dec(wdt.epoch_count);
        console_puts(" s (epoch feeds: ");
        put_dec(wdt.feed_count);
        console_puts(")\r\n");
    }
    else
    {
        console_puts("Disabled\r\n");
    }

    soc_reset_cause_t rst_cause = wdt_get_reset_cause();
    console_puts(" Reset:   ");
    console_puts(wdt_get_reset_cause_desc(rst_cause));
    console_puts(" [");
    put_hex(rst_cause);
    console_puts("]\r\n");

    dpc_queue_t dpc_stat;
    dpc_get_stats(&dpc_stat);
    console_puts(" DPC:     Active (Pending: ");
    put_dec(dpc_get_size());
    console_puts("/");
    put_dec(DPC_QUEUE_CAPACITY);
    console_puts(", Processed: ");
    put_dec(dpc_get_processed_count());
    console_puts(", Drops: ");
    put_dec(dpc_stat.drop_count);
    console_puts(")\r\n");

    console_puts(" USB:     CDC-ACM (EP1 TX Ready: ");
    put_dec(usb_serial_is_tx_ready());
    console_puts(", RX Avail: ");
    put_dec(usb_serial_is_rx_ready());
    console_puts(")\r\n");

    console_manager_t cmgr;
    console_get_manager(&cmgr);
    console_puts(" Console: Dual Multiplexed (UART0: ");
    console_puts((cmgr.active_mask & CONSOLE_MASK_UART0) ? "Active" : "Off");
    console_puts(", USB: ");
    console_puts((cmgr.active_mask & CONSOLE_MASK_USB) ? "Active" : "Off");
    console_puts(", Echo: ");
    console_puts(cmgr.echo_enabled ? "ON" : "OFF");
    console_puts(")\r\n");
    console_puts("========================================\r\n");
}

static void shell_execute(char *input_buffer)
{
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
                console_puts("ERROR: Address ");
                put_hex(addr);
                console_puts(" is out of bounds or not 4-byte aligned. Read rejected.\r\n");
            }
            else
            {
                uint32_t val = *(volatile uint32_t *)addr;
                console_puts("[");
                put_hex(addr);
                console_puts("] = ");
                put_hex(val);
                if (access == MEM_ACCESS_READONLY)
                {
                    console_puts(" (READ-ONLY)");
                }
                else if (access == MEM_ACCESS_MMIO)
                {
                    console_puts(" (MMIO)");
                }
                console_puts("\r\n");
            }
        }
        else
        {
            console_puts("Usage: peek <hex_address>\r\n");
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
                console_puts("ERROR: Address ");
                put_hex(addr);
                console_puts(" is out of bounds or not 4-byte aligned. Write rejected.\r\n");
            }
            else if (access == MEM_ACCESS_READONLY)
            {
                console_puts("ERROR: Address ");
                put_hex(addr);
                console_puts(" is in READ-ONLY memory. Write prohibited to prevent crash/corruption.\r\n");
            }
            else
            {
                *(volatile uint32_t *)addr = val;
                FENCE();
                console_puts("Written [");
                put_hex(addr);
                console_puts("] = ");
                put_hex(val);
                console_puts("\r\n");
            }
        }
        else
        {
            console_puts("Usage: poke <hex_address> <hex_value>\r\n");
        }
    }
    else if (strcmp(input_buffer, "ecall") == 0)
    {
        console_puts("Executing controlled M-mode software trap (ECALL)...\r\n");
        asm volatile("ecall");
        /* Re-arm mstatus.MPP to Machine Mode per bare-metal convention */
        asm volatile("csrs mstatus, %0" :: "r"(MSTATUS_MPP_MACHINE_MODE) : "memory");
        console_puts("Successfully resumed from ECALL trap! Total ECALLs: ");
        put_dec(trap_get_ecall_count());
        console_puts("\r\n");
    }
    else if (strcmp(input_buffer, "panic") == 0)
    {
        console_puts("Triggering illegal instruction (0x00000000) to demonstrate panic dump...\r\n");
        asm volatile(".word 0x00000000");
    }
    else
    {
        console_puts("Unknown command. Type 'help' for available commands.\r\n");
    }
}

void shell_tick(void)
{
    char input_buffer[MAX_CMD_LEN];
    if (console_read_line_nonblocking(input_buffer, MAX_CMD_LEN))
    {
        shell_execute(input_buffer);
        console_puts("iron_v> ");
    }
}

void shell(char *input_buffer)
{
    console_puts("iron_v> ");
    read_line(input_buffer, MAX_CMD_LEN);
    shell_execute(input_buffer);
}

void main(void)
{
    /* Initialize PCR clock tree to 160 MHz CPU PLL and 40 MHz APB */
    clock_init();

    /* Initialize active multi-tier watchdog supervisor */
    wdt_init(WDT_DEFAULT_TIMEOUT_MS);

    /* Initialize RISC-V machine-mode trap vector table and handler */
    trap_init();

    /* Initialize Interrupt Matrix (INTMTX) and Core Interrupt Controller (PLIC_MX) */
    interrupt_init();

    /* Initialize Lock-Free SPSC DPC Queue Engine */
    dpc_init();

    /* Initialize Unified Dual-Console Layer (UART0 interrupt-driven & USB CDC-ACM) */
    console_init();

    console_puts("\r\n");
    print_info();

    console_puts("Ready. Type 'do-test' for validation suite or 'help' for command list.\r\n");
    console_puts("iron_v> ");

    while (1)
    {
        wdt_supervisor_tick();
        dpc_process_all();
        shell_tick();
    }
}
