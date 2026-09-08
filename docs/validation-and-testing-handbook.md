# Iron V Validation & Testing Handbook: Bare-Metal RISC-V Runtime

The Iron V validation and testing framework establishes a deterministic, dual-tier verification methodology for bare-metal execution on the Espressif ESP32-C6 RISC-V (RV32IMAC) silicon architecture. Bypassing third-party RTOS abstractions, the runtime exercises direct register-level hardware control over Harvard-partitioned SRAM, high-resolution hardware timers, multi-channel direct memory access, vectored machine traps, and access permission filters. To guarantee 24/7 continuous operational stability without memory leaks, watchdog resets, or unhandled CPU traps, validation is split into two complementary environments: host-native unit and binary inspection testing executed under host GCC and Python on the development workstation, paired with on-chip hardware diagnostic validation running directly on physical silicon over the bidirectional USB-Serial-JTAG CDC-ACM interface (/dev/ttyACM0).

This production-grade handbook serves as the authoritative operational manual and forward specification blueprint for human and AI engineering teams. It consolidates the comprehensive architectural audit of all fourteen subsystems implemented across Phase 0 through Phase 3, provides an exhaustive 100% traceability matrix with standardized tracking IDs ([AUDIT-P03-01] through [AUDIT-P03-20]), documents the hardened host test infrastructure (466 C unit test assertions in tests/test_freestanding.c and 23 static binary inspection suites in tests/test_runner.py), details interactive on-chip validation procedures, and defines concrete, task-by-task forward hardware test specifications spanning Phase 4 through Phase 7 with explicit register bitfields, stimulus vectors, timing constraints, and multi-agent delegation assignments per AGENTS.md.

---

## 1. Hardware Silicon Baseline & Physical Test Environment

The validation harness is grounded in physical silicon reality rather than simulated abstractions. All build configurations, memory maps, peripheral addresses, and test assertions are anchored directly in the physical telemetry of the connected development bench.

### 1.1 Physical Device Profile & Telemetry Grounding

Validation is executed against the official ESP32-C6-DevKitC-1 development board hosting an embedded ESP32-C6-WROOM-1 module. Hardware telemetry extracted directly from the physical device (documented in `docs/technical-docs/board-flash-id` and `docs/technical-docs/board-summary`) establishes the authoritative baseline:

* **Microcontroller Silicon:** ESP32-C6 (QFN40 package, silicon revision v0.0). Single-core 32-bit RISC-V RV32IMAC High-Performance (HP) CPU operating at 160 MHz (derived from 480 MHz SPLL via PCR clock distribution), paired with a 32-bit RISC-V Low-Power (LP) coprocessor operating at 20 MHz.
* **External Flash Geometry:** 8 MB (64 Mbit) High-Speed SPI NOR Flash (GigaDevice GD25Q64E, Manufacturer ID `0xC8`, Device ID `0x4017`). Configured in Dual I/O (DIO) mode at 80 MHz clock frequency (`--flash-mode dio --flash-size 8MB --flash-freq 80m`). Bounded address space spans `0x42000000` to `0x427FFFFF`.
* **Primary Crystal Oscillator (XTAL):** 40.000 MHz reference clock providing fundamental base timing for system PLLs, APB bus clocks, and hardware baud rate generators.
* **Harvard SRAM Partitions:** Internal 512 KB High-Performance SRAM physically segregated into Harvard instruction and data execution windows to eliminate GNU ld RWX security warnings and enforce W^X (Write XOR Execute) isolation:
  - `hp_iram` (Instruction Bus, Read/Execute): `0x40800000` to `0x4081FFFF` (128 KB capacity).
  - `hp_dram` (Data Bus, Read/Write): `0x40820000` to `0x4087FFFF` (384 KB capacity).
  - `lp_sram` (Low-Power Core Memory & Retention Mailbox): `0x50000000` to `0x50003FFF` (16 KB capacity). Retained across HP core sleep states.
  - `flash_xip` (External Flash Execute-in-Place Window): `0x42000000` to `0x423FFFFF` (4 MB mapped execution window for text and read-only data).
* **Serial & JTAG Communications Node:** On-chip USB-Serial-JTAG CDC-ACM controller mapped directly at physical base address `0x6000F000`. Hardware eFuse state (`docs/technical-docs/board-summary`) confirms `DIS_USB_SERIAL_JTAG = False`, enabling native full-speed USB telemetry without external bridge chips.
* **Device Port Assignment:** Host device node `/dev/ttyACM0` (udev symlink: `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_40:4C:CA:45:1E:14-if00`).
* **Silicon Factory Identifiers:** Factory MAC address `40:4C:CA:45:1E:14` (IEEE 802.15.4 / BLE Base MAC `40:4C:CA:FF:FE:45:1E:14`).

### 1.2 Physical Register Bases & Memory Cartography

In strict accordance with `AGENTS.md`, zero magic numbers are permitted in driver logic or validation routines. All peripheral access is conducted through standardized base address constants and parameterized accessor macros defined in `src/regs/` and `src/io_constants.h`:

| Peripheral Subsystem | Physical Base Address | Size | Header Location | TRM Citation | Functional Role in Test Hierarchy |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **UART0** | `0x60000000` | 4 KB | `src/regs/uart0.h` | TRM Ch. 27 | Primary serial shell & automated bootloader console |
| **TIMG0** | `0x60008000` | 4 KB | `src/regs/timg0.h` | TRM Ch. 14, 15 | 1 MHz hardware timer 0 & Main Watchdog (MWDT) |
| **TIMG1** | `0x60009000` | 4 KB | `src/regs/timg1.h` | TRM Ch. 14, 15 | Secondary timer group & auxiliary watchdog supervisor |
| **SYSTIMER** | `0x6000A000` | 4 KB | `src/regs/systimer.h` | TRM Ch. 13 | 16 MHz 52-bit system counter & Target 0 alarm scheduler |
| **USB_DEVICE** | `0x6000F000` | 4 KB | `src/regs/usb_device.h` | TRM Ch. 32 | CDC-ACM endpoint 1 FIFO & JTAG debug registers |
| **INTERRUPT_CORE0** | `0x60010000` | 4 KB | `src/regs/interrupt_core0.h`| TRM Ch. 10 | Interrupt Matrix (INTMTX) 77-source mapping router |
| **PLIC_MX** | `0x20001000` | 4 KB | `src/regs/plic.h` | TRM Ch. 1 | Machine-mode Core PLIC priority (1-15) & threshold |
| **CLINT_M** | `0x20001800` | 4 KB | `src/regs/plic.h` | TRM Ch. 1 | Core-Local Interruptor (software interrupt trigger) |
| **GDMA** | `0x60080000` | 4 KB | `src/regs/dma.h` | TRM Ch. 4 | General DMA multi-channel descriptor engine |
| **AES** | `0x60088000` | 4 KB | SVD Hardware Map | TRM Ch. 19 | Hardware AES-128/256 ECB/CBC cryptographic engine |
| **SHA** | `0x60089000` | 4 KB | SVD Hardware Map | TRM Ch. 23 | Hardware SHA-1/224/256 hashing accelerator |
| **ECC** | `0x6008B000` | 4 KB | SVD Hardware Map | TRM Ch. 20 | Hardware ECC P-256 point multiplier accelerator |
| **IO_MUX** | `0x60090000` | 4 KB | SVD Hardware Map | TRM Ch. 7 | Pad pull-up, pull-down, and function selection |
| **GPIO** | `0x60091000` | 4 KB | SVD Hardware Map | TRM Ch. 7 | GPIO output W1TS/W1TC and input state registers |
| **PCR** | `0x60096000` | 4 KB | `src/regs/pcr.h` | TRM Ch. 8 | Power, Reset & Clock tree distribution registers |
| **HP_APM** | `0x60099000` | 4 KB | `src/regs/hp_apm.h` | TRM Ch. 16 | Access Permission Management (16-region SRAM filter) |
| **IEEE802154** | `0x600A3000` | 4 KB | `src/regs/ieee802154.h` | TRM Ch. 30 | 802.15.4 baseband transceiver & Auto-ACK control |
| **MODEM_SYSCON** | `0x600A9800` | 4 KB | `src/regs/modem_syscon.h`| TRM Ch. 8 | Wireless baseband & modem clock gating/resets |
| **MODEM_LPCON** | `0x600AF000` | 4 KB | `src/regs/modem_lpcon.h` | TRM Ch. 8 | Modem low-power & coexistence clock configuration |
| **PMU** | `0x600B0000` | 4 KB | `src/regs/pmu.h` | TRM Ch. 12 | Power Management Unit, sleep FSM & inter-core trigger |
| **LP_CLKRST** | `0x600B0400` | 4 KB | `src/regs/lp_clkrst.h` | TRM Ch. 8 | Low-power domain clock generation and resets |
| **EFUSE** | `0x600B0800` | 4 KB | SVD Hardware Map | TRM Ch. 6 | Non-volatile silicon identity, MAC & secure boot eFuses |
| **LP_AON** | `0x600B1000` | 4 KB | `src/regs/lp_aon.h` | TRM Ch. 12 | Low-power always-on storage registers (STORE0..9) |
| **LP_WDT** | `0x600B1C00` | 4 KB | `src/regs/lp_wdt.h` | TRM Ch. 15 | Super Watchdog (SWD) & RTC watchdog controller |
| **LP_PERI** | `0x600B2800` | 4 KB | `src/regs/lp_peri.h` | TRM Ch. 3, 12 | LP core clock enable, reset release & hardware TRNG |
| **LP_APM** | `0x600B3800` | 4 KB | SVD Hardware Map | TRM Ch. 16 | Low-power domain access permission controller |
| **INTPRI** | `0x600C5000` | 4 KB | `src/regs/intpri.h` | TRM Ch. 10 | CPU Interrupt Priority (1-15) & Preemption Threshold |
| **EXTMEM / CACHE**| `0x600C8000` | 4 KB | `src/regs/extmem.h` | TRM Ch. 4, 5 | L1 Cache controller & Flash MMU page mapping tables |

### 1.3 Critical Architectural Fix: USB-Serial-JTAG MMIO Base

During the Phase 0 audit, legacy references in early documentation cited `0x60043000` as the USB-Serial-JTAG base address. Verification against `docs/technical-docs/esp32c6.svd` line 69504 and TRM Chapter 5 Table 5.3-2 proved that `0x60043000` belongs to a 412 KB reserved memory hole spanning `0x60019000` to `0x6007FFFF`. Dereferencing any address within this unmapped window triggers an immediate hardware Load/Store Access Fault exception (`mcause = 5` or `mcause = 7`). 

The hardware base address is strictly verified as `0x6000F000` (`USB_DEVICE_BASE`). All register access macros in `src/regs/usb_device.h` calculate offsets from `0x6000F000`, ensuring zero bus faults during interactive console operations.

### 1.4 Host Environment & Device Permissions

To interact with the physical silicon over `/dev/ttyACM0`, the host Linux workstation environment must meet the following configuration parameters:
* **User Group Membership:** The device node `/dev/ttyACM0` is instantiated by system udev rules with ownership `root:uucp` and file mode `0660` (`crw-rw----`). The executing user (`max`) is verified as an active member of group `uucp` (`gid=984`), granting full read/write privileges.
* **Container Isolation & Sandbox Bypass Flag:** In containerized development workflows (e.g. AI-assisted subagents or sandboxed shells), standard container barriers isolate device nodes and drop serial ioctls, causing direct tool execution to fail with `recvmsg: connection reset by peer`. All tool invocations requiring access to `/dev/ttyACM0` must explicitly set the sandbox bypass parameter (`BypassSandbox: true`).

---

## 2. Phase 0 Through Phase 3 Architectural & Test Coverage Audit (R1)

An exhaustive engineering audit was performed across all 14 bare-metal subsystems developed from Phase 0 through Phase 3, validating code implementations in `src/`, memory segmentation in `ld/link.ld`, build rules in `Makefile`, and verification harnesses against official ESP32-C6 TRM specifications.

### 2.1 Subsystem Architecture & Implementation Verification

The 14 foundational subsystems operate in concert to deliver a hardened, zero-dependency bare-metal runtime:

