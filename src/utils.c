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

/* Reads a line from the user into the buffer */
void read_line(char *buffer, int max_len)
{
    char c = 0;
    int i = 0;
    while((c = uart_getc()) != '\r')
    {

        if (c == 0)
            continue;

        // Check for backspace or DEL
        if (c == 0x08 || c == 0x7F)
        {
            if (i > 0)
            {
                i--;
                uart_puts("\b \b"); // Visually erase the last character from the CLI
            }
            continue;
        }
        if (i < max_len - 1)
        {
            buffer[i++] = c;
            uart_putc(c);           // Visually print the last input character to the CLI
        }
    }
    buffer[i] = '\0';   // Null-terminate (safe because i can only increase to 'max_len - 1')
    uart_puts("\r\n");
}