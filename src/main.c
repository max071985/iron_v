#include "io_constants.h"
#include "utils.h"
#include "string.h"
#include "interrupts.h"

/* Disables RISC-V watchdogs */
void disable_wdt(void)
{
    *TIMG0_WDTWPROTECT = TIMG_WDT_WKEY;
    *TIMG0_WDTCONFIG0 = 0;
    *TIMG0_WDTWPROTECT = 0;
    uart_puts("Disabled T0WDT.\r\n\n");

    *TIMG1_WDTWPROTECT = TIMG_WDT_WKEY;
    *TIMG1_WDTCONFIG0 = 0;
    *TIMG1_WDTWPROTECT = 0;
    uart_puts("Disabled T1WDT.\r\n\n");

    *RTC_WDT_WPROTECT_REG = TIMG_WDT_WKEY;
    *RTC_WDT_CONFIG0_REG = 0;
    *RTC_WDT_WPROTECT_REG = 0;
    uart_puts("Disabled RWDT.\r\n");

    *RTC_WDT_SWD_WPROTECT_REG = TIMG_WDT_WKEY;
    *RTC_WDT_SWD_CONFIG_REG |= (1u << 30);
    *RTC_WDT_SWD_WPROTECT_REG = 0;
    uart_puts("Disabled SWD\r\n");
}

/* Initialize interrupts */
void interrupt_init(void)
{
    *UART0_INT_CLR_REG = UART_RXFIFO_FULL_INT_ST_M;
    *UART0_CONF1_REG = (*UART0_CONF1_REG & ~0x1FF) | 1;
    *UART0_INT_ENA_REG |= UART_RXFIFO_FULL_INT_ENA_M;

    *INTMTX_CORE0_UART0_INTR_MAP_REG = CPU_INTR_UART0;   // Route UART0 interrupt to CPU Interrupt CPU_INTR_UART0 (default = 6)
    
    *INTPRI_CORE0_CPU_INT_THRESH_REG = 0;
    *INTPRI_CORE0_CPU_INT_TYPE_REG &= ~(1 << 18);
    *INTPRI_CORE0_CPU_INT_PRI_18_REG = 15;
    *INTPRI_CORE0_CPU_INT_ENABLE_REG |= (1 << CPU_INTR_UART0);

    // Double check mie here just in case:
    uint32_t mie_val;
    asm volatile("csrr %0, mie" : "=r"(mie_val));
    if (!(mie_val & (1 << 18))) {
        uart_puts("WARNING: CPU mie bit 18 NOT set in crt0.S!\r\n");
    }

    // Check MIE (Machine Interrupt Enable) in mstatus
    uint32_t mstatus;
    asm volatile("csrr %0, mstatus" : "=r"(mstatus));
    if ((mstatus & 0x8) == 0) { // Bit 3 is MIE
        uart_puts("Fixing MSTATUS: Enabling Global Interrupts...\r\n");
        asm volatile("csrs mstatus, %0" :: "r"(0x8));
    }

    uart_puts("INTC: Switched to Channel 18.\r\n");
}

/* Pseudo-delay function */
void delay(volatile uint32_t count)
{
    while(count--) asm volatile("nop");
}

void shell(char *input_buffer)
{
    uart_puts("> ");
    read_line(input_buffer, MAX_CMD_LEN); // Fill buffer with values given by the user
    // TEMP SOLUTION:
    if (input_buffer[0] == 'p' && input_buffer[1] == 'e' && input_buffer[2] == 'e' && input_buffer[3] == 'k' && input_buffer[4] == ' ')
    {
        // Read address:
        uint32_t addr = 0;
        char *next_arg = input_buffer + 5;
        int isvalid = s_htoi(&next_arg, &addr);
        if (isvalid)
        {
            uart_puts("Value: 0x");
            put_hex(*(volatile uint32_t *)addr);
            uart_puts("\r\n");
        }
        else
        {
            uart_puts("ERROR: Invalid address format.\r\n");
        }
    }
    else if(input_buffer[0] == 'p' && input_buffer[1] == 'o' && input_buffer[2] == 'k' && input_buffer[3] == 'e')
    {
        // Poke address
        // syntax: poke <addr> <val>
        uint32_t addr, val;
        char *next_arg = input_buffer + 4;
        if (s_htoi(&next_arg, &addr) && s_htoi(&next_arg, &val))
        {
            // Perform the update in the memory "poke"
            *(volatile uint32_t *)addr = val;
            uart_puts("Written.\r\n");
        }
        else
            uart_puts("Error: Invalid poke arguments.\r\n");
    }
    else if (input_buffer[0] != 0)
    {
        uart_puts("Unknown command.\r\n");
    }
    /////////////////////
}

__attribute__((interrupt))
void uart_isr(void)
{
    uint32_t status = *UART0_INT_ST_REG;
    
    if (status & UART_RXFIFO_FULL_INT_ST_M)
    {
        while ((*UART0_STATUS_REG & UART_RX_FIFO_CNT) > 0)
        {
            char c = (char)(*UART0_FIFO);
            uart_putc(c);
        }
        *UART0_INT_CLR_REG = UART_RXFIFO_FULL_INT_ST_M;
    }
}

void check_mip(void) {
    uint32_t mip_val;
    asm volatile("csrr %0, mip" : "=r"(mip_val));
    uart_puts("MIP: ");
    put_hex(mip_val);
    uart_puts("\r\n");
}

void main(void)
{
    char input_buffer[MAX_CMD_LEN];
    
    uart_puts("Booting...\r\n");
    uart_puts("Disabling WDT:\r\n");
    disable_wdt();

    interrupt_init();
    // Debug: Dump CSRs
    uint32_t mstatus_dbg, mie_dbg;
    asm volatile("csrr %0, mstatus" : "=r"(mstatus_dbg));
    asm volatile("csrr %0, mie" : "=r"(mie_dbg));

    uart_puts("DEBUG CSRs:\r\n");
    uart_puts("MSTATUS: "); put_hex(mstatus_dbg); uart_puts("\r\n");
    uart_puts("MIE    : "); put_hex(mie_dbg); uart_puts("\r\n");
    while(1) {
        check_mip();
        shell(input_buffer);
    }
}