#include "io_constants.h"
#include "utils.c"

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

void main(void)
{
    char *input_buffer[MAX_CMD_LEN];
    
    uart_puts("Booting...\r\n");
    uart_puts("Disabling WDT:\r\n");
    disable_wdt();

    char c;
    while(1) {
        c = get_char("Please enter a character: ");
        uart_puts("You've entered: ");
        uart_putc(c);
        uart_puts("\r\n");
        delay(2000000);
    }
}