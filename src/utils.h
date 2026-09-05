#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stddef.h>
#include "uart.h"
#include "console.h"

void read_line(char *buffer, int max_len);
void put_hex(uint32_t val);
void put_dec(uint32_t val);

/* ASCII character constants */
#define ASCII_BS               0x08    /* Backspace */
#define ASCII_DEL              0x7F    /* Delete */
#define ASCII_PRINTABLE_MIN    ' '     /* Printable range start (0x20 Space) */
#define ASCII_PRINTABLE_MAX    '~'     /* Printable range end (0x7E Tilde) */

/* Linker symbols for stack boundary tracking */
extern char _stack_top[];
extern char _ebss[];

/* Stack boundary and margin inspection macros */
#define STACK_TOP_ADDR         ((uint32_t)_stack_top)
#define STACK_LIMIT_ADDR       ((uint32_t)_ebss)
#define STACK_TOTAL_SIZE       (STACK_TOP_ADDR - STACK_LIMIT_ADDR)
#define GET_CURRENT_SP(sp)     asm volatile("mv %0, sp" : "=r"(sp))
#define GET_STACK_MARGIN(sp)   ((uint32_t)(sp) - STACK_LIMIT_ADDR)

#endif // UTILS_H
