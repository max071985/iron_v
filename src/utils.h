#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

int uart_putc(char c);
void uart_puts(const char *str);
int uart_getc_nonblocking(char *c);
char uart_getc_blocking(void);
void read_line(char *buffer, int max_len);
void put_hex(uint32_t val);
void put_dec(uint32_t val);

#endif // UTILS_H