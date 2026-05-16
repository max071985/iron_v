#ifndef UART_H
#define UART_H

#include <stdint.h>
#include "io_constants.h"

#define RX_BUFF_SIZE 256

// Global shared variables (Circular Buffer)
extern volatile char rx_ring_buffer[RX_BUFF_SIZE];
extern volatile int rx_head; // Written by ISR
extern volatile int rx_tail; // Read by Fetcher (uart_getc)

// Hardware ISR
void uart_rx_isr(void);

#endif