1. **CRT0 Startup Vector & Machine-Mode Setup (`src/crt0.S`, `src/utils.h`):** Configures machine privilege mode, aligns the initial stack pointer to 16-byte boundaries at `_stack_top` (`0x40880000`), zeroes the `.bss` section in DRAM (`_sbss` to `_ebss`), initialises machine trap vector base address (`mtvec`) to the 256-byte aligned vector table in IRAM, and transfers execution to `main()`.
2. **Linker Script Harvard Segmentation & W^X Isolation (`ld/link.ld`):** Physically divides internal 512 KB HP SRAM into 128 KB `hp_iram` (`0x40800000`, flags `rx`) and 384 KB `hp_dram` (`0x40820000`, flags `rw`). Enforces zero overlapping sections, 16-byte alignment boundaries, and eliminates GNU ld RWX permission warnings. Mappings for `lp_sram` (`0x50000000`) and `flash_xip` (`0x42000000`) provide dedicated execution and retention cartography.
3. **Power, Reset & Clock Tree Distribution (`src/clock.c`, `src/regs/pcr.h`):** Programs the Power, Clock, and Reset (PCR) registers to derive the 160 MHz CPU clock from the 480 MHz SPLL (`PCR_CPU_FREQ_CONF_REG`), selects the 40 MHz XTAL for peripheral clocks, and locks the APB/AHB bus frequency at 40 MHz. Baud rate dividers for UART0 and SYSTIMER tick frequencies remain precisely synchronized.
4. **Multi-Tier Active Watchdog Supervisor (`src/wdt.c`, `src/regs/timg0.h`):** Controls the Main Watchdog Timer (MWDT) in Timer Group 0 with a 5000 ms timeout ceiling and a 500 ms periodic feed interval. Enforces an anti-spamming epoch guard (rejecting >20 feeds within a single 1-second window) and exposes `wdt_supervisor_tick()` to continuously verify system liveness.
5. **RISC-V Vectored Trap Pipeline & Crash Dump (`src/trap_entry.S`, `src/trap.c`, `src/panic.c`):** Houses a 256-byte aligned vector table in IRAM (`_vector_table`). Exception entry 0 saves all 32 integer registers into a 128-byte `trapframe_t` on the stack, preserves CSRs (`mstatus`, `mepc`, `mcause`, `mtval`), dispatches C exception handlers, and issues structured hexadecimal panic dumps before system halt.
6. **Interrupt Matrix (INTMTX) & Core PLIC / INTPRI Preemption (`src/interrupt.c`, `src/regs/plic.h`, `src/regs/interrupt_core0.h`):** Maps 77 peripheral interrupt sources to 32 CPU interrupt channels. Configures 15-level priority preemption and threshold filtering via `PLIC_MXINT_PRI_REG(chan)` and `PLIC_MXINT_THRESH_REG`, enabling critical ISRs to preempt lower-priority routines.
7. **Lock-Free Single-Producer Single-Consumer (SPSC) DPC Queue (`src/dpc.c`, `src/dpc.h`):** Provides a deterministic 64-entry circular ring buffer for deferring bottom-half ISR callbacks into thread context. Atomic load/store instructions with memory barriers guarantee thread safety between interrupt producer and scheduler consumer without disabling global interrupts.
8. **USB-Serial-JTAG CDC-ACM Hardware Driver (`src/usb_serial.c`, `src/regs/usb_device.h`):** Implements bidirectional communication over Endpoint 1 at `0x6000F000`. Features cycle-bounded non-blocking polling loops (default 320,000 CPU cycles) to prevent system lockups when the physical USB host is disconnected or terminal buffers stall.
9. **Non-Blocking Interrupt-Driven UART0 & Unified Dual-Console Layer (`src/uart.c`, `src/console.c`, `src/console.h`):** Implements UART0 with a 256-byte circular RX ring buffer and polling TX. The dual-console multiplexer dispatches output across both UART0 and USB CDC-ACM simultaneously while polling inputs non-blockingly with ANSI line-editing and backspace support.
10. **Hardware Periodic Timer (TIMG0 T0) Subsystem (`src/timer.c`, `src/regs/timg0.h`):** Configures Timer Group 0 Timer 0 with a 40-divider prescaler from the 40 MHz XTAL to produce a 1 MHz (1 us) counter base. Programs auto-reload alarms to trigger 10-second periodic system ticks, dispatching DPC events and feeding the watchdog supervisor.
11. **Deterministic Static Arena Memory Allocator (`src/arena.c`, `src/arena.h`):** Completely replaces dynamic heap allocation (`malloc`/`free`) with O(1) deterministic static pools: a Small Pool (32 blocks x 64 bytes), a Medium Pool (16 blocks x 256 bytes), and an 8 KB Linear Scratch Arena with mark/reset semantics. Guarantees zero heap fragmentation.
12. **High-Resolution 64-Bit SYSTIMER & Event Engine (`src/systimer.c`, `src/regs/systimer.h`):** Utilizes the 16 MHz hardware counter in SYSTIMER Unit 0. Implements atomic 52-bit latching (`SYSTIMER_UNIT0_OP_REG`), microsecond/millisecond conversion helpers, and Target 0 comparator alarms routed via INTMTX to CPU interrupt channel 7.
13. **Cooperative Coroutine Task Engine & Callee-Saved Scheduler (`src/task.c`, `src/task.h`, `src/task_switch.S`):** Implements an 8-task cooperative scheduler with static stack allocation (2 KB per task). Task switching preserves RISC-V callee-saved registers (`ra`, `sp`, `s0-s11`) via assembly context routines, achieving determinism with microsecond-level dispatch latency.
14. **RISC-V Physical Memory Protection (PMP) & HP_APM Fault Isolation (`src/pmp.c`, `src/pmp.h`, `src/regs/hp_apm.h`):** Manages 4 hardware PMP regions supporting NAPOT (Naturally Aligned Power-of-Two) and TOR (Top-of-Range) addressing modes. Configures High-Performance Access Permission Management (HP_APM) 16-region filters to restrict master bus authorities over internal SRAM.

### 2.2 Formal Traceability Matrix: Phase 0 Through Phase 3

The following traceability matrix covers 100% of discrete roadmap tasks across Phases 0, 1, 2, and 3, auditing each against TRM specifications, implemented symbols, current test coverage, missing edge cases, and standardized audit tracking IDs ([AUDIT-P03-01] through [AUDIT-P03-20]):

| Tracking ID | Roadmap Task | Subsystem | Specification Source | Implemented Files & Key Symbols | Architectural Alignment | Current Test Coverage | Cross-Subsystem Gaps & Hardening Required |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **[AUDIT-P03-01]** | Task 0.1 | Silicon Baseline | TRM Ch. 5, `board-flash-id` | `Makefile:30`, `ld/link.ld` | 8 MB GD25Q64E DIO @ 80 MHz, Harvard SRAM | `test_runner.py` (T1, T2, T6, T7, T8) | Direct test of 8 MB flash boundary access above 4 MB; verify SPI0 MSPI cache hit above `0x42400000`. |
| **[AUDIT-P03-02]** | Task 0.2 | Freestanding Utils | ISO C99, RV32 ABI | `src/string.c`, `src/utils.c` | Zero stdlib, freestanding string / hex parser | `test_freestanding.c`, `test.c` (T8) | Negative integer parsing in `s_itoa`, hex strings with `0x00000000`, buffer overflow truncation safety bounds. |
| **[AUDIT-P03-03]** | Task 0.3 | Memory Guarding | TRM Tab 5.3-1, Tab 5.3-2 | `src/test.c:check_mem_access()` | Rejects reserved holes; classifies SRAM, LP, MMIO | `test.c` (T3, T4, T5, T6, T7, T9) | Verify rejection of reserved holes `0x60019000-0x6007FFFF` and `0x6009A000-0x600AFFFF` explicitly. |
| **[AUDIT-P03-04]** | Task 1.1 | Flash & Memory Map | TRM Ch. 5, Ch. 4 | `ld/link.ld` (`hp_iram`, `hp_dram`, `lp_sram`, `flash_xip`) | Harvard IRAM/DRAM split; W^X safety enforced | `test_runner.py` (T1, T6, T7), `test.c` (T1, T2, T13) | Retention test of LP SRAM (`0x50000000`) across soft reset; multi-word pattern integrity across 16 KB range. |
| **[AUDIT-P03-05]** | Task 1.2 | CRT0 Startup & Stack | TRM Ch. 1, Ch. 9 | `src/crt0.S`, `src/utils.h` | 16-byte stack alignment, BSS zeroed, `mtvec` set | `test_runner.py` (T10, T11), `test.c` (T4, T10) | Deep recursion stack exhaustion watermark test; verify stack pointer margin check when remaining headroom < 4 KB. |
| **[AUDIT-P03-06]** | Task 1.3 | PCR Clock Tree | TRM Ch. 8 (Tab 8.2-1/2) | `src/clock.c`, `src/regs/pcr.h` | 160 MHz CPU PLL, 40 MHz APB (hardware limit) | `test_runner.py` (T12), `test.c` (T11) | Dynamic CPU frequency switching (XTAL 40 MHz to PLL 160 MHz) under live UART transmission; verify baud consistency. |
| **[AUDIT-P03-07]** | Task 1.4 | Watchdog Supervisor | TRM Ch. 14, 15 | `src/wdt.c`, `src/regs/timg0.h` | TIMG0 MWDT 5000 ms, 500 ms feed, 1 s window epoch | `test_runner.py` (T13), `test.c` (T12) | Watchdog starvation recovery test; anti-spamming feed ceiling rejection test; hardware reset cause register query test. |
| **[AUDIT-P03-08]** | Task 2.1 | RISC-V Trap Handler | TRM Ch. 1 (§1.6) | `src/trap_entry.S`, `src/trap.c`, `src/panic.c` | 256-byte aligned vector table, 128-byte context frame | `test_runner.py` (T14), `test.c` (T14, T15) | Illegal instruction trap (`mcause = 2`), misaligned load trap (`mcause = 4`), and nested interrupt trap handling. |
| **[AUDIT-P03-09]** | Task 2.2 | Interrupt Matrix / PLIC | TRM Ch. 10, Ch. 1 | `src/interrupt.c`, `src/regs/plic.h` | INTMTX 77 sources to 32 channels, 1-15 priorities | `test_runner.py` (T15), `test.c` (T16) | Priority preemption test: high-priority ISR preempting lower priority ISR; threshold masking of lower-priority interrupts. |
| **[AUDIT-P03-10]** | Task 2.3 | Lock-Free SPSC DPC | RISC-V A Extension | `src/dpc.c`, `src/dpc.h` | 64-entry ring buffer, atomic head/tail pointers | `test_runner.py` (T16), `test_freestanding.c`, `test.c` (T17) | Multi-producer stress race test (simulating ISR re-entrancy); burst saturation test with 1000 events and drop counter verification. |
| **[AUDIT-P03-11]** | Task 2.4 | USB-Serial-JTAG CDC | TRM Ch. 32 (`0x6000F000`)| `src/usb_serial.c`, `src/regs/usb_device.h` | Base `0x6000F000`, 64B FIFO, cycle-bounded timeout | `test_runner.py` (T17), `test.c` (T18) | Host disconnect simulation: transmit 10 KB while buffer stalls; verify bounded return without CPU freeze. |
| **[AUDIT-P03-12]** | Task 2.5 | Dual Console Multiplexer | TRM Ch. 27, Ch. 32 | `src/uart.c`, `src/console.c` | Dual-port output, UART0 RX ringbuffer, non-blocking | `test_runner.py` (T18), `test_freestanding.c`, `test.c` (T19) | Line editing & backspace processing test; input multiplexing priority when both UART and USB provide data simultaneously. |
| **[AUDIT-P03-13]** | Task 2.6 | Periodic Timer (TIMG0) | TRM Ch. 14 (§14.1) | `src/timer.c`, `src/regs/timg0.h` | 1 MHz tick base, auto-reload, 10 s interrupt | `test_runner.py` (T19), `test.c` (T20) | High-rate timer stress (1 ms interval); verify timer ISR accurately enqueues DPC without dropped ticks. |
| **[AUDIT-P03-14]** | Task 3.1 | Static Arena Allocator | TRM Ch. 5 (§5.3) | `src/arena.c`, `src/arena.h` | 32x64B small pool, 16x256B medium pool, 8KB scratch | `test_runner.py` (T20), `test_freestanding.c`, `test.c` (T21) | Concurrent multi-coroutine allocation from small pool until exhaustion; verify failure handling and out-of-order frees. |
| **[AUDIT-P03-15]** | Task 3.2 | High-Res SYSTIMER | TRM Ch. 13 (§13.3) | `src/systimer.c`, `src/regs/systimer.h` | 16 MHz tick base, 52-bit counter, Target 0 alarms | `test_runner.py` (T21), `test_freestanding.c`, `test.c` (T22) | Microsecond alarm jitter measurement; rapid cancellation and rescheduling of pending oneshot alarms. |
| **[AUDIT-P03-16]** | Task 3.3 | Coroutine Task Engine | RV32 Callee-Saved ABI | `src/task.c`, `src/task_switch.S` | 8 static TCBs, 2 KB stacks, `task_yield()` exchange | `test_runner.py` (T22), `test_freestanding.c`, `test.c` (T23) | Host-executable cooperative scheduling loop test; task termination (`task_exit()`) lifecycle; stack overflow canary check. |
| **[AUDIT-P03-17]** | Task 3.4 | RISC-V PMP & HP_APM | TRM Ch. 1 (§1.8), Ch. 16 | `src/pmp.c`, `src/regs/hp_apm.h` | 4 PMP regions (NAPOT/TOR), 16 APM regions | `test_runner.py` (T23), `test_freestanding.c`, `test.c` (T24) | PMP TOR contiguous boundary configuration (Region 1 start = Region 0 end); APM dynamic filter reconfiguration fault checks. |
| **[AUDIT-P03-18]** | Integration 1| Coroutine + SYSTIMER + DPC | TRM Ch. 1, 10, 13 | `src/task.c`, `src/systimer.c`, `src/dpc.c` | Tasks yield cooperatively while SYSTIMER alarms fire | Partially in `src/main.c` background loop | Unit test simulating coroutine context switch during active DPC queue draining and timer alarm dispatch. Resolved in Milestone 2. |
| **[AUDIT-P03-19]** | Integration 2| Arena Exhaustion under Tasks | Architecture Spec | `src/task.c`, `src/arena.c` | Coroutines allocating static arena memory safely | Minimal pool exhaustion in `test_freestanding.c` | Multi-task race simulation where Task A exhausts pool and Task B handles allocation failure gracefully. Resolved in Milestone 2. |
| **[AUDIT-P03-20]** | Integration 3| PMP TOR Boundaries & APM | TRM Ch. 1, Ch. 16 | `src/pmp.c`, `src/trap.c` | PMP TOR matching across SRAM and APM master filter | Mock CSR tests in `test_freestanding.c` | Back-to-back TOR regions (Region 0, Region 1, Region 2); testing invalid address ranges and master permission bits. Resolved in Milestone 2. |

