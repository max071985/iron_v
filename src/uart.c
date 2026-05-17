#include "uart.h"
#include "io_constants.h"

// Actual memory allocation
volatile char rx_ring_buffer[RX_BUFF_SIZE];
volatile int rx_head = 0;
volatile int rx_tail = 0;

/**
 * The ISR (Background):
 * Wakes up on UART interrupt, reads from hardware FIFO, 
 * and puts raw bytes into the ring buffer.
 */
void uart_rx_isr(void) {
    // Check if there is data in the hardware RX FIFO
    while ((*UART0_STATUS_REG & UART_RX_FIFO_CNT) > 0)
    {
        char c = *UART0_FIFO; // Read from hardware

        // Put into software ring buffer
        int next = (rx_head + 1) % RX_BUFF_SIZE;
        if (next != rx_tail) { // Check for overflow
            rx_ring_buffer[rx_head] = c;
            rx_head = next;
        }
    }
    
    // Note: Clearing the hardware interrupt flag usually happens here.
    // Depends on specific register (e.g., UART_INT_CLR_REG)
}

/**
 * Enable UART interrupt flags
 */
void enable_uart_intr(void) {
    // Enable interrupt bits for RXFIFO_FULL and RX_FIFO_TOUT
    *UART0_INT_ENA_REG |= UART0_INT_ENA_RXFIFO_FULL_INT_ENA_M | UART0_INT_ENA_RXFIFO_TOUT_INT_ENA_M;
    // set rxfifo threshhold to 128 bytes
//    *UART0_CONF1_REG |= (UART0_CONF1_RXFIFO_FULL_THRHD_M & 128u);
}
