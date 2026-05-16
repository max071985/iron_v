#include "io_constants.h"
#include "uart.h"

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

/**
 * The Fetcher:
 * Reads one byte out of the rx_ring_buffer.
 * If empty, it waits for an interrupt.
 */
char uart_getc(void)
{
    while (rx_head == rx_tail) {
        asm volatile("wfi"); // Wait for Interrupt
    }

    char c = rx_ring_buffer[rx_tail];
    rx_tail = (rx_tail + 1) % RX_BUFF_SIZE;
    return c;
}

/**
 * The Assembler:
 * Calls uart_getc() to build a linear command string.
 * Handles line editing (backspace) and echoes to terminal.
 */
void read_line(char *buffer, int max_len)
{
    int i = 0;
    while(1)
    {
        char c = uart_getc();

        // Handle Enter/Return (Command Complete)
        if (c == '\r' || c == '\n')
        {
            buffer[i] = '\0'; // Null-terminate the linear buffer
            uart_puts("\r\n");
            return; // Return to the Parser (shell)
        }

        // Handle Backspace or DEL
        if (c == 0x08 || c == 0x7F)
        {
            if (i > 0)
            {
                i--;
                uart_puts("\b \b"); // Visually erase character
            }
            continue;
        }

        // Standard character: add to linear buffer and echo
        if (i < max_len - 1)
        {
            buffer[i++] = c;
            uart_putc(c); 
        }
    }
}

char htoch(unsigned int a)
{
    a = a & 0xF;
    if (a < 10) return (char)(a + 48);
    return (char)(a + 55);
}

void put_hex(uint32_t val)
{
    unsigned int c = 0;
    for (int i = 32; i > 0; i -= 4)
    {
        c = (val >> (i - 4)) & 0xF;
        uart_putc(htoch(c));
    }
}
