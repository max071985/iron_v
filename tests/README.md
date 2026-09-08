# Iron V Validation & Testing Architecture

## 1. Dual-Tier Testing Model
Iron V enforces a strict separation of concerns between host-side static verification and physical silicon telemetry:

| Validation Tier | Execution Target | Invocations | Purpose & Scope |
| :--- | :--- | :--- | :--- |
| **Tier 1: Host Static & Unit** | Host Linux Workstation (x86_64) | `make do-test` | Verifies compiled ELF headers, segment permissions, 16-byte alignment, binary data/rodata initial values, and native C unit testing of `src/string.c`, `src/dpc.c`, `src/arena.c`, `src/pmp.c`, plus cross-module coroutine + SYSTIMER + DPC integration, static arena pool exhaustion, PMP chained TOR boundaries, HP_APM dynamic filters, and console line reader edge cases. |
| **Tier 2: Physical Silicon** | ESP32-C6 RISC-V Silicon | `make flash && make monitor` -> `do-test` | Executes on-board bare-metal test suite in `src/test.c`. Performs volatile MMIO reads (`UART0`, `TIMG0`), RAM peek/poke mutations, and watchdog control. |

## 2. Why Host Mocking Is Prohibited
In embedded bare-metal systems, simulating RAM mutations using host Python dictionaries (`mem = {}`) or printing hardcoded register reads creates false confidence and masks memory corruption, bus faults, and cache coherency bugs.
- Host tests must validate **real artifacts**: `firmware.elf` sections, binary symbols, and C source code compiled with native compilers.
- Hardware register behaviors must run on **physical silicon**, where bus faults (`mcause = 5` or `7`) and misalignments actually trap.
- Host-compiled integration tests exercise genuine state machines, intrusive linked lists, ring buffers, bitmasks, and memory protection algorithms without simulating hardware side-effects.

## 3. On-Board Validation Procedure
To execute Tier 2 validation on physical hardware:
1. Connect the ESP32-C6-DevKitC-1 board via USB-C.
2. Flash the firmware:
   ```bash
   make flash
   ```
3. Open serial telemetry console:
   ```bash
   make monitor
   ```
4. At the `iron_v> ` prompt, run the on-board suite:
   ```text
   iron_v> do-test
   ```