### 2.3 Catalog of Subsystem Edge Cases & Failure Scenarios

A rigorous embedded runtime must specify exact behavior under boundary violations, resource exhaustion, and asynchronous preemption. The following 23 edge cases represent the validated fault-handling matrix:

1. **Memory Guard on Legacy USB Address:** Dereferencing `0x60043000` (legacy ESP32-S3 USB base) falls in the ESP32-C6 412 KB reserved hole (`0x60019000` to `0x6007FFFF`). Triggers hardware Load/Store Access Fault (`mcause = 5` or `7`). `check_mem_access()` rejects it as invalid.
2. **Misaligned 32-Bit Memory Access:** Unaligned 32-bit load/store to `0x40820001` is rejected by `check_mem_access()`. On hardware, triggers Load/Store Address Misaligned exception (`mcause = 4` or `6`).
3. **Read-Only Data Section Mutation:** Write attempt to `.rodata` (`0x40820000` to `0x40820080`) is classified as `MEM_ACCESS_READONLY` by `check_mem_access()`, rejecting mutation.
4. **DPC Ring Buffer Full Saturation:** Attempting to enqueue a 65th event into the 64-capacity SPSC ring buffer returns `DPC_STATUS_ERR_FULL`, increments `drop_count` to 1, and preserves head/tail indices without memory corruption.
5. **DPC Dequeue Under Empty Queue:** Dequeue attempt when `head == tail` returns `DPC_STATUS_ERR_EMPTY`, leaving target event structure unmodified.
6. **Small Arena Pool Exhaustion:** Requesting a 33rd block from the 32-block Small Pool returns `NULL`. Clamps `active_count` at 32, retains `allocated_mask` at `0xFFFFFFFF`, and avoids falling back to dynamic allocation.
7. **Arena Double-Free Guard:** Calling `arena_free()` twice on block 15 succeeds on the first invocation and detects that bit 15 is 0 in `allocated_mask` on the second, returning `ARENA_FREE_FAIL` (0).
8. **Arena Misaligned Free Pointer:** Passing an unaligned pointer (`ptr + 1`) to `arena_free()` is rejected by offset alignment checks (`offset % block_size != 0`), returning `ARENA_FREE_FAIL`.
9. **Arena Foreign Pointer Free Guard:** Passing a pointer outside pool boundaries (e.g. stack or MMIO address) fails boundary validation `(ptr < memory || ptr >= memory + total_size)`, returning `ARENA_FREE_FAIL`.
10. **Linear Scratch Arena Capacity Exhaustion:** Requesting scratch memory exceeding remaining capacity returns `NULL`, leaving the allocation offset and high-watermark untouched.
11. **Linear Scratch Integer Wrap Guard:** Requesting allocation size with integer wrap (`(size_t)-1` or `0xFFFFFFFF`) triggers overflow guard `(offset + aligned_size < offset)`, returning `NULL`.
12. **Linear Scratch Unaligned Mark Reset:** Attempting to reset scratch arena with an unaligned marker (`reset(3)`) fails the alignment check `(mark & WORD_ALIGN_MASK != 0)`, ignoring the reset request.
13. **Linear Scratch Forward Reset Rejection:** Attempting to reset to a mark beyond the current allocation offset (`reset(current + 64)`) triggers the forward-reset guard `(mark > scratch.offset)`, rejecting the operation to prevent uninitialized memory access.
14. **PMP NAPOT Window Length Violation:** Configuring NAPOT mode with length smaller than 8 bytes (e.g. 4 bytes) returns `PMP_ERR_INVALID_LEN` per RISC-V privileged architecture rules.
15. **PMP NAPOT Non-Power-of-Two Window:** Configuring NAPOT mode with non-power-of-two length (e.g. 100 bytes) returns `PMP_ERR_INVALID_LEN`.
16. **PMP NAPOT Base Address Misalignment:** Configuring NAPOT mode with base address unaligned to region length (e.g. base `0x40800004`, len 64) returns `PMP_ERR_INVALID_ALIGN`.
17. **PMP TOR Non-Zero Region 0 Base:** Configuring Top-of-Range (TOR) on Region 0 with a non-zero start address returns `PMP_ERR_INVALID_ADDR` because Region 0 uses address 0 as its implicit lower boundary.
18. **HP_APM Reserved Region 0 Gating:** Attempting to disable or clear permissions on APM Region 0 returns `APM_ERR_RESERVED_REGION`. Region 0 is reserved by hardware as default pass-through and cannot be disabled.
19. **USB Host Disconnect Transmission Guard:** Calling `usb_serial_write()` when the host is disconnected or Endpoint 1 FIFO is full decrements the 320,000-cycle timeout counter, aborts transmission, increments `tx_dropped_bytes`, and returns without locking the CPU.
20. **UART0 RX Ring Buffer Influx Overflow:** Rapid burst transmission exceeding the 256-byte RX buffer detects `(head + 1) % 256 == tail`, increments `overflow_count`, preserves oldest unread bytes, and drops the newest byte.
21. **Watchdog Anti-Spam Ceiling:** Calling `wdt_feed()` more than 20 times within a single 1-second window executes the hardware reload but flags an anti-spam warning, preventing runaway loops from masking deadlocks.
22. **Coroutine Task Exit Transition:** Calling `task_exit()` transitions task state to `TASK_STATE_TERMINATED`, automatically yields the CPU via `task_yield()`, and ensures the scheduler never switches back to the terminated stack frame.
23. **Coroutine Capacity Exhaustion:** Attempting to create a 9th task when 8 are active returns `TASK_ERR_FULL`, preserving existing TCBs intact.

---

## 3. Host Unit & Cross-Subsystem Integration Test Hardening (R2)

To satisfy Requirement R2 and resolve the integration gaps identified in `[AUDIT-P03-18]`, `[AUDIT-P03-19]`, and `[AUDIT-P03-20]`, the host test harness was hardened on dedicated feature branch `feat/phase0-3-test-hardening`.

### 3.1 Host Test Architecture Overview

Host-tier testing executes natively on the developer workstation under Linux GCC without requiring active silicon or hardware emulators. It comprises two complementary suites:
* **Freestanding C Unit Test Suite (`tests/test_freestanding.c`):** Compiled with host GCC using freestanding flags (`-fno-tree-loop-distribute-patterns -Wall -Wextra -Werror`), directly linking C source modules (`src/string.c`, `src/dpc.c`, `src/arena.c`, `src/pmp.c`).
* **ELF Inspection & Memory Map Test Runner (`tests/test_runner.py`):** Direct 32-bit ELF parsing script that unpacks section headers, program segments, and symbol tables to mathematically verify firmware geometry against linker script rules.

### 3.2 Breakdown of `tests/test_freestanding.c` (466 Total Assertions)

In Milestone 2, `tests/test_freestanding.c` was expanded from 253 baseline assertions to **466 total assertions** (a net addition of 213 assertions) with zero regressions across the original 14 test suites. Five major hardened integration suites were implemented:

#### 1. Coroutine Task Yielding under Concurrent SYSTIMER Alarms & DPC Callbacks
* **Function:** `test_coroutine_systimer_dpc_integration()`
* **Assertions:** 32 assertions
* **Traceability ID:** `[AUDIT-P03-18]`
* **Test Mechanics:** Models two cooperative worker coroutines (`Task A` and `Task B`) yielding execution back and forth while simulated hardware SYSTIMER alarms trigger periodic interrupts. The interrupt handlers enqueue structured DPC callbacks into the lock-free SPSC ring buffer. A consumer task invokes `dpc_process_all()` to drain and execute callbacks.
* **Verified Properties:**
  - Strict FIFO order preserved across 10 interleaved DPC events.
  - Callback arguments match exact magic constants (`0xAAAA0000 | round` and `0xBBBB0000 | round`).
  - Zero dropped events (`dpc_get_drop_count() == 0`).
  - Coroutine state transitions (`READY` to `RUNNING` to `READY` to `TERMINATED`) complete deterministically.

#### 2. Static Arena Pool Exhaustion & Multi-Task Allocation Contention
* **Function:** `test_arena_concurrency_exhaustion()`
* **Assertions:** 77 assertions
* **Traceability ID:** `[AUDIT-P03-19]`
* **Test Mechanics:** Simulates two concurrent tasks contending for blocks from the 32-block Small Pool (64 bytes) and 16-block Medium Pool (256 bytes). Task 1 allocates 20 blocks; Task 2 allocates 12 blocks, completely exhausting the pool.
* **Verified Properties:**
  - 33rd allocation attempt returns `NULL` with `active_count == 32` and `allocated_mask == 0xFFFFFFFF`.
  - Task 1 frees an interleaved subset (blocks 0, 4, 8, 12, 16); Task 2 immediately reallocates and reuses those specific freed blocks.
  - Double-free attempts return `ARENA_FREE_FAIL`.
  - Foreign pointers (stack address) and unaligned pointers (`ptr + 1`) are safely rejected.
  - Linear scratch arena validates 4-byte alignment, forward reset rejection, unaligned reset rejection, and integer wrap overflow protection.

#### 3. PMP Chained Top-of-Range (TOR) Boundary Edge Cases & Locked Regions
* **Function:** `test_pmp_chained_tor_and_locked_regions()`
* **Assertions:** 39 assertions
* **Traceability ID:** `[AUDIT-P03-20]`
* **Test Mechanics:** Configures contiguous chained Top-of-Range (TOR) windows across multiple entries: Region 0 [0, `0x1000`), Region 1 [`0x1000`, `0x5000`), and Region 2 [`0x5000`, `0x10000`).
* **Verified Properties:**
  - Readback via `pmp_get_region()` accurately decodes chained start and end boundaries.
  - Non-contiguous TOR boundaries (where Region 1 start does not equal Region 0 end) are rejected with `PMP_ERR_INVALID_ADDR`.
  - Zero-length TOR regions (`start_addr == end_addr`) are handled cleanly.
  - Locked region enforcement: setting `PMP_CFG_L_BIT` locks configuration; subsequent calls to `pmp_set_region()` or `pmp_disable_region()` return `PMP_ERR_LOCKED`.
  - Re-initialization via `pmp_init()` preserves locked entries without modification.

#### 4. HP_APM Dynamic Filter Reconfigurations & Exception Status
* **Function:** `test_apm_dynamic_reconfiguration()`
* **Assertions:** 31 assertions
* **Traceability ID:** `[AUDIT-P03-20]`
* **Test Mechanics:** Validates runtime reconfiguration of Access Permission Management (APM) filters across masters 0 through 3.
* **Verified Properties:**
  - Reserved Region 0 protection: disabling or altering Region 0 returns `APM_ERR_RESERVED_REGION`.
  - Rejection of inverted address boundaries (`start_addr > end_addr`) returning `APM_ERR_INVALID_ADDR`.
  - Dynamic update of regions 1 through 15, mutating boundary ranges and altering permissions from Read/Write to Read-Only.
  - Enabling/disabling master filters 0 through 3; rejecting invalid master indices (`>= 4`).
  - Reading and clearing simulated master exception status registers (`HP_APM_M_STATUS_REG`).

#### 5. Console Line Reader Edge Cases
* **Function:** `test_console_line_reader_edge_cases()`
* **Assertions:** 34 assertions
* **Traceability ID:** `[AUDIT-P03-12]`
* **Test Mechanics:** Feeds complex ASCII input streams into `console_read_line_nonblocking()` to test ANSI line editing.
* **Verified Properties:**
  - Standalone LF (`\n`), standalone CR (`\r`), and CRLF (`\r\n`) sequences.
  - Consecutive commands where trailing `\n` is consumed without generating phantom blank lines.
  - Backspace (`\b` = 0x08) and DEL (`\x7F`) character erasure, verifying cursor decrements and underflow protection on empty lines.
  - Line buffer overflow truncation: strings exceeding 127 characters are capped at `CONSOLE_MAX_LINE_LEN - 1` without buffer overrun.
  - Small destination buffer protection (`max_len` parameter bounding).

### 3.3 Static Binary Inspection Suites in `tests/test_runner.py` (23 Suites)

The Python test runner parses the compiled RISC-V ELF binary (`firmware.elf`) and the flash binary image (`firmware.bin`), executing 23 comprehensive inspection test suites:

