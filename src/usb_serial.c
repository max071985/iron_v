/*
 * src/usb_serial.c
 *
 * ESP32-C6 USB-Serial-JTAG CDC-ACM Hardware Driver
 * TRM Chapter 32 (USB Serial/JTAG Controller, §32.1-§32.7)
 */

#include "usb_serial.h"
#include "io_constants.h"
#include "regs/pcr.h"
#include "wdt.h"
#include "dpc.h"

static usb_serial_dev_t g_usb_serial_dev = {
    .ep1_reg = USB_DEVICE_EP1_REG,
    .ep1_conf_reg = USB_DEVICE_EP1_CONF_REG,
    .int_raw_reg = USB_DEVICE_INT_RAW_REG,
    .int_ena_reg = USB_DEVICE_INT_ENA_REG,
    .int_clr_reg = USB_DEVICE_INT_CLR_REG,
    .tx_timeout_cycles = USB_SERIAL_DEFAULT_TX_TIMEOUT_CYCLES,
    .tx_dropped_bytes = 0U,
    .tx_bytes_total = 0U,
    .rx_bytes_total = 0U
};

static uint32_t s_tx_buffered_bytes = 0U;

void usb_serial_init(void)
{
    /* 1. Enable USB_DEVICE clock in PCR and deassert reset (TRM §8.4 & §32.3) */
    *PCR_USB_DEVICE_CONF_REG |= PCR_USB_DEVICE_CONF_USB_DEVICE_CLK_EN_M;
    *PCR_USB_DEVICE_CONF_REG &= ~PCR_USB_DEVICE_CONF_USB_DEVICE_RST_EN_M;
    FENCE();

    /* 2. Configure default descriptor pointers and telemetry */
    g_usb_serial_dev.ep1_reg = USB_DEVICE_EP1_REG;
    g_usb_serial_dev.ep1_conf_reg = USB_DEVICE_EP1_CONF_REG;
    g_usb_serial_dev.int_raw_reg = USB_DEVICE_INT_RAW_REG;
    g_usb_serial_dev.int_ena_reg = USB_DEVICE_INT_ENA_REG;
    g_usb_serial_dev.int_clr_reg = USB_DEVICE_INT_CLR_REG;
    g_usb_serial_dev.tx_timeout_cycles = USB_SERIAL_DEFAULT_TX_TIMEOUT_CYCLES;
    g_usb_serial_dev.tx_dropped_bytes = 0U;
    g_usb_serial_dev.tx_bytes_total = 0U;
    g_usb_serial_dev.rx_bytes_total = 0U;
    s_tx_buffered_bytes = 0U;

    /* 3. Mask interrupts by default (unmasked when registering DPC handlers) */
    *USB_DEVICE_INT_ENA_REG = 0U;
    *USB_DEVICE_INT_CLR_REG = USB_SERIAL_INT_ALL_CLR_MASK;
    FENCE();
}

int usb_serial_is_tx_ready(void)
{
    return ((*USB_DEVICE_EP1_CONF_REG & USB_DEVICE_EP1_CONF_SERIAL_IN_EP_DATA_FREE_M) != 0U);
}

int usb_serial_is_rx_ready(void)
{
    return ((*USB_DEVICE_EP1_CONF_REG & USB_DEVICE_EP1_CONF_SERIAL_OUT_EP_DATA_AVAIL_M) != 0U);
}

int usb_serial_putc_nonblocking(char c)
{
    if (s_tx_buffered_bytes == 0U && !usb_serial_is_tx_ready())
    {
        g_usb_serial_dev.tx_dropped_bytes++;
        return USB_SERIAL_ERR_TIMEOUT;
    }

    *USB_DEVICE_EP1_REG = (uint32_t)(uint8_t)c;
    FENCE();
    s_tx_buffered_bytes++;
    g_usb_serial_dev.tx_bytes_total++;

    if (c == '\n' || s_tx_buffered_bytes >= USB_SERIAL_EP1_FIFO_SIZE)
    {
        *USB_DEVICE_EP1_CONF_REG |= USB_DEVICE_EP1_CONF_WR_DONE_M;
        FENCE();
        s_tx_buffered_bytes = 0U;
    }

    return USB_SERIAL_OK;
}

