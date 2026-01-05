#include <stdint.h>

#define UART0_BASE          0x60000000
#define UART0_FIFO          ((volatile uint32_t *)(UART0_BASE + 0x0000))

#define TIMG0_BASE          0x60008000
#define TIMG0_WDTCONFIG0    ((volatile uint32_t *)(TIMG0_BASE + 0x48))
#define TIMG0_WDTFEED       ((volatile uint32_t *)(TIMG0_BASE + 0x60))
#define TIMG0_WDTWPROTECT   ((volatile uint32_t *)(TIMG0_BASE + 0x64))
#define TIMG_WDT_WKEY       0x50D83AA1

#define TIMG1_BASE          0x60009000
#define TIMG1_WDTCONFIG0    ((volatile uint32_t *)(TIMG1_BASE + 0x48))
#define TIMG1_WDTFEED       ((volatile uint32_t *)(TIMG1_BASE + 0x60))
#define TIMG1_WDTWPROTECT   ((volatile uint32_t *)(TIMG1_BASE + 0x64))

#define LP_TIMER_BASE       0x600B0C00
#define LP_WDT_BASE         0x600B1C00
#define LP_WDT_CONFIG0      ((volatile uint32_t *)(LP_WDT_BASE + 0x0))

#define RTC_WDT_BASE                0x600B1C00
#define RTC_WDT_CONFIG0_REG         ((volatile uint32_t *)(RTC_WDT_BASE + 0x0000))
#define RTC_WDT_WPROTECT_REG        ((volatile uint32_t *)(RTC_WDT_BASE + 0x0018))
#define RTC_WDT_SWD_WPROTECT_REG    ((volatile uint32_t *)(RTC_WDT_BASE + 0x0020))
#define RTC_WDT_SWD_CONFIG_REG      ((volatile uint32_t *)(RTC_WDT_BASE + 0x001C))
#define RTC_WDT_SWD_AUTO_FEED_EN    (1u << 18)


void putc(char c)
{
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