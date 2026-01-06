#include "io_constants.h"

void putc(char c)
{
    while (((*UART0_STATUS_REG >> UART_TX_FIFO_CNT_SHIFT) & 0xFF) > UART_FIFO_THRESHOLD);

    *UART0_FIFO = c;
}

void print(const char *str)
{
    while (*str)
    {
        putc(*str++);
    }
}

void disable_wdt(void)
{
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    *TIMG0_WDTCONFIG0 = 0;
    *TIMG0_WDTWPROTECT = 0;
    print("Disabled T0WDT.\r\n\n");

    *TIMG1_WDTWPROTECT = TIMG_WDT_WKEY;
    *TIMG1_WDTCONFIG0 = 0;
    *TIMG1_WDTWPROTECT = 0;
    print("Disabled T1WDT.\r\n\n");

    *RTC_WDT_WPROTECT_REG = TIMG_WDT_WKEY;
    *RTC_WDT_CONFIG0_REG = 0;
    *RTC_WDT_WPROTECT_REG = 0;
    print("Disabled RWDT.\r\n");

    *RTC_WDT_SWD_WPROTECT_REG = TIMG_WDT_WKEY;
    *RTC_WDT_SWD_CONFIG_REG |= (1u << 30);
    *RTC_WDT_SWD_WPROTECT_REG = 0;
    print("Disabled SWD\r\n");
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
    print("Disabling WDT:\r\n");
    disable_wdt();
    print("Booting...\r\n");
    while(1) {
        print(".");
        delay(2000000);
    }
}