/*
 * src/uart.c
 *
 * ESP32-C6 UART0 Interrupt-Driven Hardware Driver
 * TRM Chapter 27 (UART Controller, §27.1-§27.5)
 */

#include "uart.h"
#include "io_constants.h"
#include "interrupt.h"
#include "dpc.h"
#include "wdt.h"

static uart_ring_buffer_t g_uart_rx_ring = {
    .head = 0U,
    .tail = 0U,
    .overflow_count = 0U,
    .rx_bytes_total = 0U,
    .tx_bytes_total = 0U
};

void uart_init(void)
{
    /* 1. Configure UART0 RX FIFO threshold and timeout registers */
    *UART0_CONF1_REG = (*UART0_CONF1_REG & ~UART0_CONF1_RXFIFO_FULL_THRHD_M) |
                       UART0_CONF1_RXFIFO_FULL_THRHD_V(UART0_RX_FIFO_THRHD_DEFAULT);

    *UART0_TOUT_CONF_REG = UART0_TOUT_CONF_RX_TOUT_EN_M |
                           UART0_TOUT_CONF_RX_TOUT_THRHD_V(UART0_RX_TOUT_THRHD_DEFAULT);
    FENCE();

    /* 2. Clear all pending interrupt flags */
    *UART0_INT_CLR_REG = UART0_INT_ALL_CLR_MASK;
    FENCE();

    /* 3. Enable RX FIFO full and RX FIFO timeout interrupts */
    *UART0_INT_ENA_REG |= (UART0_INT_ENA_RXFIFO_FULL_INT_ENA_M | UART0_INT_ENA_RXFIFO_TOUT_INT_ENA_M);
    FENCE();

    /* 4. Reset software RX ring buffer state */
    g_uart_rx_ring.head = 0U;
    g_uart_rx_ring.tail = 0U;
    g_uart_rx_ring.overflow_count = 0U;
    g_uart_rx_ring.rx_bytes_total = 0U;
    g_uart_rx_ring.tx_bytes_total = 0U;

    /* 5. Route UART0 (INT_SRC_UART0 = 43) to CPU channel 5 */
    interrupt_route(INT_SRC_UART0, UART0_CPU_INTR_CHANNEL);
    interrupt_set_priority(UART0_CPU_INTR_CHANNEL, UART0_INTR_PRIORITY);
    interrupt_register_handler(UART0_CPU_INTR_CHANNEL, uart_isr, NULL);
    interrupt_enable(UART0_CPU_INTR_CHANNEL);
    interrupt_global_enable();
    FENCE();
}

void uart_isr(void *arg)
{
    (void)arg;

    /* Read all available bytes from hardware RX FIFO */
    while ((*UART0_STATUS_REG & UART_RX_FIFO_CNT) > 0U)
    {
        uint32_t val = *UART0_FIFO;
        uint8_t byte = (uint8_t)(val & 0xFFU);

        uint32_t next_head = (g_uart_rx_ring.head + 1U) % UART0_RX_BUFFER_SIZE;
        if (next_head != g_uart_rx_ring.tail)
        {
            g_uart_rx_ring.buffer[g_uart_rx_ring.head] = byte;
            FENCE();
            g_uart_rx_ring.head = next_head;
            g_uart_rx_ring.rx_bytes_total++;
        }
        else
        {
            g_uart_rx_ring.overflow_count++;
        }
    }

    /* Clear interrupt status flags in UART0 */
    *UART0_INT_CLR_REG = (UART0_INT_CLR_RXFIFO_FULL_INT_CLR_M | UART0_INT_CLR_RXFIFO_TOUT_INT_CLR_M);
    FENCE();
}

int uart_putc(char c)
{
    volatile uint32_t timeout = UART0_TIMEOUT_CYCLES;
    while (((*UART0_STATUS_REG >> UART_TX_FIFO_CNT_SHIFT) & 0xFFU) > UART0_FIFO_THRESHOLD)
    {
        if (--timeout == 0U)
        {
            return UART_STATUS_ERR_TIMEOUT;
        }
    }

    *UART0_FIFO = (uint32_t)(uint8_t)c;
    FENCE();
    g_uart_rx_ring.tx_bytes_total++;
    return UART_STATUS_OK;
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

void uart_flush(void)
{
    volatile uint32_t timeout = UART0_TIMEOUT_CYCLES;
    while (((*UART0_STATUS_REG >> UART_TX_FIFO_CNT_SHIFT) & 0xFFU) > 0U)
    {
        if (--timeout == 0U)
        {
            break;
        }
    }
}

int uart_getc_nonblocking(char *c)
{
    if (!c) return 0;

    /* 1. Check software interrupt RX ring buffer */
    if (g_uart_rx_ring.head != g_uart_rx_ring.tail)
    {
        *c = (char)g_uart_rx_ring.buffer[g_uart_rx_ring.tail];
        FENCE();
        g_uart_rx_ring.tail = (g_uart_rx_ring.tail + 1U) % UART0_RX_BUFFER_SIZE;
        return 1;
    }

    /* 2. Direct hardware FIFO fallback ONLY when interrupts are disabled */
    if (!interrupt_is_enabled(UART0_CPU_INTR_CHANNEL))
    {
        if ((*UART0_STATUS_REG & UART_RX_FIFO_CNT) > 0U)
        {
            *c = (char)(*UART0_FIFO & 0xFFU);
            FENCE();
            g_uart_rx_ring.rx_bytes_total++;
            return 1;
        }
    }

    return 0;
}

char uart_getc_blocking(void)
{
    char c = '\0';
    while (!uart_getc_nonblocking(&c))
    {
        wdt_supervisor_tick();
        dpc_process_all();
    }
    return c;
}

void uart_get_stats(uart_ring_buffer_t *out_stats)
{
    if (!out_stats) return;
    *out_stats = g_uart_rx_ring;
}

uint32_t uart_get_rx_count(void)
{
    return g_uart_rx_ring.rx_bytes_total;
}

uint32_t uart_get_tx_count(void)
{
    return g_uart_rx_ring.tx_bytes_total;
}
