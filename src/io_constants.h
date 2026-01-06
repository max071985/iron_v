#include <stdint.h>

// UART0 constants
#define UART0_BASE          0x60000000
#define UART0_FIFO          ((volatile uint32_t *)(UART0_BASE + 0x0000))
#define UART0_CLKDIV        ((volatile uint32_t *)(UART0_BASE + 0x0014))
#define UART0_STATUS_REG    ((volatile uint32_t *)(UART0_BASE + 0x001C))

#define UART_TX_FIFO_CNT    0x00FF0000
#define UART_TX_FIFO_CNT_SHIFT 16
#define UART_FIFO_SIZE      128
#define UART_FIFO_HEADROOM  8
#define UART_FIFO_THRESHOLD (UART_FIFO_SIZE - UART_FIFO_HEADROOM)

#define MCU_SEL             1   // 0 = GPIO, 1 = UART

// GPIO constants
#define IO_MUX_BASE         0x60090000
#define IO_MUX_GPIO16_REG   ((volatile uint32_t *)(IO_MUX_BASE + 0x44))
#define IO_MUX_GPIO17_REG   ((volatile uint32_t *)(IO_MUX_BASE + 0x48))

// Watchdog constants
#define TIMG0_BASE          0x60008000
#define TIMG0_WDTCONFIG0    ((volatile uint32_t *)(TIMG0_BASE + 0x48))
#define TIMG0_WDTFEED       ((volatile uint32_t *)(TIMG0_BASE + 0x60))
#define TIMG0_WDTWPROTECT   ((volatile uint32_t *)(TIMG0_BASE + 0x64))
#define TIMG_WDT_WKEY       0x50D83AA1

#define TIMG1_BASE          0x60009000
#define TIMG1_WDTCONFIG0    ((volatile uint32_t *)(TIMG1_BASE + 0x48))
#define TIMG1_WDTFEED       ((volatile uint32_t *)(TIMG1_BASE + 0x60))
#define TIMG1_WDTWPROTECT   ((volatile uint32_t *)(TIMG1_BASE + 0x64))

#define RTC_WDT_BASE                0x600B1C00
#define RTC_WDT_CONFIG0_REG         ((volatile uint32_t *)(RTC_WDT_BASE + 0x0000))
#define RTC_WDT_WPROTECT_REG        ((volatile uint32_t *)(RTC_WDT_BASE + 0x0018))
#define RTC_WDT_SWD_WPROTECT_REG    ((volatile uint32_t *)(RTC_WDT_BASE + 0x0020))
#define RTC_WDT_SWD_CONFIG_REG      ((volatile uint32_t *)(RTC_WDT_BASE + 0x001C))