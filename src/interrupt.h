/*
 * src/interrupt.h
 *
 * ESP32-C6 Interrupt Matrix (INTMTX) and Core Interrupt Controller (INTPRI) Driver
 * TRM Chapter 10 (INTMTX) & Chapter 1 (High-Performance CPU Interrupt Controller)
 */

#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>
#include <stddef.h>
#include "trap.h"
#include "regs/interrupt_core0.h"
#include "regs/intpri.h"

/* Total CPU interrupt channels in ESP-RISC-V core (0 to 31) */
#define INTERRUPT_CPU_CHANNELS 32U

/* Total peripheral interrupt sources in ESP32-C6 INTMTX (0 to 76) */
#define INTERRUPT_SOURCE_COUNT 77U

/* Priority level boundaries (1 to 15, 0 = disabled) */
#define INTERRUPT_PRIORITY_MIN 1U
#define INTERRUPT_PRIORITY_MAX 15U

/* Preemption threshold boundary (0 to 15) */
#define INTERRUPT_THRESHOLD_MAX 15U

/* Software interrupt channel count (0 to 3) */
#define INTPRI_SW_INTR_COUNT 4U

/* Bitfield masks for hardware registers */
#define INTMTX_MAP_CHANNEL_MASK   0x0000001FU /* Bits 4:0: CPU interrupt channel */
#define INTPRI_PRIORITY_MASK      0x0000000FU /* Bits 3:0: Priority level */
#define INTPRI_THRESH_MASK        0x000000FFU /* Bits 7:0: Interrupt threshold */
#define INTPRI_CLEAR_ALL_MASK     0xFFFFFFFFU /* Acknowledge all edge interrupt lines */

/* RISC-V Machine Status CSR (mstatus) bitfield constants */
#define MSTATUS_MIE_BIT           (1U << 3)   /* Bit 3: Machine Interrupt Enable */
#define MSTATUS_MPP_MACHINE_MODE  (3U << 11)  /* Bits 12:11: MPP = 2'b11 (Machine Mode) */

/* Register array address calculation macros */
#define INTMTX_SOURCE_MAP_REG(src)   ((volatile uint32_t *)(INTERRUPT_CORE0_BASE + ((uint32_t)(src) * 4U)))
#define INTPRI_CPU_PRI_REG(chan)     ((volatile uint32_t *)(INTPRI_BASE + 0x0CU + ((uint32_t)(chan) * 4U)))
#define INTPRI_SW_INTR_REG(idx)      ((volatile uint32_t *)(INTPRI_BASE + 0x90U + ((uint32_t)(idx) * 4U)))

/* Interrupt Trigger Type */
typedef enum {
    INTR_TYPE_LEVEL = 0,
    INTR_TYPE_EDGE  = 1
} interrupt_type_t;

/*
 * Peripheral Interrupt Sources (ESP32-C6 TRM Table 10.3-1)
 * Register offset: INTERRUPT_CORE0_BASE + 4 * source
 */