* **[TEST 01] Section Topology & Monotonicity:** Unpacks ELF section headers to assert strictly increasing VMA addresses without overlapping sections.
* **[TEST 02] 16-Byte Section Alignment Verification:** Asserts that every loaded section (`.text`, `.rodata`, `.data`, `.bss`) is aligned to a 16-byte boundary (`addr % 16 == 0`).
* **[TEST 03] RW Data Section Static Initial Value:** Unpacks 4 bytes of `g_test_data_var` from the `.data` section in DRAM, asserting initial value `0x12345678`.
* **[TEST 04] BSS Section Allocation & SHT_NOBITS:** Verifies `g_test_bss_var` is placed in an `SHT_NOBITS` section with flags `SHF_ALLOC | SHF_WRITE`.
* **[TEST 05] Read-Only Data (RODATA) Content & Flags:** Asserts `.rodata` has `SHF_ALLOC` without `SHF_WRITE`, and validates the string `"IRON_V_RODATA_TEST_PATTERN"`.
* **[TEST 06] Harvard Segment Isolation & W^X Permission Safety:** Asserts that exactly 0 RWX segments exist in program headers, and that `hp_iram` (`0x40800000`, `RX`) is segregated from `hp_dram` (`0x40820000`, `RW`).
* **[TEST 07] External Flash XIP Section Allocation:** Asserts `.flash_xip` exists, is marked `SHT_PROGBITS`, and is mapped to `0x42000000`.
* **[TEST 08] Flash Binary Image Geometry Validation:** Inspects `firmware.bin` header for Espressif magic byte `0xE9`, 3 valid segments, entry at `0x40800000`, and FlashSizeCode `0x3` (8 MB DIO @ 80 MHz).
* **[TEST 09] Host-Native Freestanding C Unit Test Suite Execution:** Compiles and executes `tests/test_freestanding`, asserting exit code 0 and 0 failures across all 466 assertions.
* **[TEST 10] Memory Cartography Symbols Validation:** Asserts `_vector_table` is 256-byte aligned in IRAM, `_lp_sram_start == 0x50000000`, and `_flash_text_start == 0x42000000`.
* **[TEST 11] Stack Boundary Geometry & CRT0 Entry:** Asserts `_stack_top == 0x40880000` with 16-byte alignment, >=64 KB headroom above `.bss` (actual headroom: 336 KB), and entry point at `_start` (`0x40800000`).
* **[TEST 12] PCR Clock Subsystem Linkage:** Asserts `clock_init`, `clock_get_config`, and related symbols reside in IRAM text (`0x40800000` to `0x40820000`).
* **[TEST 13] Watchdog Supervisor Linkage:** Asserts `wdt_init`, `wdt_feed`, and `wdt_supervisor_tick` reside in IRAM executable memory.
* **[TEST 14] Trap Handler & Vector Table Subsystem Linkage:** Asserts `trap_init`, `trap_handler`, `panic_dump`, and 256-byte aligned `_vector_table` reside in IRAM.
* **[TEST 15] Interrupt Matrix (INTMTX) & INTPRI Linkage:** Asserts `interrupt_init`, `interrupt_route`, and threshold functions reside in IRAM.
* **[TEST 16] Lock-Free SPSC DPC Queue Engine Linkage:** Asserts `dpc_init`, `dpc_enqueue`, and `dpc_process` symbols reside in IRAM.
* **[TEST 17] USB-Serial-JTAG CDC-ACM Driver Linkage:** Asserts `usb_serial_init`, `usb_serial_write`, and register structures reside in IRAM.
* **[TEST 18] Dual-Console Multiplexer Linkage:** Asserts `uart_init`, `console_init`, and dispatch functions reside in IRAM.
* **[TEST 19] Hardware Periodic Timer (TIMG0 T0) Driver Linkage:** Asserts `timer_init`, `timer_start`, and timer ISR symbols reside in IRAM.
* **[TEST 20] Deterministic Static Arena Allocator Linkage:** Asserts `arena_init`, `arena_alloc`, and scratch functions reside in IRAM.
* **[TEST 21] High-Resolution SYSTIMER Driver Linkage:** Asserts `systimer_init`, `systimer_get_ticks`, and alarm functions reside in IRAM.
* **[TEST 22] Cooperative Coroutine Task Engine Linkage:** Asserts `task_init`, `task_create`, `task_yield`, and `task_switch_asm` reside in IRAM.
* **[TEST 23] RISC-V PMP & APM Fault Isolation Linkage:** Asserts `pmp_init`, `pmp_set_region`, and APM driver symbols reside in IRAM.

### 3.4 Step-by-Step Instructions: Running `make do-test`

To execute the complete host validation suite:

1. **Toolchain Verification:** Ensure host GCC (`gcc`), Python 3 (`python3`), and the RISC-V cross-compiler (`riscv64-unknown-elf-gcc`) are in the active system path:
   ```bash
   gcc --version
   python3 --version
   riscv64-unknown-elf-gcc --version
   ```
2. **Execute Full Host Test Target:** Run the automated build and test pipeline from the project root:
   ```bash
   make do-test
   ```
3. **Execution Pipeline:**
   - Compiles `firmware.elf` with `-O2 -Wall -Wextra -Werror` using `riscv64-unknown-elf-gcc`.
   - Converts `firmware.elf` to `firmware.bin` via `esptool elf2image --flash-mode dio --flash-size 8MB --flash-freq 80m`.
   - Compiles `tests/test_freestanding.c` linking `src/string.c`, `src/dpc.c`, `src/arena.c`, and `src/pmp.c` using host `gcc`.
   - Executes `./tests/test_freestanding` (466 assertions).
   - Executes `python3 tests/test_runner.py` (23 inspection suites).
4. **Expected Output:**
   ```text
   ======================================================================
           IRON V FREESTANDING RUNTIME UNIT TEST SUITE (HOST GCC)       
   ======================================================================
     [TEST] s_strlen & strlen...
     [TEST] s_strcmp & strcmp...
     ...
     [TEST] console line reader edge cases (crlf, backspace, overflow)...
   ======================================================================
     Result: ALL FREESTANDING C UNIT TESTS PASSED (0 FAILURES)
   ======================================================================
   ...
   ======================================================================
                          TEST SUITE SUMMARY                             
   ======================================================================
     Total Tests Run: 23
     Passed:          23
     Failed:          0
     Success Rate:    100%
   ======================================================================
   ```

---

## 4. Physical Hardware Silicon Test Execution Over /dev/ttyACM0 (R3)

Tier 2 verification executes compiled RISC-V machine instructions directly on the physical ESP32-C6 silicon over the USB-Serial-JTAG CDC-ACM port (`/dev/ttyACM0`).

### 4.1 Silicon Build & Flashing Procedures

Firmware compilation and flashing are automated through `Makefile` targets:

1. **Clean & Build Release Firmware:**
   ```bash
   make clean && make all
   ```
   *Expected Artifacts:* `firmware.elf` (ELF32 RISC-V) and `firmware.bin` (Espressif image).
2. **Flash to Physical Hardware:**
   ```bash
   make flash
   ```
   *Execution Mechanics:* Calls `esptool --chip esp32c6 --port /dev/ttyACM0 --baud 460800 write_flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 firmware.bin`.
   *Baud Rate Selection:* 460800 baud provides rapid, stable flash programming over the native USB JTAG/serial interface.
3. **Flashing via Model Context Protocol (MCP):**
   In AI agent environments, the `esp32-board-controller` MCP server exposes tool `flash_firmware(firmware_path="firmware.bin")`. This invokes the internal Python esptool runner with identical 8 MB DIO parameters.

### 4.2 Interactive Firmware Console Shell Documentation

Following boot or reset, the firmware initialises the dual-console multiplexer and displays the interactive shell prompt (`iron_v> `). The console is accessible via terminal monitors (`make monitor` using picocom at 115200 baud) or via MCP tool `execute_and_listen`:

```bash
picocom --baud 115200 --noreset --lower-rts --lower-dtr /dev/ttyACM0
```

The shell provides 12 interactive commands for runtime inspection and diagnostics:

#### 1. `help`
Displays the command menu and brief usage syntax.
```text
Iron V Shell Commands:
  help                - Show available commands
  info                - Show system information
  uptime              - Show high-resolution system uptime & timer telemetry
  tasks               - Show cooperative coroutine scheduler tasks & status
  pmp                 - Show RISC-V PMP & HP_APM memory protection status
  peek <hex_addr>     - Read 32-bit word from hex address
  poke <addr> <val>   - Write 32-bit hex value to address
  ecall               - Trigger controlled M-mode software trap (ECALL)
  panic               - Trigger illegal instruction exception to test panic dump
  timer [start|stop]  - Show or control periodic timer telemetry
  arena               - Show static memory arena allocation telemetry
  do-test             - Run full baseline validation test suite
iron_v> 
```

#### 2. `info`
Presents a complete system status report covering CPU frequency, memory split, flash parameters, watchdog timers, uptime, and peripheral states.
```text
========================================
 Iron V Bare-Metal RISC-V Runtime
 Target:  ESP32-C6 (RV32IMAC)
 Mode:    Bare Metal / No ESP-IDF
 CPU:     160 MHz (PLL 480M)
 APB:     40 MHz
 Memory:  HP SRAM 512KB (Harvard Split)
 Flash:   8 MB SPI NOR Flash (DIO @ 80M)
 WDT:     Active (5000 ms timeout, 1s epoch window)
 Uptime:  25 s (total feeds: 51, epoch feeds: 0)
 Timer:   Active (TIMG0 T0, 10s period, ticks: 2, DPC: 2)
 SYSTIMER: Active (16 MHz, 25s uptime, 406372439 ticks)
 Reset:   USB Serial/UART Reset [0x00000015]
 DPC:     Active (Pending: 0/64, Processed: 2, Drops: 0)
 USB:     CDC-ACM (EP1 TX Ready: 1, RX Avail: 0)
 Console: Dual Multiplexed (UART0: Active, USB: Active, Echo: ON)
 Arena:   Small 0/32 (64B), Med 0/16 (256B), Scratch 0/8192 B
 PMP/APM: PMP Active: 0/4 (pmpcfg0: 0x00000000), APM Active: 1/16
========================================
iron_v> 
```

#### 3. `uptime`
Reports high-resolution uptime derived from the 52-bit SYSTIMER counter, total ticks, and Target 0 alarm status.
```text
System Uptime: 0d 00:00:29.421717 (29s)
  Total Ticks: 0x00000000_0x1C0F095B (16 MHz tick base)
  SYSTIMER Unit 0: Active (16.0 MHz XTAL/PLL)
  Target 0 Alarm:  Inactive (firings: 0)
iron_v> 
```

#### 4. `tasks`
Displays the cooperative coroutine scheduler status, active tasks, context switch counts, and per-task stack bases, priorities, and runtimes.
```text
Cooperative Coroutine Scheduler Status:
  Active Tasks:     1/8
  Context Switches: 0
  Current Task ID:  0

 ID | Name         | State      | Pri | Stack Base | Size   | Yields | Runtime(us)
----+--------------+------------+-----+------------+--------+--------+------------
  0 | main         | RUNNING    | 10  | 0x40820000 | 393216 B | 0 | 0
iron_v> 
```

#### 5. `pmp`
Inspects machine CSRs `pmpcfg0` and `pmpaddr0..3`, decoding active PMP regions (NAPOT, TOR, NA4), permissions (R/W/X), and HP_APM filter enable masks.
```text
RISC-V Physical Memory Protection (PMP) Status:
  pmpcfg0: 0x00000000 (Active Regions: 0/4)

 Region | Mode  | R | W | X | L | Base Addr  | Length   | Raw pmpaddr
--------+-------+---+---+---+---+------------+----------+------------
   0    | OFF   | 0 | 0 | 0 | 0 | 0x00000000 | 0x00000000 | 0x00000000
   1    | OFF   | 0 | 0 | 0 | 0 | 0x00000000 | 0x00000000 | 0x00000000
   2    | OFF   | 0 | 0 | 0 | 0 | 0x00000000 | 0x00000000 | 0x00000000
   3    | OFF   | 0 | 0 | 0 | 0 | 0x00000000 | 0x00000000 | 0x00000000

HP Access Permission Management (APM) Status:
  Filter Enable Mask: 0x00000001 (Active Regions: 1/16)
  Func Control Mask:  0x0000000F (M0: 1, M1: 1, M2: 1, M3: 1)
iron_v> 
```

#### 6. `arena`
Displays deterministic static memory allocation telemetry across the Small Pool, Medium Pool, and Linear Scratch Arena.
```text
Static Memory Arena Telemetry:
  Small Pool:   32 blocks x 64 B (2048 B total)
    Active:     0/32 (Peak: 0)
    Bitmask:    0x00000000
    Allocs:     0, Frees: 0
  Medium Pool:  16 blocks x 256 B (4096 B total)
    Active:     0/16 (Peak: 0)
    Bitmask:    0x00000000
    Allocs:     0, Frees: 0
  Scratch:      Linear Arena (8192 B capacity)
    Offset:     0/8192 B (Peak: 0 B)
    Allocs:     0, Resets: 0
iron_v> 
```

#### 7. `timer [start|stop]`
Queries or toggles the TIMG0 Timer 0 periodic hardware timer.
```text
TIMG0 Timer 0 Telemetry:
  State:       Active (Running)
  Period:      10000 ms (10.0 s)
  ISR Fired:   3 times
  DPC Fired:   3 times
  Base Clock:  1 MHz (Prescaler: 40)
iron_v> 
```

#### 8. `peek <hex_addr>` & `poke <addr> <val>`
Reads or writes a 32-bit word at the specified physical address. Validates address boundaries via `check_mem_access()` before executing volatile dereference, preventing accidental bus fault exceptions.

#### 9. `ecall`
Executes an inline machine-mode `ecall` instruction to verify vectored trap handling, context frame save/restore, and clean resumption via `mret`.

#### 10. `panic`
Executes an invalid instruction (`.word 0x00000000`) in controlled fashion to verify structured hexadecimal crash dump generation across dual consoles.

#### 11. `do-test`
Executes the comprehensive automated on-chip hardware test suite (`run_validation_suite()` in `src/test.c`).

### 4.3 Silicon Diagnostic Validation Suite (`do-test`)

Executing `do-test` on physical silicon exercises 24 automated hardware test assertions:

