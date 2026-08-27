# Iron V: Core Design Principles & Capabilities

## Overview
This document records the foundational engineering design principles and architectural capabilities governing the Iron V bare-metal runtime on the ESP32-C6 (RV32IMAC).

---

## 1. Deterministic Static Memory (Zero Dynamic Heap Fragmentation)
- **Constraint:** Strictly no unbounded `malloc()` / `free()` in the runtime and communication pathways.
- **Implementation:**
  - All process control blocks, DMA buffers, network packet pools, and task stacks are pre-allocated at compile time in fixed-size static memory arenas.
  - Guarantees 24/7 uptime without memory leaks, fragmentation, or out-of-memory panics.
- **Ref:** ESP32-C6 TRM Chapter 3.1 & 3.3.1.

---

## 2. Physical Memory Protection (PMP) over Virtual Paging
- **Hardware Reality:** The ESP32-C6 HP RISC-V core has no Sv32 MMU (no virtual page tables / `satp`).
- **Implementation:**
  - Task isolation and memory safety are enforced via **RISC-V PMP registers (`pmpcfgX`, `pmpaddrX`)** and Espressif APM (Access Permission Management).
  - Runtime kernel executes in Machine Mode (`M-mode`).
  - Tasks execute in User Mode (`U-mode`) and access kernel services via `ecall` traps.
- **Ref:** ESP32-C6 TRM Chapter 1.2, Chapter 3.4; RISC-V Privileged Spec v20211203.

---

## 3. Harvard Architecture Enforcement (IRAM / DRAM / Flash Split)
- **Constraint:** Explicit section placement attributes for every symbol.
- **Memory Mapping:**
  - **`.iram0.text` (HP SRAM `0x40800000`):** Interrupt service routines, trap vectors, context switchers, and flash-programming routines (guarantees zero cache-miss latency).
  - **`.dram0.data` / `.dram0.bss` (HP SRAM):** Mutable globals, stacks, DMA descriptors, and ring buffers.
  - **`.flash.text` / `.flash.rodata` (Flash XIP `0x42000000` / `0x42800000`):** Bulk application logic, static HTML/JSON web assets, and lookup tables.
- **Ref:** ESP32-C6 TRM Chapter 3.3, Chapter 4.1.

---

## 4. Two-Tiered Interrupt & Asynchronous DPC Architecture
- **Constraint:** Interrupt service routines must execute in bounded, minimal cycles.
- **Implementation:**
  - **Top-Half ISR:** Executes in `.iram0.text` on a fixed stack; raises hardware threshold (`INTPRI_CPU_INT_THRESH_REG`) to enable deterministic nested preemption; acknowledges hardware, stages descriptors, and yields.
  - **Bottom-Half (Deferred Procedure Call / DPC):** A lock-free Single-Producer Single-Consumer (SPSC) ring buffer in DRAM drained in thread mode for Wi-Fi 802.11 frames, Thread packets, and HTTP parsing.
- **Ref:** ESP32-C6 TRM Chapter 8 (Interrupt Matrix) & Chapter 9 (INTPRI).

---

## 5. Register-Accurate, Zero-Magic MMIO with Hardware Barriers
- **Constraint:** No raw magic integer offsets; all hardware registers mapped via typed SVD structs.
- **Implementation:**
  - All register pointers are strictly marked `volatile` and 32-bit aligned.
  - Explicit memory barriers (`asm volatile ("fence" ::: "memory")` and `fence.i`) are placed before/after DMA triggers, interrupt status clearing, and clock reconfiguration.
- **Ref:** ESP32-C6 SVD, RISC-V Unprivileged Spec.

---

## 6. Bounded Polling & Active Multi-Tiered Watchdog Supervision
- **Constraint:** Zero infinite polling loops (`while(!ready)`); active hardware health management.
- **Implementation:**
  - All peripheral polling loops include hard, cycle-bounded timeout counters returning explicit error codes.
  - Hardware watchdogs (**TIMG0 WDT**, **TIMG1 WDT**, **RTC WDT**) are actively managed by a kernel supervisor tick rather than permanently disabled.
  - Fatal traps dump full CPU registers (`mepc`, `mcause`, `mtval`, GPRs) to UART for post-mortem diagnostics before initiating a controlled software reset.
- **Ref:** ESP32-C6 TRM Chapter 13.5 (TIMG WDT), Chapter 14.3 (RTC WDT).

---

## 7. Zero-Copy DMA Buffer Chains for High-Throughput I/O
- **Constraint:** Zero in-memory `memcpy` on high-bandwidth communication streams.
- **Implementation:**
  - GDMA, Wi-Fi MAC, and 802.15.4 controllers interact directly with linked circular DMA descriptor rings in SRAM.
  - Packet payloads pass directly from the radio RX descriptors to the protocol parser.
- **Ref:** ESP32-C6 TRM Chapter 6 (GDMA), Chapter 29-33 (Modem/Wireless).
