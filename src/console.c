/*
 * src/console.c
 *
 * Unified Dual-Console Subsystem (UART0 & USB-Serial-JTAG CDC-ACM)
 * Multiplexes input and output streams across active hardware ports.
 */

#include "console.h"
#include "uart.h"
#include "usb_serial.h"
#include "wdt.h"
#include "dpc.h"
#include "utils.h"

static void uart_backend_putc(char c)
{
    uart_putc(c);
}

static void uart_backend_puts(const char *str)
{
    uart_puts(str);
}

static int uart_backend_getc_nonblocking(char *c)
{
    return uart_getc_nonblocking(c);
}

static void uart_backend_flush(void)
{
    uart_flush();
}

static void usb_backend_putc(char c)
{
    usb_serial_putc_nonblocking(c);
}

static void usb_backend_puts(const char *str)
{
    usb_serial_puts(str);
}

static int usb_backend_getc_nonblocking(char *c)
{
    return (usb_serial_getc_nonblocking(c) == USB_SERIAL_OK) ? 1 : 0;
}

static void usb_backend_flush(void)
{
    usb_serial_flush();
}

static console_manager_t g_console_manager = {
    .uart = {
        .putc = uart_backend_putc,
        .puts = uart_backend_puts,
        .getc_nonblocking = uart_backend_getc_nonblocking,
        .flush = uart_backend_flush
    },
    .usb = {
        .putc = usb_backend_putc,
        .puts = usb_backend_puts,
        .getc_nonblocking = usb_backend_getc_nonblocking,
        .flush = usb_backend_flush
    },
    .echo_enabled = 1U,
    .active_mask = CONSOLE_MASK_UART0 | CONSOLE_MASK_USB
};

static char s_line_buf[CONSOLE_MAX_LINE_LEN];
static size_t s_line_idx = 0U;

void console_init(void)
{
    /* 1. Initialize underlying hardware peripherals */
    uart_init();
    usb_serial_init();

    /* 2. Configure default backend interfaces */
    g_console_manager.uart.putc = uart_backend_putc;
    g_console_manager.uart.puts = uart_backend_puts;
    g_console_manager.uart.getc_nonblocking = uart_backend_getc_nonblocking;
    g_console_manager.uart.flush = uart_backend_flush;

    g_console_manager.usb.putc = usb_backend_putc;
    g_console_manager.usb.puts = usb_backend_puts;
    g_console_manager.usb.getc_nonblocking = usb_backend_getc_nonblocking;
    g_console_manager.usb.flush = usb_backend_flush;

    g_console_manager.echo_enabled = 1U;
    g_console_manager.active_mask = CONSOLE_MASK_UART0 | CONSOLE_MASK_USB;
}

void console_putc(char c)
{
    if ((g_console_manager.active_mask & CONSOLE_MASK_UART0) && g_console_manager.uart.putc)
    {
        g_console_manager.uart.putc(c);
    }
    if ((g_console_manager.active_mask & CONSOLE_MASK_USB) && g_console_manager.usb.putc)
    {
        g_console_manager.usb.putc(c);
    }
}

void console_puts(const char *str)
{
    if (!str) return;

    if ((g_console_manager.active_mask & CONSOLE_MASK_UART0) && g_console_manager.uart.puts)
    {
        g_console_manager.uart.puts(str);
    }
    if ((g_console_manager.active_mask & CONSOLE_MASK_USB) && g_console_manager.usb.puts)
    {
        g_console_manager.usb.puts(str);
    }
}

int console_getc_nonblocking(char *c)
{
    if (!c) return 0;

    /* 1. Check UART0 backend if active */
    if ((g_console_manager.active_mask & CONSOLE_MASK_UART0) && g_console_manager.uart.getc_nonblocking)
    {
        if (g_console_manager.uart.getc_nonblocking(c))
        {
            return 1;
        }
    }

    /* 2. Check USB CDC-ACM backend if active */
    if ((g_console_manager.active_mask & CONSOLE_MASK_USB) && g_console_manager.usb.getc_nonblocking)
    {
        if (g_console_manager.usb.getc_nonblocking(c))
        {
            return 1;
        }
    }

    return 0;
}

char console_getc_blocking(void)
{
    char c = '\0';
    while (!console_getc_nonblocking(&c))
    {
        wdt_supervisor_tick();
        dpc_process_all();
    }
    return c;
}

void console_flush(void)
{
    if ((g_console_manager.active_mask & CONSOLE_MASK_UART0) && g_console_manager.uart.flush)
    {
        g_console_manager.uart.flush();
    }
    if ((g_console_manager.active_mask & CONSOLE_MASK_USB) && g_console_manager.usb.flush)
    {
        g_console_manager.usb.flush();
    }
}

void console_set_active_mask(uint8_t mask)
{
    g_console_manager.active_mask = mask;
}

uint8_t console_get_active_mask(void)
{
    return g_console_manager.active_mask;
}

void console_set_echo(uint8_t enable)
{
    g_console_manager.echo_enabled = enable ? 1U : 0U;
}

uint8_t console_get_echo(void)
{
    return g_console_manager.echo_enabled;
}

void console_get_manager(console_manager_t *out_mgr)
{
    if (!out_mgr) return;
    *out_mgr = g_console_manager;
}

int console_read_line_nonblocking(char *out_buffer, size_t max_len)
{
    if (!out_buffer || max_len == 0U) return 0;

    char c = '\0';
    while (console_getc_nonblocking(&c))
    {
        if (c == '\r' || c == '\n')
        {
            s_line_buf[s_line_idx] = '\0';
            if (g_console_manager.echo_enabled)
            {
                console_puts("\r\n");
            }

            size_t copy_len = (s_line_idx < max_len - 1U) ? s_line_idx : (max_len - 1U);
            for (size_t i = 0; i < copy_len; i++)
            {
                out_buffer[i] = s_line_buf[i];
            }
            out_buffer[copy_len] = '\0';

            s_line_idx = 0U;
            return 1;
        }
        else if (c == ASCII_BS || c == ASCII_DEL)
        {
            if (s_line_idx > 0U)
            {
                s_line_idx--;
                if (g_console_manager.echo_enabled)
                {
                    console_puts("\b \b");
                }
            }
        }
        else if (c >= ASCII_PRINTABLE_MIN && c <= ASCII_PRINTABLE_MAX)
        {
            if (s_line_idx < CONSOLE_MAX_LINE_LEN - 1U)
            {
                s_line_buf[s_line_idx++] = c;
                if (g_console_manager.echo_enabled)
                {
                    console_putc(c);
                }
            }
        }
    }

    return 0;
}
