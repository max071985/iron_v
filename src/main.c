#include "io_constants.h"

void uart_putc(char c)
{
    while (((*UART0_STATUS_REG >> UART_TX_FIFO_CNT_SHIFT) & 0xFF) > UART_FIFO_THRESHOLD);

    *UART0_FIFO = c;
}

void uart_puts(const char *str)
{
    while (*str)
    {
        uart_putc(*str++);
    }
}

/* Gets the character from UART0_FIFO */
char uart_getc(void)
{
    char c = 0;
    if ((*UART0_STATUS_REG & UART_RX_FIFO_CNT) > 0)
        c = *UART0_FIFO;
    return c;
}

/* Gets a character from the user */
char get_char(char *msg)
{
    char c, output_c = 0;
    uart_puts(msg); // Prompts using the string provided
    while((c = uart_getc()) != '\r')
    {
        if (c > 0)
            output_c = c;
        if (c != '\r' && c != '\n' && c > 0)
            uart_putc(c);
    }
    uart_puts("\r\n");
    return output_c;
}

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

void delay (volatile uint32_t count)
{
    while(count--) asm volatile("nop");
}

void feed_wdt(void)
{
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    *TIMG0_WDTFEED = 1;
    *TIMG0_WDTWPROTECT = 0;
}

void main(void)
{
    uart_puts("Disabling WDT:\r\n");
    disable_wdt();
    uart_puts("Booting...\r\n");
    char c;
    while(1) {
        c = get_char("Please enter a character: ");
        uart_puts("You've entered: ");
        uart_putc(c);
        uart_puts("\r\n");
        delay(2000000);
    }
}