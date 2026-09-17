#include "utils.h"
#include "io_constants.h"
#include "wdt.h"
#include "dpc.h"

void read_line(char *buffer, int max_len)
{
    if (!buffer || max_len <= 0) return;

    while (!console_read_line_nonblocking(buffer, (size_t)max_len))
    {
        wdt_supervisor_tick();
        dpc_process_all();
    }
}

static char nibble_to_hex(uint8_t n)
{
    n &= 0x0F;
    return (n < 10) ? (char)('0' + n) : (char)('A' + (n - 10));
}

void put_hex(uint32_t val)
{
    console_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
    {
        console_putc(nibble_to_hex((uint8_t)(val >> i)));
    }
}

void put_dec(uint32_t val)
{
    char buf[12];
    int idx = 0;

    if (val == 0)
    {
        console_putc('0');
        return;
    }

    while (val > 0)
    {
        buf[idx++] = (char)('0' + (val % 10));
        val /= 10;
    }

    for (int i = idx - 1; i >= 0; i--)
    {
        console_putc(buf[i]);
    }
}

int mini_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    if (buf == NULL || size == 0U)
    {
        return 0;
    }
    if (fmt == NULL)
    {
        buf[0] = '\0';
        return 0;
    }

    size_t written = 0U;
    size_t max_chars = size - 1U;

    while (*fmt != '\0')
    {
        if (*fmt == '%')
        {
            fmt++;
            /* Optional zero padding width */
            int pad_zero = 0;
            int width = 0;
            if (*fmt == '0')
            {
                pad_zero = 1;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9')
            {
                width = (width * 10) + (*fmt - '0');
                fmt++;
            }

            if (*fmt == 's')
            {
                const char *s = va_arg(ap, const char *);
                if (s == NULL) s = "(null)";
                while (*s != '\0' && written < max_chars)
                {
                    buf[written++] = *s++;
                }
            }
            else if (*fmt == 'd' || *fmt == 'i')
            {
                int val = va_arg(ap, int);
                if (val < 0)
                {
                    if (written < max_chars) buf[written++] = '-';
                    val = -val;
                }
                char num_buf[16];
                int n_idx = 0;
                if (val == 0) num_buf[n_idx++] = '0';
                while (val > 0)
                {
                    num_buf[n_idx++] = (char)('0' + (val % 10));
                    val /= 10;
                }
                while (width > n_idx && written < max_chars)
                {
                    buf[written++] = pad_zero ? '0' : ' ';
                    width--;
                }
                for (int i = n_idx - 1; i >= 0 && written < max_chars; i--)
                {
                    buf[written++] = num_buf[i];
                }
            }
            else if (*fmt == 'u')
            {
                uint32_t val = va_arg(ap, uint32_t);
                char num_buf[16];
                int n_idx = 0;
                if (val == 0U) num_buf[n_idx++] = '0';
                while (val > 0U)
                {
                    num_buf[n_idx++] = (char)('0' + (val % 10U));
                    val /= 10U;
                }
                while (width > n_idx && written < max_chars)
                {
                    buf[written++] = pad_zero ? '0' : ' ';
                    width--;
                }
                for (int i = n_idx - 1; i >= 0 && written < max_chars; i--)
                {
                    buf[written++] = num_buf[i];
                }
            }
            else if (*fmt == 'x' || *fmt == 'X' || *fmt == 'p')
            {
                uint32_t val = va_arg(ap, uint32_t);
                char hex_digits[] = "0123456789abcdef";
                char hex_buf[16];
                int h_idx = 0;
                if (val == 0U) hex_buf[h_idx++] = '0';
                while (val > 0U)
                {
                    hex_buf[h_idx++] = hex_digits[val & 0x0FU];
                    val >>= 4U;
                }
                while (width > h_idx && written < max_chars)
                {
                    buf[written++] = pad_zero ? '0' : ' ';
                    width--;
                }
                for (int i = h_idx - 1; i >= 0 && written < max_chars; i--)
                {
                    buf[written++] = hex_buf[i];
                }
            }
            else if (*fmt == 'c')
            {
                int c = va_arg(ap, int);
                if (written < max_chars) buf[written++] = (char)c;
            }
            else if (*fmt == '%')
            {
                if (written < max_chars) buf[written++] = '%';
            }
            else
            {
                if (written < max_chars) buf[written++] = '%';
                if (*fmt != '\0' && written < max_chars) buf[written++] = *fmt;
            }
        }
        else
        {
            if (written < max_chars) buf[written++] = *fmt;
        }
        if (*fmt != '\0')
        {
            fmt++;
        }
    }

    buf[written] = '\0';
    return (int)written;
}

int mini_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int res = mini_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return res;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int res = mini_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return res;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* Safe large upper bound for freestanding sprintf */
    int res = mini_vsnprintf(buf, 4096U, fmt, ap);
    va_end(ap);
    return res;
}

int puts(const char *s)
{
    if (s != NULL)
    {
        console_puts(s);
        console_puts("\r\n");
    }
    return 0;
}

int putchar(int c)
{
    console_putc((char)c);
    return c;
}