* **[TEST 01] Memory Section Topology & Monotonicity:** Verifies linker symbols satisfy `_stext < _etext <= _srodata < _erodata <= _sdata < _edata <= _sbss < _ebss <= _stack_bottom < _stack_top`.
* **[TEST 02] 16-Byte Section Alignment:** Verifies all section boundaries are divisible by 16 (`symbol & 0xF == 0`).
* **[TEST 03] RW Data Section Mutation:** Writes patterns `0xAA55AA55` and `0x12345678` to `g_test_data_var` in DRAM, confirming volatile readback and restoring initial value.
* **[TEST 04] RW BSS Zero-Init & Mutation:** Verifies `g_test_bss_var` is initialized to 0, mutates value, and restores 0.
* **[TEST 05] Read-Only (RODATA) Protection:** Confirms `.rodata` pattern matches `"IRON_V_RODATA_TEST_PATTERN"`.
* **[TEST 06] Out-of-Bounds Address Guarding:** Asserts `check_mem_access()` rejects unmapped hole `0x60043000`, DRAM upper bound `0x40880000`, and SRAM lower bound `0x407FFFFF`.
* **[TEST 07] Misaligned Address Guarding:** Asserts `check_mem_access()` rejects unaligned 32-bit addresses `0x40820001` and `0x40820002`.
* **[TEST 08] Freestanding String & Hex Conversion Parser:** Tests `s_strlen`, `s_strcmp`, `s_strncmp`, `s_htoi` (`"0x1A2B" == 0x1A2B`), and `s_itoa`.
* **[TEST 09] Peripheral MMIO Space Accessibility:** Verifies non-faulting volatile reads to UART0 (`0x60000000`), TIMG0 (`0x60008000`), and PCR (`0x60096000`).
* **[TEST 10] Stack Bounds, Alignment & Machine CSR State:** Inspects active SP alignment (16-byte), computes remaining stack headroom, and reads `mstatus` and `mtvec`.
* **[TEST 11] PCR Clock Distribution & Frequency Status:** Verifies `PCR_SYSCLK_CONF_REG` and `PCR_CPU_FREQ_CONF_REG` confirm 160 MHz CPU PLL clock.
* **[TEST 12] Active Watchdog Supervisor & Reload Status:** Asserts TIMG0 MWDT is active, queries watchdog feed counters, and verifies anti-spam ceiling.
* **[TEST 13] Low-Power (LP) SRAM Accessibility & Retention:** Writes test patterns across LP SRAM (`0x50000000` to `0x50003FFF`), verifying non-faulting read/write retention.
* **[TEST 14] Vectored Trap Vector (`mtvec`) Alignment & Base:** Asserts `mtvec` has mode bit 1 set (Vectored Mode) and base address is 256-byte aligned in IRAM.
* **[TEST 15] Controlled ECALL Trap Execution & MRET Resume:** Executes `ecall`, confirming trap entry, incremented trap counter, and seamless resumption at `mepc + 4`.
* **[TEST 16] INTMTX Routing & PLIC Priority / Threshold Preemption:** Routes software interrupt via INTMTX, configures channel priority 10 and threshold 0, triggers interrupt, asserts ISR firing, elevates threshold to 15, and asserts interrupt masking.
* **[TEST 17] Lock-Free SPSC DPC Queue Engine:** Enqueues 64 events, asserts 65th drop, drains all 64 in strict FIFO order, and tests live callback dispatch via `dpc_process_all()`.
* **[TEST 18] USB-Serial-JTAG CDC-ACM Hardware Driver:** Verifies volatile reads to `USB_DEVICE_EP1_CONF_REG` (`0x6000F004`), non-blocking TX readiness, and timeout protection.
* **[TEST 19] Unified Dual-Console Multiplexer Subsystem:** Verifies backend dispatch structures, active port mask `0x03` (UART0 + USB), non-blocking character receive, and echo control.
* **[TEST 20] Hardware Periodic Timer (TIMG0 T0) Configuration:** Asserts TIMG0 Timer 0 is enabled, prescaled by 40 (1 MHz), auto-reloads, is routed via INTMTX channel 6 at priority 8, and advances monotonically.
* **[TEST 21] Deterministic Static Arena Allocator:** Allocates all 32 small blocks, asserts 33rd returns `NULL`, frees block 15, verifies reuse, frees all blocks, and tests linear scratch mark/reset.
* **[TEST 22] High-Resolution SYSTIMER & Event Engine:** Verifies 16 MHz Unit 0 counter monotonic increase ($T_2 > T_1$), microsecond conversion, millisecond calculation, and Target 0 alarm routing/cancellation.
* **[TEST 23] Cooperative Coroutine Task Engine & Scheduler:** Creates Task A and Task B, executes cooperative scheduling loop across 25 yields, verifies interleaved turn counter, and asserts both tasks reach `TASK_STATE_TERMINATED`.
* **[TEST 24] RISC-V PMP & APM Fault Isolation:** Configures PMP Region 0 over kernel data in User Mode Read-Only, verifies raw `pmpcfg0` bitfield matches `0x19` (`PMP_CFG_R_BIT | PMP_CFG_A_NAPOT`), checks `pmpaddr0` NAPOT address calculation, configures HP_APM Region 1 over DRAM, and cleans up test regions.

### 4.4 Zero Watchdog Fault Acceptance Criteria

For physical silicon validation to be classified as PASSED:
* **Test Pass Rate:** Exactly 24 out of 24 tests must report `[ PASS ]` (100% success rate). Zero assertion failures permitted.
* **Watchdog Trip Stability:** Zero spontaneous resets or watchdog stage timeouts during execution. `wdt_get_reset_cause()` must report clean power-on or software reset (`0x00000015` or `0x00000001`), never watchdog reset.
* **DPC Queue Drops:** Zero dropped events during normal operation (`dpc_get_drop_count() == 0`).
* **Memory Leak Immunity:** `arena` telemetry must report `active_count == 0` and `allocated_mask == 0x00000000` following test suite completion.

---

## 5. Forward-Looking Hardware Test Specifications (Phases 4 through 7) (R4)

To satisfy Requirement R4, the following section establishes an exhaustive, task-by-task hardware test specification for all 17 discrete tasks in upcoming development phases (Phases 4 through 7). Each specification details the target SVD register blocks, TRM chapter citations, key bitfields, preconditions, stimulus sequences, expected outputs, timing/concurrency constraints, pass/fail criteria, and explicit `@agent` delegation assignments adhering to `AGENTS.md`.

### 5.1 Master Forward Test Matrix (TEST 25 Through TEST 41)

| Test ID | Phase | Discrete Task | Target Subsystem | Hardware Register Block | SVD Physical Base | Primary Target Role |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **TEST 25** | Phase 4 | Task 4.1 | LP Core Coprocessor Lifecycle | `PMU`, `LP_PERI`, `LP_CLKRST` | `0x600B0000`, `0x600B2800` | `@agent:coder` |
| **TEST 26** | Phase 4 | Task 4.2 | LP SRAM Mailbox & Sleep FSM | `PMU`, `LP_AON`, LP SRAM | `0x600B0000`, `0x50003000` | `@agent:coder` |
| **TEST 27** | Phase 4 | Task 4.3 | GPIO Matrix & IO_MUX Routing | `GPIO`, `IO_MUX` | `0x60091000`, `0x60090000` | `@agent:coder` |
| **TEST 28** | Phase 4 | Task 4.4 | GDMA Multi-Channel Engine | `GDMA` | `0x60080000` | `@agent:coder` |
| **TEST 29** | Phase 5 | Task 5.1 | Modem Clock & Power Gating | `MODEM_SYSCON`, `MODEM_LPCON` | `0x600A9800`, `0x600AF000` | `@agent:coder` |
| **TEST 30** | Phase 5 | Task 5.2 | Bluetooth 5 (LE) Controller / GATT | `MODEM_SYSCON`, `INTMTX` | `0x600A9800`, `0x60010000` | `@agent:coder` |
| **TEST 31** | Phase 5 | Task 5.3 | 802.11ax Wi-Fi 6 MAC GDMA Ring | `GDMA`, `MODEM_SYSCON` | `0x60080000`, `0x600A9800` | `@agent:coder` |
| **TEST 32** | Phase 5 | Task 5.4 | IEEE 802.15.4 Radio Transceiver | `IEEE802154`, `MODEM_SYSCON` | `0x600A3000`, `0x600A9800` | `@agent:coder` |
| **TEST 33** | Phase 5 | Task 5.5 | Zero-Copy TCP/IP & Checksum Engine| HP SRAM DRAM, `SYSTIMER` | `0x40820000`, `0x6000A000` | `@agent:coder` |
| **TEST 34** | Phase 6 | Task 6.1 | Hardware Crypto & Hardware TRNG | `SHA`, `AES`, `ECC`, `RNG` | `0x60089000`, `0x60088000` | `@agent:coder` |
| **TEST 35** | Phase 6 | Task 6.2 | APM & TEE Multi-Domain Sandboxing | `HP_APM`, `TEE`, `LP_APM` | `0x60099000`, `0x60098000` | `@agent:coder` |
| **TEST 36** | Phase 6 | Task 6.3 | Watchdog Escalation & Panic Dumps | `TIMG0`, `LP_WDT`, CSRs | `0x60008000`, `0x600B1C00` | `@agent:coder` |
| **TEST 37** | Phase 6 | Task 6.4 | REST/HTTP Engine & Matter Bridge | `.flash.rodata`, `ECC`, `TCP` | `0x42800000`, `0x6008B000` | `@agent:coder` |
| **TEST 38** | Phase 7 | Task 7.1 | eFuse Memory Controller & Sealing | `EFUSE` | `0x600B0800` | `@agent:coder` |
| **TEST 39** | Phase 7 | Task 7.2 | 24/7 Soak Stability & Leak Check | `SYSTIMER`, `TIMG0`, Arenas | `0x6000A000`, `0x60008000` | `@agent:tester` |
| **TEST 40** | Phase 7 | Task 7.3 | Dual-Slot Flash OTA & Rollback | `EXTMEM / CACHE`, `SPI0` | `0x600C8000`, `0x60002000` | `@agent:coder` |
| **TEST 41** | Phase 7 | Task 7.4 | Production Hardening & Golden Seal| Full Hardware Baseline | System-Wide | `@agent:tester` |

---

### 5.2 Phase 4: Low-Power Coprocessor Lifecycle & Core Peripherals

#### Task 4.1: LP Core Coprocessor Firmware Build, Lifecycle & PMU Handshake (TEST 25)
* **Target Roles:** `@agent:architect` (coprocessor memory and startup ABI), `@agent:coder` (firmware payload and driver), `@agent:tester` (hardware lifecycle test implementation)
* **Hardware Register Blocks (SVD):** `PMU` (`0x600B0000`), `LP_PERI` (`0x600B2800`), `LP_CLKRST` (`0x600B0400`). TRM Ch. 3 (§3.1-§3.9), Ch. 12 (§12.4).
* **Key TRM Bitfields:**
  - `LP_PERI_CLK_EN_REG` (`0x600B2800 + 0x000`): bit 31 `LP_CPU_CK_EN` (1 = Enable clock).
  - `LP_PERI_RESET_EN_REG` (`0x600B2800 + 0x004`): bit 31 `LP_CPU_RESET_EN` (1 = Reset, 0 = Release).
  - `PMU_LP_CPU_PWR0_REG` (`0x600B0000 + 0x17C`): bit 29 `PMU_LP_CPU_SLP_STALL_EN`.
  - `PMU_LP_CPU_PWR1_REG` (`0x600B0000 + 0x180`): bits 15:0 `PMU_LP_CPU_WAKEUP_EN` (bit 0 = HP trigger).
  - `PMU_HP_LP_CPU_COMM_REG` (`0x600B0000 + 0x184`): bit 31 `PMU_HP_TRIGGER_LP`, bit 30 `PMU_LP_TRIGGER_HP`.
* **Preconditions:** HP core running at 160 MHz. LP SRAM (`0x50000000`) clock gate enabled in `LP_CLKRST_HP_CLK_CTRL_REG`.
* **Stimulus Inputs & Operation Sequence:**
  1. Compile minimal 32-bit RISC-V LP payload (RV32IMAC, ilp32 ABI) entry at `0x50000000`. Payload initializes `sp = 0x50003000`, writes magic word `0xCAFEBABE` to address `0x50002FFC`, sets bit 30 `PMU_LP_TRIGGER_HP` in `PMU_HP_LP_CPU_COMM_REG`, and enters low-power wait loop (`wfi`).
  2. Copy compiled binary payload into LP SRAM base address `0x50000000`.
  3. Execute memory fence instruction: `asm volatile("fence rw, rw" ::: "memory")`.
  4. Write 1 to `LP_PERI_CLK_EN_REG` bit 31 (`LP_CPU_CK_EN`).
  5. Write 0 to `LP_PERI_RESET_EN_REG` bit 31 (`LP_CPU_RESET_EN`).
  6. Pulse bit 31 (`PMU_HP_TRIGGER_LP`) in `PMU_HP_LP_CPU_COMM_REG`.
* **Expected Outputs & Hardware Telemetry:**
  - `LP_PERI_CLK_EN_REG` bit 31 reads 1.
  - `LP_PERI_RESET_EN_REG` bit 31 reads 0.
  - LP core executes instructions and writes `0xCAFEBABE` to `0x50002FFC`.
  - `PMU_HP_LP_CPU_COMM_REG` bit 30 (`PMU_LP_TRIGGER_HP`) asserts within timeout threshold.
* **Timing / Concurrency Constraints:** LP core operates at 20 MHz (8x slower than HP CPU 160 MHz). Polling loop on HP core must execute with minimum timeout threshold of 200,000 CPU cycles (1.25 ms). Memory fence required before releasing reset.
* **Pass/Fail Criteria:** Pass if `*(volatile uint32_t *)0x50002FFC == 0xCAFEBABE` and LP trigger bit asserts within timeout. Fail if value does not match or polling times out.

