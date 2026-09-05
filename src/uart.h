/*
 * src/uart.h
 *
 * ESP32-C6 UART0 Interrupt-Driven Hardware Driver
 * TRM Chapter 27 (UART Controller, §27.1-§27.5)
 *
 * Provides register-level UART0 FIFO access, interrupt-driven RX ring buffer,
 * bounded TX polling with hardware safety timeout, and status telemetry.
 */

#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>
#include "regs/uart0.h"

/* UART0 Hardware Configuration Constants */
#define UART0_TIMEOUT_CYCLES             100000U
#define UART0_FIFO_THRESHOLD             112U
#define UART0_RX_BUFFER_SIZE             256U
#define UART0_RX_FIFO_THRHD_DEFAULT      1U
#define UART0_RX_TOUT_THRHD_DEFAULT      10U
#define UART0_CPU_INTR_CHANNEL           5U
#define UART0_INTR_PRIORITY              10U
#define UART0_INT_ALL_CLR_MASK           0xFFFFFFFFU

/* Status and Return Codes */
typedef enum {
    UART_STATUS_OK             =  0,
    UART_STATUS_ERR_TIMEOUT    = -1,
    UART_STATUS_ERR_NODATA     = -2,
    UART_STATUS_ERR_INVALID    = -3,
    UART_STATUS_ERR_FULL       = -4
} uart_status_t;

/* UART0 RX Ring Buffer Structure */
typedef struct {
    uint8_t buffer[UART0_RX_BUFFER_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;
    volatile uint32_t rx_bytes_total;
    volatile uint32_t tx_bytes_total;
} uart_ring_buffer_t;

/* Lifecycle & Configuration Primitives */
void uart_init(void);
void uart_isr(void *arg);

/* Character & String Output Primitives */
int uart_putc(char c);
void uart_puts(const char *str);
void uart_flush(void);

/* Character Input Primitives */
int uart_getc_nonblocking(char *c);
char uart_getc_blocking(void);

/* Telemetry & Ring Buffer Status */
void uart_get_stats(uart_ring_buffer_t *out_stats);
uint32_t uart_get_rx_count(void);
uint32_t uart_get_tx_count(void);

#endif /* UART_H */
