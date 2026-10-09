#include "utils.h"
#include "io_constants.h"
#include "modem.h"
#include "regs/plic.h"
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
            /* Optional flags */
            int left_justify = 0;
            int pad_zero = 0;
            int width = 0;
            while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '0')
            {
                if (*fmt == '-') left_justify = 1;
                else if (*fmt == '0') pad_zero = 1;
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9')
            {
                width = (width * 10) + (*fmt - '0');
                fmt++;
            }
            if (*fmt == '.')
            {
                fmt++;
                while (*fmt >= '0' && *fmt <= '9') fmt++;
            }
            while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z') fmt++;

            if (*fmt == 's')
            {
                const char *s = va_arg(ap, const char *);
                if (s == NULL) s = "(null)";
                int slen = 0;
                while (s[slen] != '\0') slen++;
                if (!left_justify)
                {
                    while (width > slen && written < max_chars)
                    {
                        buf[written++] = ' ';
                        width--;
                    }
                }
                while (*s != '\0' && written < max_chars)
                {
                    buf[written++] = *s++;
                }
                if (left_justify)
                {
                    while (width > slen && written < max_chars)
                    {
                        buf[written++] = ' ';
                        width--;
                    }
                }
            }
            else if (*fmt == 'd' || *fmt == 'i')
            {
                int val = va_arg(ap, int);
                int is_neg = (val < 0);
                if (is_neg)
                {
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
                if (is_neg) num_buf[n_idx++] = '-';
                if (!left_justify)
                {
                    while (width > n_idx && written < max_chars)
                    {
                        buf[written++] = pad_zero ? '0' : ' ';
                        width--;
                    }
                }
                for (int i = n_idx - 1; i >= 0 && written < max_chars; i--)
                {
                    buf[written++] = num_buf[i];
                }
                if (left_justify)
                {
                    while (width > n_idx && written < max_chars)
                    {
                        buf[written++] = ' ';
                        width--;
                    }
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
                if (!left_justify)
                {
                    while (width > n_idx && written < max_chars)
                    {
                        buf[written++] = pad_zero ? '0' : ' ';
                        width--;
                    }
                }
                for (int i = n_idx - 1; i >= 0 && written < max_chars; i--)
                {
                    buf[written++] = num_buf[i];
                }
                if (left_justify)
                {
                    while (width > n_idx && written < max_chars)
                    {
                        buf[written++] = ' ';
                        width--;
                    }
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
                if (!left_justify)
                {
                    while (width > h_idx && written < max_chars)
                    {
                        buf[written++] = pad_zero ? '0' : ' ';
                        width--;
                    }
                }
                for (int i = h_idx - 1; i >= 0 && written < max_chars; i--)
                {
                    buf[written++] = hex_buf[i];
                }
                if (left_justify)
                {
                    while (width > h_idx && written < max_chars)
                    {
                        buf[written++] = ' ';
                        width--;
                    }
                }
            }
            else if (*fmt == 'f')
            {
                (void)va_arg(ap, double);
                const char *fstr = "0.00";
                while (*fstr != '\0' && written < max_chars)
                {
                    buf[written++] = *fstr++;
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

/* Linker section bounds (ld/link.ld) */
extern char _stext[], _etext[], _srodata[], _erodata[], _sdata[], _stack_top[];

mem_access_t check_mem_access(uint32_t addr)
{
    /* 1. Unaligned addresses are strictly invalid for 32-bit word access */
    if (addr & WORD_ALIGN_MASK)
    {
        return MEM_ACCESS_INVALID;
    }

    /* 2. Read-Only Code in HP IRAM */
    if (addr >= (uint32_t)_stext && addr < (uint32_t)_etext)
    {
        return MEM_ACCESS_READONLY;
    }

    /* 3. Read-Only Constant Data (.rodata) in HP DRAM */
    if (addr >= (uint32_t)_srodata && addr < (uint32_t)_erodata)
    {
        return MEM_ACCESS_READONLY;
    }

    /* 4. Read-Write Data, BSS, Heap, and Stack range in HP SRAM */
    if (addr >= (uint32_t)_sdata && addr < (uint32_t)_stack_top)
    {
        return MEM_ACCESS_READWRITE;
    }

    /* 5. LP SRAM (16 KB @ 0x50000000) */
    if (addr >= LP_SRAM_START_ADDR && addr < LP_SRAM_END_ADDR)
    {
        return MEM_ACCESS_READWRITE;
    }

    /* 6. Flash XIP Execution & Read-Only Space (0x42000000 - 0x42800000) */
    if (addr >= FLASH_XIP_START_ADDR && addr < FLASH_XIP_END_ADDR)
    {
        return MEM_ACCESS_READONLY;
    }

    /* 7. Memory-Mapped I/O Peripheral Space (0x60000000 - 0x600D0000) */
    if (addr >= PERIPHERAL_MMIO_START_ADDR && addr < PERIPHERAL_MMIO_END_ADDR)
    {
        /* Reject unmapped reserved peripheral holes (TRM Tab 5.3-2):
         * 0x60019000 - 0x6007FFFF (412 KB reserved hole, containing legacy USB 0x60043000)
         * 0x6009A000 - 0x600A2FFF (36 KB reserved hole, excluding MODEM_FE at 0x600A0000 - 0x600A0FFF)
         */
        if ((addr >= PERIPHERAL_MMIO_HOLE0_FIRST && addr <= PERIPHERAL_MMIO_HOLE0_LAST) ||
            (addr >= PERIPHERAL_MMIO_HOLE1_FIRST && addr <= PERIPHERAL_MMIO_HOLE1_LAST &&
             (addr < MODEM_FE_BASE_ADDR || addr >= MODEM_FE_END_ADDR)))
        {
            return MEM_ACCESS_INVALID;
        }
        return MEM_ACCESS_MMIO;
    }

    /* 8. Core-Local Interrupt & Timer Subsystem Space (PLIC/CLINT: 0x20000000 - 0x20002000) */
    if (addr >= CORE_LOCAL_PERI_START_ADDR && addr < CORE_LOCAL_PERI_END_ADDR)
    {
        return MEM_ACCESS_MMIO;
    }

    /* 9. Internal ROM (0x40000000 - 0x40050000) */
    if (addr >= INTERNAL_ROM_START_ADDR && addr < INTERNAL_ROM_END_ADDR)
    {
        return MEM_ACCESS_READONLY;
    }

    /* All other unmapped regions */
    return MEM_ACCESS_INVALID;
}
