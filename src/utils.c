#include "utils.h"
#include "io_constants.h"
#include "wdt.h"

int uart_putc(char c)
{
    volatile uint32_t timeout = UART_TIMEOUT_CYCLES;
    while (((*UART0_STATUS_REG >> UART_TX_FIFO_CNT_SHIFT) & 0xFF) > UART_FIFO_THRESHOLD)
    {
        if (--timeout == 0)
        {
            return -1; // Bounded polling timeout reached (hardware safety)
        }
    }

    *UART0_FIFO = (uint32_t)(uint8_t)c;
    FENCE();
    return 0;
}

void uart_puts(const char *str)
{
    if (!str) return;
    while (*str)
    {
        if (*str == '\n')
        {
            uart_putc('\r');
        }
        uart_putc(*str++);
    }
}

int uart_getc_nonblocking(char *c)
{
    if ((*UART0_STATUS_REG & UART_RX_FIFO_CNT) > 0)
    {
        *c = (char)(*UART0_FIFO & 0xFF);
        FENCE();
        return 1;
    }
    return 0;
}

char uart_getc_blocking(void)
{
    char c = 0;
    while (!uart_getc_nonblocking(&c))
    {
        wdt_supervisor_tick();
    }
    return c;
}

void read_line(char *buffer, int max_len)
{
    int i = 0;
    if (!buffer || max_len <= 0) return;

    while (1)
    {
        char c = uart_getc_blocking();

        // Handle Carriage Return / Newline
        if (c == '\r' || c == '\n')
        {
            buffer[i] = '\0';
            uart_puts("\r\n");
            return;
        }

        // Handle Backspace or DEL
        if (c == 0x08 || c == 0x7F)
        {
            if (i > 0)
            {
                i--;
                uart_puts("\b \b");
            }
            continue;
        }

        // Printable ASCII characters
        if (c >= 32 && c <= 126)
        {
            if (i < max_len - 1)
            {
                buffer[i++] = c;
                uart_putc(c);
            }
        }
    }
}

static char nibble_to_hex(uint8_t n)
{
    n &= 0x0F;
    return (n < 10) ? (char)('0' + n) : (char)('A' + (n - 10));
}

void put_hex(uint32_t val)
{
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
    {
        uart_putc(nibble_to_hex((uint8_t)(val >> i)));
    }
}

void put_dec(uint32_t val)
{
    char buf[12];
    int idx = 0;

    if (val == 0)
    {
        uart_putc('0');
        return;
    }

    while (val > 0)
    {
        buf[idx++] = (char)('0' + (val % 10));
        val /= 10;
    }

    for (int i = idx - 1; i >= 0; i--)
    {
        uart_putc(buf[i]);
    }
}
