# Iron V Validation & Testing Architecture

## 1. Dual-Tier Testing Model
Iron V enforces a strict separation of concerns between host-side static verification and physical silicon telemetry:

| Validation Tier | Execution Target | Invocations | Purpose & Scope |
| :--- | :--- | :--- | :--- |
| **Tier 1: Host Static & Unit** | Host Linux Workstation (x86_64) | `make host-test`<br>`make test` | Verifies compiled ELF headers, segment permissions, 16-byte alignment, binary data/rodata initial values, and native C unit testing of `src/string.c`, `src/dpc.c`, `src/arena.c`, `src/pmp.c`, `src/wifi_regulatory.c`, `src/wifi_ftm_cal.c`, `src/wifi_phy_data.c`, cross-module coroutine + SYSTIMER + DPC integration, static arena pool exhaustion, PMP chained TOR boundaries, HP_APM dynamic filters, and console line reader edge cases. |
| **Tier 2: Physical Silicon** | ESP32-C6 RISC-V Silicon | `make PROFILE=dev flash && make monitor` -> `do-test` | Executes on-board bare-metal test suite in `src/modules/dev/test.c` (test build only). Performs volatile MMIO reads (`UART0`, `TIMG0`), RAM peek/poke mutations, watchdog control, GDMA descriptor ring transfers, and Wi-Fi baseband telemetry. |

## 2. Why Host Mocking Is Prohibited
In embedded bare-metal systems, simulating RAM mutations using host Python dictionaries (`mem = {}`) or printing hardcoded register reads creates false confidence and masks memory corruption, bus faults, and cache coherency bugs.
- Host tests must validate **real artifacts**: `build/<profile>/firmware.elf` sections, binary symbols, and C source code compiled with native compilers.
- Hardware register behaviors must run on **physical silicon**, where bus faults (`mcause = 5` or `7`) and misalignments actually trap.
- Host-compiled integration tests exercise genuine state machines, intrusive linked lists, ring buffers, bitmasks, and memory protection algorithms without simulating hardware side-effects.

## 3. On-Board Validation Procedure

### Host Test Layout
- `make host-test` compiles `tests/test_freestanding.c` with every portable source of the core and of every
  module (the Makefile's `HOST_SRCS`; `tests/host/config_gen.h` selects all modules) using the native
  compiler, into `build/host/test_freestanding`.
- `tests/host/lp_firmware_image.h` stands in for the generated LP core image so the host build needs no
  RISC-V toolchain; host tests check only the image header fields.
- `make test` then builds every profile and runs `tests/test_runner.py` on each image (ELF/BIN analysis of
  `build/<profile>/firmware.elf` and `.bin`: layout, budgets, the profile's modules linked and no others).

### Manual Hardware Validation
1. Connect the ESP32-C6-DevKitC-1 board via USB-C.
2. Flash the test build:
   ```bash
   make PROFILE=dev flash
   ```
3. Open serial telemetry console:
   ```bash
   make monitor
   ```
4. At the `iron_v> ` prompt, run the on-board suite:
   ```text
   iron_v> do-test
   ```