typedef enum {
    INT_SRC_WIFI_MAC                = 0,
    INT_SRC_WIFI_MAC_NMI            = 1,
    INT_SRC_WIFI_PWR                = 2,
    INT_SRC_WIFI_BB                 = 3,
    INT_SRC_BT_MAC                  = 4,
    INT_SRC_BT_BB                   = 5,
    INT_SRC_BT_BB_NMI               = 6,
    INT_SRC_LP_TIMER                = 7,
    INT_SRC_COEX                    = 8,
    INT_SRC_BLE_TIMER               = 9,
    INT_SRC_BLE_SEC                 = 10,
    INT_SRC_I2C_MST                 = 11,
    INT_SRC_ZB_MAC                  = 12,
    INT_SRC_PMU                     = 13,
    INT_SRC_EFUSE                   = 14,
    INT_SRC_LP_RTC_TIMER            = 15,
    INT_SRC_LP_UART                 = 16,
    INT_SRC_LP_I2C                  = 17,
    INT_SRC_LP_WDT                  = 18,
    INT_SRC_LP_PERI_TIMEOUT         = 19,
    INT_SRC_LP_APM_M0               = 20,
    INT_SRC_LP_APM_M1               = 21,
    INT_SRC_CPU_INTR_FROM_CPU_0     = 22,
    INT_SRC_CPU_INTR_FROM_CPU_1     = 23,
    INT_SRC_CPU_INTR_FROM_CPU_2     = 24,
    INT_SRC_CPU_INTR_FROM_CPU_3     = 25,
    INT_SRC_ASSIST_DEBUG            = 26,
    INT_SRC_TRACE                   = 27,
    INT_SRC_CACHE                   = 28,
    INT_SRC_CPU_PERI_TIMEOUT        = 29,
    INT_SRC_GPIO                    = 30,
    INT_SRC_GPIO_NMI                = 31,
    INT_SRC_PAU                     = 32,
    INT_SRC_HP_PERI_TIMEOUT         = 33,
    INT_SRC_MODEM_PERI_TIMEOUT      = 34,
    INT_SRC_HP_APM_M0               = 35,
    INT_SRC_HP_APM_M1               = 36,
    INT_SRC_HP_APM_M2               = 37,
    INT_SRC_HP_APM_M3               = 38,
    INT_SRC_LP_APM0                 = 39,
    INT_SRC_MSPI                    = 40,
    INT_SRC_I2S1                    = 41,
    INT_SRC_UHCI0                   = 42,
    INT_SRC_UART0                   = 43,
    INT_SRC_UART1                   = 44,
    INT_SRC_LEDC                    = 45,
    INT_SRC_CAN0                    = 46,
    INT_SRC_CAN1                    = 47,
    INT_SRC_USB_SERIAL_JTAG         = 48,
    INT_SRC_RMT                     = 49,
    INT_SRC_I2C_EXT0                = 50,
    INT_SRC_TG0_T0                  = 51,
    INT_SRC_TG0_T1                  = 52,
    INT_SRC_TG0_WDT                 = 53,
    INT_SRC_TG1_T0                  = 54,
    INT_SRC_TG1_T1                  = 55,
    INT_SRC_TG1_WDT                 = 56,
    INT_SRC_SYSTIMER_TARGET0        = 57,
    INT_SRC_SYSTIMER_TARGET1        = 58,
    INT_SRC_SYSTIMER_TARGET2        = 59,
    INT_SRC_APB_ADC                 = 60,
    INT_SRC_PWM                     = 61,
    INT_SRC_PCNT                    = 62,
    INT_SRC_PARL_IO                 = 63,
    INT_SRC_SLC0                    = 64,
    INT_SRC_SLC1                    = 65,
    INT_SRC_DMA_IN_CH0              = 66,
    INT_SRC_DMA_IN_CH1              = 67,
    INT_SRC_DMA_IN_CH2              = 68,
    INT_SRC_DMA_OUT_CH0             = 69,
    INT_SRC_DMA_OUT_CH1             = 70,
    INT_SRC_DMA_OUT_CH2             = 71,
    INT_SRC_GPSPI2                  = 72,
    INT_SRC_AES                     = 73,
    INT_SRC_SHA                     = 74,
    INT_SRC_RSA                     = 75,
    INT_SRC_ECC                     = 76,
    INT_SRC_MAX_COUNT               = 77
} interrupt_source_t;

/* ISR Callback function signature */
typedef void (*isr_handler_t)(void *arg);

/* Subsystem initialization and hardware reset */
void interrupt_init(void);

/* Interrupt Matrix (INTMTX) routing */
int interrupt_route(interrupt_source_t source, uint32_t cpu_channel);
int interrupt_unroute(interrupt_source_t source);
uint32_t interrupt_get_map(interrupt_source_t source);

/* Core Interrupt Controller (INTPRI) configuration */
int interrupt_set_priority(uint32_t cpu_channel, uint32_t priority);
uint32_t interrupt_get_priority(uint32_t cpu_channel);

int interrupt_set_threshold(uint32_t threshold);
uint32_t interrupt_get_threshold(void);

int interrupt_set_type(uint32_t cpu_channel, interrupt_type_t type);
interrupt_type_t interrupt_get_type(uint32_t cpu_channel);

int interrupt_enable(uint32_t cpu_channel);
int interrupt_disable(uint32_t cpu_channel);
int interrupt_is_enabled(uint32_t cpu_channel);

/* ISR Handler registration and query */
int interrupt_register_handler(uint32_t cpu_channel, isr_handler_t handler, void *arg);
int interrupt_unregister_handler(uint32_t cpu_channel);
uint32_t interrupt_get_count(uint32_t cpu_channel);

/* Software interrupt triggers (0 to 3) */
void interrupt_trigger_cpu_intr(uint32_t sw_intr_idx);
void interrupt_clear_cpu_intr(uint32_t sw_intr_idx);

/* Global Machine Interrupt Enable (mstatus.MIE) management */
void interrupt_global_enable(void);
void interrupt_global_disable(void);
uint32_t interrupt_global_save_and_disable(void);
void interrupt_global_restore(uint32_t prev_mstatus);

/* Central interrupt dispatcher invoked from trap_handler */
void interrupt_dispatch(uint32_t channel, trapframe_t *tf);

#endif // INTERRUPT_H