#### Task 4.2: LP SRAM Shared Mailbox, Retention & Deep/Light Sleep State Machine (TEST 26)
* **Target Roles:** `@agent:architect` (mailbox protocol layout), `@agent:coder` (PMU sleep and mailbox driver), `@agent:tester` (retention verification test)
* **Hardware Register Blocks (SVD):** `PMU` (`0x600B0000`), `LP_AON` (`0x600B1000`), LP SRAM (`0x50003000`). TRM Ch. 3 (§3.7), Ch. 12 (§12.4).
* **Key TRM Bitfields:**
  - `PMU_SLP_HP_PERI_CONF_REG` (`0x600B0000 + 0x018`): bit 31 `PMU_HP_PERI_PD_EN`.
  - `LP_AON_STORE0_REG` (`0x600B1000 + 0x050`): 32-bit retained scratchpad register.
  - `PMU_HP_LP_CPU_COMM_REG` (`0x600B0000 + 0x184`): bit 31 `PMU_HP_TRIGGER_LP`, bit 30 `PMU_LP_TRIGGER_HP`.
* **Preconditions:** Task 4.1 LP core running. Shared mailbox structure `lp_shared_mailbox_t` defined in `src/power.h` mapped at `0x50003000`.
* **Stimulus Inputs & Operation Sequence:**
  1. HP core writes magic header `0x49524F4E` ("IRON") to `mailbox->magic`.
  2. HP core writes command opcode `0x00000001` (`CMD_SAMPLE_TELEMETRY`) to `mailbox->hp_to_lp_cmd`.
  3. HP core writes 32-bit seed `0xDEADBEEF` to `LP_AON_STORE0_REG`.
  4. HP core pulses `PMU_HP_TRIGGER_LP` bit and awaits ACK.
  5. LP core ISR detects trigger, reads command, writes `lp_to_hp_ack = 0x00000001`, increments `wake_count`, writes simulated sensor telemetry, and asserts `PMU_LP_TRIGGER_HP`.
* **Expected Outputs & Hardware Telemetry:**
  - `mailbox->magic == 0x49524F4E`.
  - `mailbox->lp_to_hp_ack == 0x00000001`.
  - `mailbox->wake_count >= 1`.
  - `LP_AON_STORE0_REG` preserves `0xDEADBEEF` without corruption.
* **Timing / Concurrency Constraints:** Single-writer protocol enforced: HP writes only to `hp_to_lp_*` fields; LP writes only to `lp_to_hp_*` fields. Handshake response timeout bounded at 10 ms. Memory fences (`fence rw, rw`) required around mailbox reads and writes.
* **Pass/Fail Criteria:** Pass if ACK matches command opcode within 10 ms and retained LP AON scratchpad retains value.

#### Task 4.3: GPIO Matrix & IO_MUX Multi-Function Pin Routing (TEST 27)
* **Target Roles:** `@agent:architect` (pad configuration), `@agent:coder` (GPIO driver), `@agent:tester` (pin toggle test)
* **Hardware Register Blocks (SVD):** `GPIO` (`0x60091000`), `IO_MUX` (`0x60090000`). TRM Ch. 7 (§7.1-§7.9).
* **Key TRM Bitfields:**
  - `GPIO_ENABLE_W1TS_REG` (`0x60091000 + 0x024`): bit N enables output buffer.
  - `GPIO_ENABLE_W1TC_REG` (`0x60091000 + 0x028`): bit N disables output buffer.
  - `GPIO_OUT_W1TS_REG` (`0x60091000 + 0x008`): bit N sets output high.
  - `GPIO_OUT_W1TC_REG` (`0x60091000 + 0x00C`): bit N sets output low.
  - `GPIO_IN_REG` (`0x60091000 + 0x03C`): bit N reflects pad input state.
  - `IO_MUX_GPIO_N_REG` (`0x60090000 + 0x004 + 4*N`): bit 7 `FUN_WPD` (pull-down), bit 8 `FUN_WPU` (pull-up), bit 9 `FUN_IE` (input enable).
* **Preconditions:** GPIO 15 selected as output pin; GPIO 16 selected as input pin with pull-up resistor enabled.
* **Stimulus Inputs & Operation Sequence:**
  1. Configure IO_MUX for GPIO 15: select Function 1 (GPIO), clear pull-up and pull-down.
  2. Write `(1U << 15)` to `GPIO_ENABLE_W1TS_REG`.
  3. Write `(1U << 15)` to `GPIO_OUT_W1TS_REG` (drive high). Read back `GPIO_OUT_REG`.
  4. Write `(1U << 15)` to `GPIO_OUT_W1TC_REG` (drive low). Read back `GPIO_OUT_REG`.
  5. Configure GPIO 16: set `FUN_IE` (bit 9) and `FUN_WPU` (bit 8) in `IO_MUX_GPIO_16_REG`. Read `GPIO_IN_REG` bit 16.
* **Expected Outputs & Hardware Telemetry:**
  - `GPIO_OUT_REG` bit 15 reads 1 after W1TS write.
  - `GPIO_OUT_REG` bit 15 reads 0 after W1TC write.
  - `GPIO_OUT_REG` mutations do not affect adjacent bits (atomic W1TS/W1TC execution).
  - `GPIO_IN_REG` bit 16 reads 1 (due to internal pull-up).
* **Timing / Concurrency Constraints:** Register propagation latency < 2 APB clock cycles (50 ns). Volatile write synchronization requires single-instruction memory fence.
* **Pass/Fail Criteria:** Pass if bit 15 accurately reflects W1TS and W1TC states and pull-up input reads 1.

#### Task 4.4: GDMA Multi-Channel Engine & Circular Buffer Descriptor Rings (TEST 28)
* **Target Roles:** `@agent:architect` (descriptor ring layout), `@agent:coder` (GDMA driver), `@agent:tester` (DMA ring test)
* **Hardware Register Blocks (SVD):** `GDMA` (`0x60080000`). TRM Ch. 4 (§4.1-§4.8).
* **Key TRM Bitfields:**
  - `DMA_IN_CONF0_CH0_REG` (`0x60080000 + 0x070`): bit 0 `IN_RST_CH0`, bit 1 `IN_LOOP_TEST_CH0`.
  - `DMA_IN_LINK_CH0_REG` (`0x60080000 + 0x080`): bits 19:0 `INLINK_ADDR_CH0`, bit 22 `INLINK_START_CH0`, bit 21 `INLINK_STOP_CH0`.
  - `DMA_OUT_LINK_CH0_REG` (`0x60080000 + 0x0E0`): bits 19:0 `OUTLINK_ADDR_CH0`, bit 21 `OUTLINK_START_CH0`.
* **Preconditions:** GDMA peripheral clock enabled in `PCR_GDMA_CONF_REG`. Two 4-byte aligned `dma_descriptor_t` instances allocated in HP SRAM DRAM (`0x40820000` to `0x4087FFFF`).
* **Stimulus Inputs & Operation Sequence:**
  1. Allocate Descriptor 0 and Descriptor 1 in DRAM. Set `desc0.buffer` to 64-byte aligned buffer in DRAM; set `desc0.next = &desc1`. Set `desc1.next = &desc0` (forming a closed circular ring).
  2. Set descriptor word 0: `size = 64`, `length = 0`, `owner = 1` (`DMA_OWNER_DMA`).
  3. Load Descriptor 0 physical address into `DMA_IN_LINK_CH0_REG` bits 19:0 (`INLINK_ADDR_CH0`).
  4. Assert reset `IN_RST_CH0`, release reset, and set `INLINK_START_CH0`.
* **Expected Outputs & Hardware Telemetry:**
  - Descriptor addresses verified as strictly 4-byte aligned (`((uintptr_t)&desc & 3) == 0`).
  - Readback of `DMA_IN_LINK_CH0_REG` bits 19:0 matches `(uintptr_t)&desc0 & 0xFFFFF`.
  - Hardware accepts descriptor chain without asserting configuration or bus alignment error flags.
* **Timing / Concurrency Constraints:** Descriptors and payload buffers must reside exclusively in internal DRAM (`0x40820000` to `0x4087FFFF`); GDMA cannot access external Flash XIP directly. Memory fence required prior to starting link.
* **Pass/Fail Criteria:** Pass if alignment assertions hold, circular link traversal returns to head, and hardware starts descriptor link without bus fault.

---

### 5.3 Phase 5: Wireless Subsystem, Modem Control & Network Transport

#### Task 5.1: Modem Clock & Power Control (MODEM_SYSCON / MODEM_LPCON) (TEST 29)
* **Target Roles:** `@agent:architect` (RF power architecture), `@agent:coder` (modem power driver), `@agent:tester` (clock verification test)
* **Hardware Register Blocks (SVD):** `MODEM_SYSCON` (`0x600A9800`), `MODEM_LPCON` (`0x600AF000`). TRM Ch. 8 (§8.3-§8.4).
* **Key TRM Bitfields:**
  - `MODEM_SYSCON_CLK_CONF_REG` (`0x600A9800 + 0x004`): bit 30 `CLK_BLE_TIMER_EN`, bit 29 `CLK_MODEM_SEC_EN`, bit 28 `CLK_MODEM_SEC_APB_EN`, bit 24 `CLK_ZB_MAC_EN`.
  - `MODEM_SYSCON_MODEM_RST_CONF_REG` (`0x600A9800 + 0x010`): bit 30 `RST_BLE_TIMER`, bit 24 `RST_ZBMAC`, bit 10 `RST_WIFIMAC`, bit 8 `RST_WIFIBB`.
  - `MODEM_SYSCON_CLK_CONF1_REG` (`0x600A9800 + 0x014`): bit 17 `CLK_BT_APB_EN`, bit 10 `CLK_WIFI_APB_EN`, bit 9 `CLK_WIFIMAC_EN`.
  - `MODEM_LPCON_COEX_LP_CLK_CONF_REG` (`0x600AF000 + 0x008`): bit 2 `CLK_COEX_LP_SEL_XTAL`.
* **Preconditions:** Main system clocks operational at 160 MHz. PCR peripheral clock distribution active.
* **Stimulus Inputs & Operation Sequence:**
  1. Write `MODEM_SYSCON_CLK_CONF_REG` to set bits 30, 29, 28, and 24.
  2. Write `MODEM_SYSCON_CLK_CONF1_REG` to set bits 17, 10, and 9.
  3. Clear reset bits 30, 24, 10, and 8 in `MODEM_SYSCON_MODEM_RST_CONF_REG`.
  4. Write `MODEM_LPCON_COEX_LP_CLK_CONF_REG` to set bit 2 (`CLK_COEX_LP_SEL_XTAL`).
* **Expected Outputs & Hardware Telemetry:**
  - Volatile readback of `MODEM_SYSCON_CLK_CONF_REG` confirms clock enable bits assert.
  - Volatile readback of `MODEM_SYSCON_MODEM_RST_CONF_REG` confirms reset bits clear.
  - Bus accesses to wireless baseband register blocks execute without generating bus faults.
* **Timing / Concurrency Constraints:** 1000-cycle settling delay required between enabling clocks and releasing resets.
* **Pass/Fail Criteria:** Pass if all clock enable bits read 1, reset bits read 0, and non-faulting bus access is confirmed.

#### Task 5.2: Bluetooth 5 (LE) Driver: Link Layer Controller, HCI & Minimal GATT Server (TEST 30)
* **Target Roles:** `@agent:architect` (HCI packet shim and GATT layout), `@agent:coder` (BLE driver), `@agent:tester` (HCI reset loopback test)
* **Hardware Register Blocks (SVD):** `MODEM_SYSCON` (`0x600A9800`), `INTERRUPT_CORE0` (`0x60010000`). TRM Ch. 8, 10, Bluetooth Core Spec v5.3.
* **Key TRM Bitfields:**
  - `MODEM_SYSCON_CLK_CONF_REG`: bit 30 `CLK_BLE_TIMER_EN`.
  - `INTERRUPT_CORE0_BLE_TIMER_INT_MAP_REG` (`0x60010000 + 4 * 9`): INTMTX Source 9 mapping.
* **Preconditions:** Task 5.1 modem clocks enabled. Static buffer allocated in DRAM for VHCI command/event packets.
* **Stimulus Inputs & Operation Sequence:**
  1. Initialize BLE controller shim.
  2. Assemble standard HCI Reset Command packet: Type `0x01` (Command), Opcode `0x0C03` (`HCI_Reset`), Length `0x00` (`[0x01, 0x03, 0x0C, 0x00]`).
  3. Dispatch packet to controller interface and poll for event response.
* **Expected Outputs & Hardware Telemetry:**
  - Controller returns standard HCI Event packet: Type `0x04` (Event), Event Code `0x0E` (`HCI_Command_Complete`), Length `0x04`, Num_HCI_Command_Packets `0x01`, Opcode `0x0C03`, Status `0x00` (`[0x04, 0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00]`).
  - Readback of static GATT database confirms valid pre-compiled attributes for Device Information (`0x180A`) and Custom Automation (`0xFFE0`).
* **Timing / Concurrency Constraints:** Controller response latency bounded at <= 100 ms. Zero dynamic memory allocation permitted (static buffers only).
* **Pass/Fail Criteria:** Pass if HCI Command Complete event returns status 0x00 within 100 ms.

