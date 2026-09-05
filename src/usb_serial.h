/*
 * src/usb_serial.h
 *
 * ESP32-C6 USB-Serial-JTAG CDC-ACM Hardware Driver
 * TRM Chapter 32 (USB Serial/JTAG Controller, §32.1-§32.7)
 *
 * Provides register-level CDC-ACM endpoint 1 FIFO management, non-blocking
 * transmission with timeout protection, and non-faulting hardware access.
 */

#ifndef USB_SERIAL_H
#define USB_SERIAL_H

#include <stdint.h>
#include <stddef.h>
#include "regs/usb_device.h"

/* Default non-blocking transmission timeout cycles (~320,000 cycles at 160MHz ~= 2ms) */
#define USB_SERIAL_DEFAULT_TX_TIMEOUT_CYCLES   320000U

/* CDC-ACM Endpoint 1 Hardware Buffer Size */
#define USB_SERIAL_EP1_FIFO_SIZE               64U

/* Interrupt status clear mask (clears all pending raw interrupt flags) */
#define USB_SERIAL_INT_ALL_CLR_MASK            0xFFFFFFFFU

/* USB-Serial-JTAG Device Hardware Structure */
typedef struct {
    volatile uint32_t *ep1_reg;       /* 0x6000F000 */
    volatile uint32_t *ep1_conf_reg;  /* 0x6000F004 */
    volatile uint32_t *int_raw_reg;   /* 0x6000F008 */
    volatile uint32_t *int_ena_reg;   /* 0x6000F010 */
    volatile uint32_t *int_clr_reg;   /* 0x6000F014 */
    uint32_t tx_timeout_cycles;
    uint32_t tx_dropped_bytes;
    uint32_t tx_bytes_total;
    uint32_t rx_bytes_total;
} usb_serial_dev_t;

/* Status and Return Codes */
typedef enum {
    USB_SERIAL_OK             =  0,
    USB_SERIAL_ERR_NODATA     = -1,
    USB_SERIAL_ERR_TIMEOUT    = -2,
    USB_SERIAL_ERR_INVALID    = -3
} usb_serial_status_t;

/* Initialize USB-Serial-JTAG peripheral clock in PCR and configure hardware device */
void usb_serial_init(void);

/* Transmit character with cycle-bounded timeout protection */
int usb_serial_putc_blocking(char c);

/* Non-blocking character transmission (returns immediately if buffer unavailable) */
int usb_serial_putc_nonblocking(char c);

/* String and buffer transmission */
int usb_serial_puts(const char *str);
int usb_serial_write(const uint8_t *data, size_t len);

/* Flush transmit FIFO to host */
void usb_serial_flush(void);

/* Non-blocking character receive */
int usb_serial_getc_nonblocking(char *c);

/* Blocking character receive with timeout */
int usb_serial_getc_blocking(char *c);

/* Read available bytes into buffer */
size_t usb_serial_read(uint8_t *buffer, size_t max_len);

/* Hardware status queries */
int usb_serial_is_tx_ready(void);
int usb_serial_is_rx_ready(void);
void usb_serial_get_dev(usb_serial_dev_t *out_dev);

#endif /* USB_SERIAL_H */
