#ifndef IO_CONSTANTS_H
#define IO_CONSTANTS_H

#include <stdint.h>
#include "regs/uart0.h"
#include "regs/gpio.h"
#include "regs/io_mux.h"
#include "regs/timg0.h"
#include "regs/timg1.h"
#include "regs/lp_wdt.h"

// Shell constants
#define MAX_CMD_LEN         128

// UART aliases for compatibility with existing code
#define UART0_FIFO          UART0_FIFO_REG
// UART0_STATUS_REG is already defined in regs/uart0.h

#define UART_RX_FIFO_CNT    UART0_STATUS_RXFIFO_CNT_M
#define UART_TX_FIFO_CNT    UART0_STATUS_TXFIFO_CNT_M
#define UART_TX_FIFO_CNT_SHIFT UART0_STATUS_TXFIFO_CNT_S

#define UART_FIFO_SIZE      128
#define UART_FIFO_HEADROOM  8
#define UART_FIFO_THRESHOLD (UART_FIFO_SIZE - UART_FIFO_HEADROOM)

#define MCU_SEL             1   // 0 = GPIO, 1 = UART

// TIMG aliases
#define TIMG0_WDTCONFIG0    TIMG0_WDTCONFIG0_REG
#define TIMG0_WDTFEED       TIMG0_WDTFEED_REG
#define TIMG0_WDTWPROTECT   TIMG0_WDTWPROTECT_REG
#define TIMG_WDT_WKEY       0x50D83AA1

#define TIMG1_WDTCONFIG0    TIMG1_WDTCONFIG0_REG
#define TIMG1_WDTFEED       TIMG1_WDTFEED_REG
#define TIMG1_WDTWPROTECT   TIMG1_WDTWPROTECT_REG

// RTC/LP WDT aliases
#define RTC_WDT_BASE                LP_WDT_BASE
#define RTC_WDT_CONFIG0_REG         LP_WDT_WDTCONFIG0_REG
#define RTC_WDT_WPROTECT_REG        LP_WDT_WDTWPROTECT_REG
#define RTC_WDT_SWD_WPROTECT_REG    LP_WDT_SWD_WPROTECT_REG
#define RTC_WDT_SWD_CONFIG_REG      LP_WDT_SWD_CONF_REG

#endif // IO_CONSTANTS_H