int usb_serial_putc_blocking(char c)
{
    if (s_tx_buffered_bytes == 0U)
    {
        uint32_t timeout = g_usb_serial_dev.tx_timeout_cycles;
        while (!usb_serial_is_tx_ready())
        {
            if (timeout == 0U)
            {
                g_usb_serial_dev.tx_dropped_bytes++;
                return USB_SERIAL_ERR_TIMEOUT;
            }
            timeout--;
        }
    }

    *USB_DEVICE_EP1_REG = (uint32_t)(uint8_t)c;
    FENCE();
    s_tx_buffered_bytes++;
    g_usb_serial_dev.tx_bytes_total++;

    if (c == '\n' || s_tx_buffered_bytes >= USB_SERIAL_EP1_FIFO_SIZE)
    {
        *USB_DEVICE_EP1_CONF_REG |= USB_DEVICE_EP1_CONF_WR_DONE_M;
        FENCE();
        s_tx_buffered_bytes = 0U;
    }

    return USB_SERIAL_OK;
}

int usb_serial_puts(const char *str)
{
    if (!str) return USB_SERIAL_ERR_INVALID;

    while (*str)
    {
        int res = usb_serial_putc_blocking(*str++);
        if (res != USB_SERIAL_OK)
        {
            return res;
        }
    }
    return USB_SERIAL_OK;
}

int usb_serial_write(const uint8_t *data, size_t len)
{
    if (!data) return USB_SERIAL_ERR_INVALID;

    for (size_t i = 0; i < len; i++)
    {
        int res = usb_serial_putc_blocking((char)data[i]);
        if (res != USB_SERIAL_OK)
        {
            return res;
        }
    }
    return USB_SERIAL_OK;
}

void usb_serial_flush(void)
{
    if (s_tx_buffered_bytes > 0U)
    {
        *USB_DEVICE_EP1_CONF_REG |= USB_DEVICE_EP1_CONF_WR_DONE_M;
        FENCE();
        s_tx_buffered_bytes = 0U;
    }
}

int usb_serial_getc_nonblocking(char *c)
{
    if (!c) return USB_SERIAL_ERR_INVALID;

    if (!usb_serial_is_rx_ready())
    {
        return USB_SERIAL_ERR_NODATA;
    }

    uint32_t val = *USB_DEVICE_EP1_REG;
    *c = (char)(val & USB_DEVICE_EP1_RDWR_BYTE_M);
    FENCE();

    g_usb_serial_dev.rx_bytes_total++;
    return USB_SERIAL_OK;
}

int usb_serial_getc_blocking(char *c)
{
    if (!c) return USB_SERIAL_ERR_INVALID;

    while (!usb_serial_is_rx_ready())
    {
        wdt_supervisor_tick();
        dpc_process_all();
    }

    uint32_t val = *USB_DEVICE_EP1_REG;
    *c = (char)(val & USB_DEVICE_EP1_RDWR_BYTE_M);
    FENCE();

    g_usb_serial_dev.rx_bytes_total++;
    return USB_SERIAL_OK;
}

size_t usb_serial_read(uint8_t *buffer, size_t max_len)
{
    if (!buffer || max_len == 0U) return 0U;

    size_t count = 0U;
    while (count < max_len && usb_serial_is_rx_ready())
    {
        char c = '\0';
        if (usb_serial_getc_nonblocking(&c) == USB_SERIAL_OK)
        {
            buffer[count++] = (uint8_t)c;
        }
        else
        {
            break;
        }
    }
    return count;
}

void usb_serial_get_dev(usb_serial_dev_t *out_dev)
{
    if (!out_dev) return;
    *out_dev = g_usb_serial_dev;
}
