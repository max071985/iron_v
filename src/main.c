#include "io_constants.h"
#include "utils.h"
#include "string.h"

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

/* Pseudo-delay function */
void delay(volatile uint32_t count)
{
    while(count--) asm volatile("nop");
}

void shell(char *input_buffer)
{
    uart_puts("> ");
    read_line(input_buffer, MAX_CMD_LEN); // Fill buffer with values given by the user
    // TEMP SOLUTION:
    if (input_buffer[0] == 'p' && input_buffer[1] == 'e' && input_buffer[2] == 'e' && input_buffer[3] == 'k' && input_buffer[4] == ' ')
    {
        // Read address:
        uint32_t addr = 0;
        char *next_arg = input_buffer + 5;
        int isvalid = s_htoi(&next_arg, &addr);
        if (isvalid)
        {
            uart_puts("Value: 0x");
            put_hex(*(volatile uint32_t *)addr);
            uart_puts("\r\n");
        }
        else
        {
            uart_puts("ERROR: Invalid address format.\r\n");
        }
    }
    else if (input_buffer[0] != 0)
    {
        uart_puts("Unknown command.\r\n");
    }
    /////////////////////
}

void main(void)
{
    char input_buffer[MAX_CMD_LEN];
    
    uart_puts("Booting...\r\n");
    uart_puts("Disabling WDT:\r\n");
    disable_wdt();

    while(1) {
        shell(input_buffer);
    }
}