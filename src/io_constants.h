#include <stdint.h>

// Shell constants
#define MAX_CMD_LEN         128

// UART0 constants
#define UART0_BASE          0x60000000
#define UART0_FIFO          ((volatile uint32_t *)(UART0_BASE + 0x0000))
#define UART0_CLKDIV        ((volatile uint32_t *)(UART0_BASE + 0x0014))
#define UART0_STATUS_REG    ((volatile uint32_t *)(UART0_BASE + 0x001C))

#define UART_RX_FIFO_CNT    0x000000FF
#define UART_TX_FIFO_CNT    0x00FF0000
#define UART_TX_FIFO_CNT_SHIFT 16
#define UART_FIFO_SIZE      128
#define UART_FIFO_HEADROOM  8
#define UART_FIFO_THRESHOLD (UART_FIFO_SIZE - UART_FIFO_HEADROOM)

#define MCU_SEL             1   // 0 = GPIO, 1 = UART

// IO Mux constants
#define IO_MUX_BASE         0x60090000
#define IO_MUX_GPIO16_REG   ((volatile uint32_t *)(IO_MUX_BASE + 0x44))
#define IO_MUX_GPIO17_REG   ((volatile uint32_t *)(IO_MUX_BASE + 0x48))

// GPIO constants
#define GPIO_BASE           0x60091000
#define GPIO_OUT_REG        ((volatile uint32_t *)(GPIO_BASE + 0x0004))
#define GPIO_OUT_W1TS_REG   ((volatile uint32_t *)(GPIO_BASE + 0x0008))
#define GPIO_OUT_W1TC_REG   ((volatile uint32_t *)(GPIO_BASE + 0x000C))
#define GPIO_ENABLE_REG     ((volatile uint32_t *)(GPIO_BASE + 0x0020))
#define GPIO_ENABLE_W1TS_REG     ((volatile uint32_t *)(GPIO_BASE + 0x0024))
#define GPIO_ENABLE_W1TC_REG     ((volatile uint32_t *)(GPIO_BASE + 0x0028))

// Interrupt status registers
#define GPIO_STATUS_REG     ((volatile uint32_t *)(GPIO_BASE + 0x0044))
#define GPIO_STATUS_W1TS_REG    ((volatile uint32_t *)(GPIO_BASE + 0x0048))
#define GPIO_STATUS_W1TC_REG    ((volatile uint32_t *)(GPIO_BASE + 0x004C))

// Pin configurations
#define GPIO_PIN0_REG       ((volatile uint32_t *)(GPIO_BASE + 0x0074))
#define GPIO_PIN8_REG       ((volatile uint32_t *)(GPIO_BASE + 0x0094))

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