#include <stdint.h> 
#include "io_constants.h"

// Interrupt matrix
#define INTMTX_BASE                     0x60010000
#define INTMTX_CORE0_UART0_INTR_MAP_REG ((volatile uint32_t *)(INTMTX_BASE + 0x00AC))

// CPU interrupts
#define CPU_INTR_UART0  6
#define MIE_UART0_MASK  (1 << CPU_INTR_UART0)

// UART interrupts
#define UART0_INT_ST_REG     ((volatile uint32_t *)(UART0_BASE + 0x0008))
#define UART0_INT_ENA_REG    ((volatile uint32_t *)(UART0_BASE + 0x000C))
#define UART0_INT_CLR_REG    ((volatile uint32_t *)(UART0_BASE + 0x0010))