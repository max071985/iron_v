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
