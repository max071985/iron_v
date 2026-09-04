/*
 * src/regs/plic.h
 *
 * ESP32-C6 RISC-V High-Performance Core PLIC & CLINT Register Map
 * Base Addresses:
 * - PLIC MX (Machine Mode): 0x20001000
 * - PLIC UX (User Mode):    0x20001400
 * - CLINT M (Machine Mode): 0x20001800
 * - CLINT U (User Mode):    0x20001C00
 */

#ifndef REGS_PLIC_H
#define REGS_PLIC_H

#include <stdint.h>

/* Base Addresses */
#define PLIC_MX_BASE                0x20001000U
#define PLIC_UX_BASE                0x20001400U
#define CLINT_M_BASE                0x20001800U
#define CLINT_U_BASE                0x20001C00U

/* Subsystem Boundary Constants */
#define CORE_LOCAL_PERI_START_ADDR  0x20000000U
#define CORE_LOCAL_PERI_END_ADDR    0x20002000U

/* PLIC MX Register Offsets */
#define PLIC_MXINT_ENABLE_OFFSET    0x00U
#define PLIC_MXINT_TYPE_OFFSET      0x04U
#define PLIC_MXINT_CLEAR_OFFSET     0x08U
#define PLIC_EMIP_STATUS_OFFSET     0x0CU
#define PLIC_MXINT_PRI_BASE_OFFSET  0x10U
#define PLIC_MXINT_THRESH_OFFSET    0x90U
#define PLIC_MXINT_CLAIM_OFFSET     0x94U

/* Parameterized PLIC MX Register Accessor Macros */
#define PLIC_MXINT_ENABLE_REG       ((volatile uint32_t *)(PLIC_MX_BASE + PLIC_MXINT_ENABLE_OFFSET))
#define PLIC_MXINT_TYPE_REG         ((volatile uint32_t *)(PLIC_MX_BASE + PLIC_MXINT_TYPE_OFFSET))
#define PLIC_MXINT_CLEAR_REG        ((volatile uint32_t *)(PLIC_MX_BASE + PLIC_MXINT_CLEAR_OFFSET))
#define PLIC_EMIP_STATUS_REG        ((volatile uint32_t *)(PLIC_MX_BASE + PLIC_EMIP_STATUS_OFFSET))
#define PLIC_MXINT_PRI_REG(chan)    ((volatile uint32_t *)(PLIC_MX_BASE + PLIC_MXINT_PRI_BASE_OFFSET + ((uint32_t)(chan) * 4U)))
#define PLIC_MXINT_THRESH_REG       ((volatile uint32_t *)(PLIC_MX_BASE + PLIC_MXINT_THRESH_OFFSET))
#define PLIC_MXINT_CLAIM_REG        ((volatile uint32_t *)(PLIC_MX_BASE + PLIC_MXINT_CLAIM_OFFSET))

/* CLINT Machine-Mode Register Offsets */
#define CLINT_MSIP_OFFSET           0x00U
#define CLINT_MTIMECTL_OFFSET       0x04U
#define CLINT_MTIME_LO_OFFSET       0x08U
#define CLINT_MTIME_HI_OFFSET       0x0CU
#define CLINT_MTIMECMP_LO_OFFSET    0x10U
#define CLINT_MTIMECMP_HI_OFFSET    0x14U

/* CLINT Machine-Mode Register Accessor Macros */
#define CLINT_MSIP_REG              ((volatile uint32_t *)(CLINT_M_BASE + CLINT_MSIP_OFFSET))
#define CLINT_MTIMECTL_REG          ((volatile uint32_t *)(CLINT_M_BASE + CLINT_MTIMECTL_OFFSET))
#define CLINT_MTIME_LO_REG          ((volatile uint32_t *)(CLINT_M_BASE + CLINT_MTIME_LO_OFFSET))
#define CLINT_MTIME_HI_REG          ((volatile uint32_t *)(CLINT_M_BASE + CLINT_MTIME_HI_OFFSET))
#define CLINT_MTIMECMP_LO_REG       ((volatile uint32_t *)(CLINT_M_BASE + CLINT_MTIMECMP_LO_OFFSET))
#define CLINT_MTIMECMP_HI_REG       ((volatile uint32_t *)(CLINT_M_BASE + CLINT_MTIMECMP_HI_OFFSET))

/* Bitfield Constants */
#define PLIC_ENABLE_ALL_MASK        0xFFFFFFFFU
#define PLIC_CLEAR_ALL_MASK         0xFFFFFFFFU
#define PLIC_PRIORITY_MASK          0x0000000FU
#define PLIC_THRESH_MASK            0x000000FFU
#define CLINT_MSIP_TRIGGER_BIT      0x00000001U

#endif /* REGS_PLIC_H */
