#include "io_constants.h"
#include "utils.h"
#include "string.h"
#include "uart.h"

/* Disables RISC-V watchdogs */
void disable_wdt(void)
{
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    *TIMG0_WDTCONFIG0 = 0;
    *TIMG0_WDTWPROTECT = 0;
    uart_puts("Disabled T0WDT.\r\n\n");

    *TIMG1_WDTWPROTECT = TIMG_WDT_WKEY;
    *TIMG1_WDTCONFIG0 = 0;
    *TIMG1_WDTWPROTECT = 0;
    uart_puts("Disabled T1WDT.\r\n\n");

    *RTC_WDT_WPROTECT_REG = TIMG_WDT_WKEY;
    *RTC_WDT_CONFIG0_REG = 0;
    *RTC_WDT_WPROTECT_REG = 0;
    uart_puts("Disabled RWDT.\r\n");

    *RTC_WDT_SWD_WPROTECT_REG = TIMG_WDT_WKEY;
    *RTC_WDT_SWD_CONFIG_REG |= (1u << 30);
    *RTC_WDT_SWD_WPROTECT_REG = 0;
    uart_puts("Disabled SWD\r\n");
}

/**
 * The Parser:
 * Takes the assembled command from the input_buffer and
 * executes the logic.
 */
void shell(char *input_buffer)
{
    uart_puts("> ");
    read_line(input_buffer, MAX_CMD_LEN); // Assembler: Blocks until Enter

    if (input_buffer[0] == '\0') return;

    // Peek Command
    if (strcmp(input_buffer, "peek ") == 0) // Basic startswith check
    {
        uint32_t addr = 0;
        char *next_arg = input_buffer + 5;
        if (s_htoi(&next_arg, &addr))
        {
            uart_puts("Value: 0x");
            put_hex(*(volatile uint32_t *)addr);
            uart_puts("\r\n");
        }
        else
        {
            uart_puts("ERROR: Invalid address.\r\n");
        }
    }
    // Poke Command
    else if (input_buffer[0] == 'p' && input_buffer[1] == 'o' && input_buffer[2] == 'k' && input_buffer[3] == 'e')
    {
        uint32_t addr, val;
        char *next_arg = input_buffer + 4;
        if (s_htoi(&next_arg, &addr) && s_htoi(&next_arg, &val))
        {
            *(volatile uint32_t *)addr = val;
            uart_puts("Written.\r\n");
        }
        else
        {
            uart_puts("Error: Invalid poke arguments.\r\n");
        }
    }
    else
    {
        uart_puts("Unknown command.\r\n");
    }
}

void main(void)
{
    char input_buffer[MAX_CMD_LEN];
    
    uart_puts("Booting Iron V...\r\n");
    disable_wdt();

    // TODO: Initialize UART interrupts and Global Interrupts here

    while(1) {
        shell(input_buffer);
    }
}