#### Task 5.3: 802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Packet Ring (TEST 31)
* **Target Roles:** `@agent:architect` (packet ring architecture), `@agent:coder` (Wi-Fi driver), `@agent:tester` (packet ring test)
* **Hardware Register Blocks (SVD):** `GDMA` (`0x60080000`), `MODEM_SYSCON` (`0x600A9800`). TRM Ch. 4, 8.
* **Key TRM Bitfields:**
  - `MODEM_SYSCON_CLK_CONF1_REG`: bit 10 `CLK_WIFI_APB_EN`, bit 9 `CLK_WIFIMAC_EN`.
  - `MODEM_SYSCON_MODEM_RST_CONF_REG`: bit 10 `RST_WIFIMAC`, bit 8 `RST_WIFIBB`.
* **Preconditions:** GDMA Channel 1 initialized. Static pool of 32 `net_packet_t` buffers (1536 bytes each) allocated in `.dram0.bss`.
* **Stimulus Inputs & Operation Sequence:**
  1. Allocate 32 `dma_descriptor_t` structures.
  2. Bind each descriptor to a corresponding 1536-byte buffer from the static pool.
  3. Link descriptors in circular ring: `desc[i].next = &desc[(i + 1) % 32]`.
  4. Set descriptor word 0 flags: `size = 1536`, `length = 0`, `owner = 1` (`DMA_OWNER_DMA`).
* **Expected Outputs & Hardware Telemetry:**
  - All 32 descriptors are 4-byte aligned and point to DRAM addresses in `[0x40820000, 0x40880000)`.
  - Circular linkage traversal visits all 32 unique descriptors and returns to index 0.
  - Zero dynamic heap memory calls invoked.
* **Timing / Concurrency Constraints:** Top-half ISR must pass descriptor indices to DPC queue in < 5 us without payload copying.
* **Pass/Fail Criteria:** Pass if all 32 descriptors are verified inside DRAM boundaries and circularly linked.

#### Task 5.4: IEEE 802.15.4 Radio Transceiver Driver (TEST 32)
* **Target Roles:** `@agent:architect` (transceiver state machine), `@agent:coder` (802.15.4 driver), `@agent:tester` (radio control test)
* **Hardware Register Blocks (SVD):** `IEEE802154` (`0x600A3000`). TRM Ch. 30 (§30.1-§30.4).
* **Key TRM Bitfields:**
  - `IEEE802154_COMMAND_REG` (`0x600A3000 + 0x000`): bits 7:0 `OPCODE` (`0x04` = `FORCE_TRX_OFF`).
  - `IEEE802154_CTRL_CFG_REG` (`0x600A3000 + 0x004`): bit 0 `HW_AUTO_ACK_TX_EN`, bit 3 `HW_AUTO_ACK_RX_EN`.
  - `IEEE802154_CHANNEL_REG` (`0x600A3000 + 0x048`): bits 7:0 `CHANNEL` (11 through 26).
  - `IEEE802154_INF0_SHORT_ADDR_REG` (`0x600A3000 + 0x080`): bits 15:0 short address.
* **Preconditions:** Modem APB clock enabled. IEEE 802.15.4 reset released in `MODEM_SYSCON`.
* **Stimulus Inputs & Operation Sequence:**
  1. Write opcode `0x04` (`FORCE_TRX_OFF`) to `IEEE802154_COMMAND_REG`.
  2. Write value 15 (2425 MHz) to `IEEE802154_CHANNEL_REG`.
  3. Write `0x09` (`HW_AUTO_ACK_TX_EN | HW_AUTO_ACK_RX_EN`) to `IEEE802154_CTRL_CFG_REG`.
  4. Write short address `0x1234` to `IEEE802154_INF0_SHORT_ADDR_REG`.
* **Expected Outputs & Hardware Telemetry:**
  - Transceiver enters TRX_OFF state.
  - `IEEE802154_CHANNEL_REG` reads 15.
  - `IEEE802154_CTRL_CFG_REG` bits 0 and 3 read 1.
  - `IEEE802154_INF0_SHORT_ADDR_REG` reads `0x1234`.
* **Timing / Concurrency Constraints:** Radio state transition delay < 10 us. RF frequency synthesizer settling time <= 150 us.
* **Pass/Fail Criteria:** Pass if register readbacks match configured values with zero bus stalls.

#### Task 5.5: Bare-Metal Zero-Copy IPv4, ARP, ICMP, UDP & Lightweight TCP State Machine (TEST 33)
* **Target Roles:** `@agent:architect` (protocol state engine), `@agent:coder` (TCP/IP stack), `@agent:tester` (packet serialization test)
* **Hardware Register Blocks (SVD):** HP SRAM DRAM (`0x40820000`), `SYSTIMER` (`0x6000A000`). RFC 791, 792, 793, 826.
* **Preconditions:** Packet buffer available from static pool. Base MAC initialized from eFuse.
* **Stimulus Inputs & Operation Sequence:**
  1. Construct synthetic ARP Request packet for IP `192.168.1.1`.
  2. Parse packet through ARP input parser; generate ARP Reply.
  3. Compute IPv4 header checksum across standard RFC 1071 test vector (20-byte IP header).
  4. Verify TCP one's complement pseudo-header checksum calculation.
* **Expected Outputs & Hardware Telemetry:**
  - Serialized ARP Reply Ethernet header has ethertype `0x0806`, hardware type `0x0001`, protocol `0x0800`, opcode `0x0002` (Reply), and source MAC matching factory eFuse MAC (`40:4C:CA:45:1E:14`).
  - Computed RFC 1071 checksum validates to `0x0000` when verified over the completed header.
* **Timing / Concurrency Constraints:** Packet serialization and checksum execution time < 5 us without heap allocations.
* **Pass/Fail Criteria:** Pass if serialized wire bytes match RFC specifications and checksum matches test vector.

---

### 5.4 Phase 6: System Hardening & Security

#### Task 6.1: Hardware Cryptographic Accelerators & Hardware TRNG (TEST 34)
* **Target Roles:** `@agent:architect` (crypto driver API), `@agent:coder` (crypto drivers), `@agent:tester` (hardware crypto test)
* **Hardware Register Blocks (SVD):** `SHA` (`0x60089000`), `AES` (`0x60088000`), `ECC` (`0x6008B000`), `RNG` (`0x600B2800`). TRM Ch. 19, 20, 23.
* **Key TRM Bitfields:**
  - `SHA_MODE_REG` (`0x60089000 + 0x000`): mode 2 = SHA-256.
  - `SHA_START_REG` (`0x60089000 + 0x010`): bit 0 `START`.
  - `SHA_BUSY_REG` (`0x60089000 + 0x018`): bit 0 `BUSY` (1 = busy, 0 = idle).
  - `AES_MODE_REG` (`0x60088000 + 0x040`): mode 0 = AES-128.
  - `AES_TRIGGER_REG` (`0x60088000 + 0x048`): bit 0 `TRIGGER`.
  - `AES_STATE_REG` (`0x60088000 + 0x04C`): bit 0 `STATE` (0 = idle).
  - `RNG_DATA_REG` (`0x600B2800 + 0x008`): 32-bit hardware true random entropy word.
* **Preconditions:** Crypto accelerator clock gates enabled in `PCR_SEC_CONF_REG`.
* **Stimulus Inputs & Operation Sequence:**
  1. Write ASCII string `"abc"` into SHA text buffer; start SHA-256 calculation; poll `SHA_BUSY_REG` until 0; read digest.
  2. Load 128-bit key and 16-byte plaintext test vector (NIST SP 800-38A) into AES buffer; trigger AES-128 encryption; poll `AES_STATE_REG` until 0; read ciphertext.
  3. Sample two consecutive 32-bit random words from `RNG_DATA_REG`.
* **Expected Outputs & Hardware Telemetry:**
  - SHA-256 digest matches byte-exact NIST vector: `BA7816BF 8F01CFEA 414140DE 5DAE2223 B00361A3 96177A9C B410FF61 F20015AD`.
  - AES-128 ciphertext matches NIST SP 800-38A expected output.
  - Hardware TRNG yields two distinct, non-zero random words (`rand1 != rand2` and `rand1 != 0`).
* **Timing / Concurrency Constraints:** SHA polling timeout <= 10,000 cycles. AES block completes in < 100 cycles at 160 MHz.
* **Pass/Fail Criteria:** Pass if SHA and AES match test vectors and TRNG produces distinct non-zero words.

#### Task 6.2: APM & TEE Multi-Domain Security Sandboxing (TEST 35)
* **Target Roles:** `@agent:architect` (security partitions), `@agent:coder` (APM driver), `@agent:tester` (security filter test)
* **Hardware Register Blocks (SVD):** `HP_APM` (`0x60099000`), `TEE` (`0x60098000`), `LP_APM` (`0x600B3800`). TRM Ch. 16 (§16.1-§16.5).
* **Key TRM Bitfields:**
  - `HP_APM_REGION_START_REG(n)` (`0x60099000 + 0x004 + 0x0C * n`): 32-bit start address.
  - `HP_APM_REGION_END_REG(n)` (`0x60099000 + 0x008 + 0x0C * n`): 32-bit end address.
  - `HP_APM_REGION_PMS_ATTR_REG(n)` (`0x60099000 + 0x00C + 0x0C * n`): bit 0 `PMS_X`, bit 1 `PMS_W`, bit 2 `PMS_R`.
  - `HP_APM_REGION_FILTER_ENABLE_REG` (`0x60099000 + 0x000`): 16-bit enable mask.
  - `HP_APM_M_STATUS_REG(m)` (`0x60099000 + 0x0C8 + 0x10 * m`): Master violation status register.
* **Preconditions:** APM initialized. Region 0 pass-through active.
* **Stimulus Inputs & Operation Sequence:**
  1. Configure APM Region 2 over sensitive key buffer (`0x40823000` to `0x408230FF`) with Execute=0, Write=0, Read=1 (Read-Only).
  2. Enable Region 2 in `HP_APM_REGION_FILTER_ENABLE_REG`.
  3. Verify Region 0 remains enabled with full pass-through permissions.
* **Expected Outputs & Hardware Telemetry:**
  - `HP_APM_REGION_START_REG(2)` reads `0x40823000`; `END_REG(2)` reads `0x408230FF`.
  - `HP_APM_REGION_PMS_ATTR_REG(2)` reflects `PMS_R = 1`, `PMS_W = 0`, `PMS_X = 0`.
  - `HP_APM_REGION_FILTER_ENABLE_REG` has bits 0 and 2 set.
  - `HP_APM_M_STATUS_REG(0)` reports 0 pending violation exceptions.
* **Timing / Concurrency Constraints:** Reconfiguration takes effect within 1 clock cycle. Region 0 cannot be disabled (hardware reservation guard).
* **Pass/Fail Criteria:** Pass if Region 2 registers match configuration and 0 violation exceptions are flagged.

#### Task 6.3: Fault Injection Resistance, Watchdog Multi-Stage Escalation & Panic Crash-Dumps (TEST 36)
* **Target Roles:** `@agent:architect` (fault recovery hierarchy), `@agent:coder` (panic dumper), `@agent:tester` (fault recovery test)
* **Hardware Register Blocks (SVD):** `TIMG0` (`0x60008000`), `LP_WDT` (`0x600B1C00`), RISC-V CSRs (`mcause`, `mepc`, `mtval`). TRM Ch. 15, Ch. 1.
* **Key TRM Bitfields:**
  - `TIMG_WDTCONFIG0_REG`: bits 17:15 `WDT_STG0` (1 = interrupt), bits 20:18 `WDT_STG1` (2 = CPU reset).
  - `TIMG_WDTFEED_REG`: reload register.
* **Preconditions:** Vectored trap handler registered in IRAM. Watchdog supervisor active.
* **Stimulus Inputs & Operation Sequence:**
  1. Execute invalid instruction (`asm volatile(".word 0x00000000")`) within controlled test harness.
  2. Trap handler intercepts exception `mcause == 2` (Illegal Instruction).
  3. Format structured hexadecimal panic dump over dual console multiplexer.
* **Expected Outputs & Hardware Telemetry:**
  - Panic dump formats all 32 integer registers (`x0` to `x31`).
  - Formats CSRs: `mcause = 0x00000002`, `mtval = 0x00000000`, `mepc` equal to fault PC.
  - Panic output transmits completely via non-blocking polling without system freeze.
* **Timing / Concurrency Constraints:** Panic dump transmission must complete in < 50 ms before watchdog escalation triggers reset.
* **Pass/Fail Criteria:** Pass if panic dump accurately outputs 32 registers, valid mcause, and faulting PC.

#### Task 6.4: Zero-Allocation Local REST/HTTP Engine & Google Home Matter Commissioning Bridge (TEST 37)
* **Target Roles:** `@agent:architect` (REST API schema and Matter cluster model), `@agent:coder` (HTTP parser and Matter encoder), `@agent:tester` (API validation test)
* **Hardware Register Blocks (SVD):** Static `.flash.rodata` (`0x42800000`), `.dram0.bss` route table, `ECC` (`0x6008B000`). Matter Spec v1.2, RFC 2616.
* **Preconditions:** HTTP router registered with static route table in `.dram0.bss`. Route `/api/status` active.
* **Stimulus Inputs & Operation Sequence:**
  1. Feed synthetic HTTP request: `"GET /api/status HTTP/1.1\r\nHost: 192.168.1.50\r\n\r\n"`.
  2. Feed synthetic invalid request: `"POST /unknown HTTP/1.1\r\n\r\n"`.
  3. Invoke Matter onboarding manual code generator with test parameters: Vendor ID `0xFFF1`, Product ID `0x8001`, Discriminator `3840` (`0x0F00`), Passcode `20202021`.
