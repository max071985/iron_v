/*
 * src/console.h
 *
 * Unified Dual-Console Subsystem (UART0 & USB-Serial-JTAG CDC-ACM)
 * Multiplexes input and output streams across active hardware ports.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include <stddef.h>

/* Console Backend Bitmasks */
#define CONSOLE_MASK_UART0               (1U << 0)
#define CONSOLE_MASK_USB                 (1U << 1)
#define CONSOLE_MASK_ALL                 (CONSOLE_MASK_UART0 | CONSOLE_MASK_USB)

/* Maximum Shell Command Line Buffer Length */
#define CONSOLE_MAX_LINE_LEN             128U

/* Backend Interface Function Pointers (per Roadmap Task 2.5) */
typedef struct {
    void (*putc)(char c);
    void (*puts)(const char *str);
    int (*getc_nonblocking)(char *c);
    void (*flush)(void);
} console_backend_t;

/* Unified Console Manager Structure (per Roadmap Task 2.5) */
typedef struct {
    console_backend_t uart;
    console_backend_t usb;
    uint8_t echo_enabled;
    uint8_t active_mask; /* bit 0 = UART0, bit 1 = USB */
} console_manager_t;

/* Lifecycle and Configuration */
void console_init(void);
void console_set_active_mask(uint8_t mask);
uint8_t console_get_active_mask(void);
void console_set_echo(uint8_t enable);
uint8_t console_get_echo(void);
void console_get_manager(console_manager_t *out_mgr);

/* Multiplexed Input/Output Primitives */
void console_putc(char c);
void console_puts(const char *str);
int console_getc_nonblocking(char *c);
char console_getc_blocking(void);
void console_flush(void);

/* Non-blocking line accumulator / reader */
int console_read_line_nonblocking(char *out_buffer, size_t max_len);

#endif /* CONSOLE_H */