* **Expected Outputs & Hardware Telemetry:**
  - Route `/api/status` returns HTTP 200 OK with Content-Type `application/json` containing valid uptime and heap metrics.
  - Route `/unknown` returns HTTP 404 Not Found.
  - Matter generator produces exact 11-digit pairing code `"34970112332"`.
* **Timing / Concurrency Constraints:** Routing latency <= 1 ms. Zero heap allocations permitted (`arena_scratch` used for temporary formatting).
* **Pass/Fail Criteria:** Pass if HTTP 200 JSON parses, 404 rejects invalid route, and Matter manual code matches test specification.

---

### 5.5 Phase 7: Production Release

#### Task 7.1: eFuse Memory Controller & Silicon Identity Sealing (TEST 38)
* **Target Roles:** `@agent:architect` (eFuse sealing plan), `@agent:coder` (eFuse driver), `@agent:tester` (identity verification test)
* **Hardware Register Blocks (SVD):** `EFUSE` (`0x600B0800`). TRM Ch. 6 (§6.1-§6.5), Ch. 21, 22.
* **Key TRM Bitfields:**
  - `EFUSE_RD_MAC_SPI_SYS_0_REG` (`0x600B0800 + 0x044`): bits 31:0 `MAC_0` (lower 32 bits).
  - `EFUSE_RD_MAC_SPI_SYS_1_REG` (`0x600B0800 + 0x048`): bits 15:0 `MAC_1` (upper 16 bits).
  - `EFUSE_RD_SYS_DATA0_REG` (`0x600B0800 + 0x028`): bits 7:0 `PKG_VERSION`, bits 23:18 `CHIP_VER`.
  - `EFUSE_RD_REPEAT_DATA0_REG` (`0x600B0800 + 0x018`): bit 2 `DIS_USB_SERIAL_JTAG`.
* **Preconditions:** Development eFuse state verified (`SPI_BOOT_CRYPT_CNT = 0`, `SECURE_BOOT_EN = 0`).
* **Stimulus Inputs & Operation Sequence:**
  1. Read `EFUSE_RD_MAC_SPI_SYS_0_REG` and `EFUSE_RD_MAC_SPI_SYS_1_REG`.
  2. Assemble 48-bit MAC address and compare against physical board summary.
  3. Verify `DIS_USB_SERIAL_JTAG` is 0 (enabled) in `EFUSE_RD_REPEAT_DATA0_REG`.
* **Expected Outputs & Hardware Telemetry:**
  - Extracted MAC address exactly matches `40:4C:CA:45:1E:14`.
  - USB-Serial-JTAG controller confirms unburned disable bit (`DIS_USB_SERIAL_JTAG == 0`).
  - eFuse controller reports 0 read errors.
* **Timing / Concurrency Constraints:** Read operations execute in 1 clock cycle. Zero irreversible burns permitted during automated test suites.
* **Pass/Fail Criteria:** Pass if extracted MAC matches physical board telemetry byte-for-byte with 0 read errors.

#### Task 7.2: Continuous 24/7 Soak Stability, Memory Leak Detection & Fault Injection Recovery (TEST 39)
* **Target Roles:** `@agent:architect` (stability criteria), `@agent:coder` (soak runner), `@agent:tester` (24-hour verification test)
* **Hardware Register Blocks (SVD):** `SYSTIMER` (`0x6000A000`), `TIMG0` (`0x60008000`), HP SRAM Arenas. TRM Ch. 13, 14, 15.
* **Preconditions:** Firmware flashed to hardware; connected to continuous power and automated serial telemetry logger.
* **Stimulus Inputs & Operation Sequence:**
  1. Execute continuous soak test cycling through 10,000 coroutine task yields, 1,000 DPC events, 500 timer interrupts, and 100 arena allocation/free cycles.
  2. Query telemetry every 1,000 cycles.
  3. In test coroutine, simulate deadlocked task; verify watchdog stage 1 interrupt catches stall, logs dump to retained LP SRAM, feeds watchdog, and terminates stalled task.
* **Expected Outputs & Hardware Telemetry:**
  - Uptime advances monotonically without rollbacks.
  - DPC queue drop count remains strictly 0.
  - Arena allocation bitmask returns to `0x00000000` (zero memory leaks).
  - Watchdog supervisor recovers deadlocked task without triggering whole-chip system reset.
* **Timing / Concurrency Constraints:** Continuous 24-hour duration.
* **Pass/Fail Criteria:** Pass if 0 leaks detected (`active_count == 0`), 0 DPC drops, 0 unexpected reboots, and 100% scheduler tick consistency.

#### Task 7.3: Dual-Slot External Flash OTA Firmware Update Engine & Rollback Protection (TEST 40)
* **Target Roles:** `@agent:architect` (OTA partition layout), `@agent:coder` (flash MMU driver), `@agent:tester` (OTA test)
* **Hardware Register Blocks (SVD):** `EXTMEM / CACHE` (`0x600C8000`), `SPI0` (`0x60002000`). TRM Ch. 4, 5.
* **Key TRM Bitfields:** `CACHE_FLASH_MMU_TABLE_REG(i)` (MMU page mapping entries).
* **Preconditions:** 8 MB flash partitioned into Slot A (`0x42000000` to `0x423FFFFF`, 4 MB) and Slot B (`0x42400000` to `0x427FFFFF`, 4 MB).
* **Stimulus Inputs & Operation Sequence:**
  1. Validate image header magic byte `0xE9`, 8 MB DIO flash geometry code `0x3`, and SHA-256 integrity hash over Slot B binary.
  2. Simulate Flash MMU page table remap of Active Slot to execution base `0x42000000`.
  3. Assert rollback flag state is safe.
* **Expected Outputs & Hardware Telemetry:**
  - Firmware header validation confirms compatibility for both slots.
  - MMU page tables support dynamic remapping of Slot B into execution window.
  - Rollback guard prevents booting corrupted slots.
* **Timing / Concurrency Constraints:** Flash erase/program operations must be 4 KB sector-aligned with periodic watchdog feeds.
* **Pass/Fail Criteria:** Pass if Slot A and Slot B boundaries align with 8 MB physical flash and SHA-256 integrity validates.

#### Task 7.4: Production Build Hardening, Strip Symbols, Final Certification Verification & Golden Baseline Seal (TEST 41)
* **Target Roles:** `@agent:architect` (release criteria), `@agent:coder` (build hardening), `@agent:tester` (golden baseline test)
* **Hardware Register Blocks (SVD):** Full System Architecture & ELF Executable.
* **Preconditions:** Feature branch `feat/phase0-3-test-hardening` ready for final certification.
* **Stimulus Inputs & Operation Sequence:**
  1. Execute production build target `make all` with optimization `-O2 -g0`, stripping debug symbols from final binary.
  2. Execute host verification: `make do-test`.
  3. Execute physical silicon verification over `/dev/ttyACM0`: shell command `do-test`.
* **Expected Outputs & Hardware Telemetry:**
  - Host test runner passes 23 out of 23 test suites (100%).
  - On-chip hardware validation passes 40 out of 40 tests (TEST 01 through TEST 40) with zero watchdog faults.
  - Binary size fits comfortably within allocated IRAM and Flash XIP partitions.
  - Git working directory is clean.
* **Timing / Concurrency Constraints:** Zero compiler or linker warnings with `-Wall -Wextra -Werror`.
* **Pass/Fail Criteria:** Pass if 100% of host tests pass, 100% of silicon tests pass, and zero unapproved changes exist on `main`.

---

## 6. Standardized Tracking, Defect Classification & Incident Protocols

To ensure multi-agent coordination and traceability across complex bare-metal development cycles, all anomalies, architectural discrepancies, and deferred work must adhere to standardized tracking schemas and escalation protocols.

### 6.1 Defect Severity Hierarchy

Every anomaly identified during validation is classified under one of four severity tiers:

* **Severity 1 (Blocker / Critical):** Hardware bus faults, unhandled machine trap panics, CPU lockups, memory corruptions across Harvard partitions, watchdog reset loops, or direct regressions affecting active roadmap milestones. Immediately blocks progression. Requires immediate rollback or hotfix on the feature branch.
* **Severity 2 (Major / High):** Subsystem timing violations, dropped DPC events under burst conditions, buffer truncation edge cases, priority preemption inversion, or inaccurate hardware telemetry reporting. Must be resolved before the feature branch can be submitted for review.
* **Severity 3 (Moderate / Medium):** Non-blocking performance bottlenecks, suboptimal memory alignment padding, shell formatting inconsistencies, or minor test harness improvements that do not impact hardware stability.
* **Severity 4 (Minor / Low):** Code typography anomalies, comment clarifications, or documentation styling enhancements adhering to `AGENTS.md`.

### 6.2 Standardized Tracking ID Schemas

Tracking identifiers must follow strict format rules to enable automated parsing and cross-referencing:

* **Audit Findings:** `[AUDIT-P<Phase>-<Index>]` (e.g. `[AUDIT-P03-18]`)
  - Identifies architectural gaps, missing unit tests, or hardware alignment discrepancies discovered during milestone audits.
* **Test Assertions:** `[TEST-P<Phase>-<Task>]` (e.g. `[TEST-P04-4.1]`)
  - Maps discrete roadmap tasks to automated validation routines.
* **Hardware Telemetry Reports:** `[TELEM-P<Phase>-<Subsystem>]` (e.g. `[TELEM-P02-USB]`)
  - References physical board dumps, eFuse bitfields, or console telemetry captures.
* **Incident Escalation Tickets:** `[INCIDENT-P<Phase>-<Number>]` (e.g. `[INCIDENT-P03-01]`)
  - Generated when a task fails, an assumption breaks, or a dependency is invalid.

### 6.3 Incident Escalation Protocol (per AGENTS.md)

When a task fails or an assumption breaks during execution:
1. **Stop Execution:** Immediately halt task progression. Do not attempt speculative workarounds or undocumented fixes outside task boundaries.
2. **Compile Structured Incident Review:** Document the exact failure point, verbatim tool outputs or register dumps, broken dependencies, and current git state.
3. **Escalate to Parent Agent:**
   - When operating under an orchestrator: Forward the structured incident report up to the parent agent via `send_message`.
   - When operating standalone: Surface the structured review directly to the user for explicit alignment before proceeding.
4. **Deferred Work Tracking:** Never embed inline placeholders or `TODO` comments in source files. When follow-ups or deferred work items are identified, document them as formal tracking tickets with complete inputs, deliverables, and acceptance criteria.
5. **Protected Branch Enforcement:** Never merge any feature branch into `main` without explicit user permission. All task development, fixes, and validation must remain isolated on designated feature branches (`feat/<task-id>-<task-name>`).

---

## 7. Operational Troubleshooting & Recovery Runbook

During continuous bare-metal development and hardware flashing over `/dev/ttyACM0`, operational anomalies can occur. The following procedures provide rapid diagnosis and recovery:

### 7.1 Serial Device Contention (`Device or resource busy`)
* **Symptom:** Flashing (`make flash`) or serial monitoring (`make monitor`) fails with `Could not open /dev/ttyACM0: Device or resource busy`.
* **Root Cause:** A background process (e.g. an active `picocom` session, zombie Python serial monitor, or MCP server connection) holds an exclusive open lock on the device node.
* **Recovery Procedure:**
  1. Identify the holding process:
     ```bash
     fuser /dev/ttyACM0
     ```
  2. Terminate the blocking process:
     ```bash
     fuser -k /dev/ttyACM0
     ```
  3. Re-verify accessibility:
     ```bash
     test -w /dev/ttyACM0 && echo "DEVICE_AVAILABLE"
     ```

### 7.2 Bootloader Strapping & Panic Loop Recovery
* **Symptom:** Hardware repeatedly crashes on boot or traps before USB CDC-ACM initialisation, preventing firmware updates over serial.
* **Root Cause:** Corrupted vector table, illegal instruction in startup path, or invalid clock prescaler triggering immediate watchdog reset.
* **Recovery Procedure:**
  1. Force the ESP32-C6 into ROM Bootloader mode using hardware strapping pins:
     - Hold down the on-board `BOOT` button (GPIO 9 pulled low).
     - Press and release the `RESET` (CHIP_PU) button.
     - Release the `BOOT` button.
  2. Verify ROM bootloader response:
     ```bash
     esptool --chip esp32c6 --port /dev/ttyACM0 chip_id
     ```
  3. Flash known-good golden firmware image or erase flash:
     ```bash
     esptool --chip esp32c6 --port /dev/ttyACM0 erase_flash
     make flash
     ```

### 7.3 Watchdog Trip Root-Cause Diagnosis
* **Symptom:** Device resets unexpectedly during test execution.
* **Root Cause:** Starvation of watchdog feed loop, coroutine task deadlock, or ISR execution exceeding maximum time slice.
* **Diagnosis Procedure:**
  1. Query system reset reason register on reboot:
     - The shell `info` command prints the reset cause: `Reset: [0x00000015]`.
     - Code `0x00000015` confirms normal USB/UART reset.
     - Code `0x00000010` or `0x00000003` indicates hardware watchdog reset.
  2. Inspect retained LP SRAM scratchpad (`0x50003000`) for diagnostic crash frames logged prior to reset.
  3. Check watchdog feed epoch counters to ensure feed rate stays within 1 to 20 feeds per second.

### 7.4 Container Sandbox Permission Failure Mitigation
* **Symptom:** Commands executed by AI agents return `connecting to sandbox server: read unix @->@: recvmsg: connection reset by peer`.
* **Root Cause:** The execution environment defaults to containerized isolation that restricts device node access to `/dev/ttyACM0`.
* **Mitigation:** Specify `BypassSandbox: true` in all tool calls requiring access to `/dev/` or host build tools.
