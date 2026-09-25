# Iron V: Bare-Metal RISC-V Operating System for ESP32-C6

Iron V is a freestanding, zero-dependency bare-metal C11 and GNU Assembly operating system and runtime built for the Espressif ESP32-C6 RISC-V microcontroller. Operating completely independently of ESP-IDF and third-party RTOS kernels, Iron V implements direct hardware control over memory, interrupt dispatching, multi-tier watchdog supervision, coroutine scheduling, and wireless transceivers.

The runtime is designed for continuous operational stability with zero dynamic heap fragmentation, using static memory pools, a lock-free Single-Producer Single-Consumer (SPSC) Deferred Procedure Call (DPC) engine, and a dual-console command shell accessible over both hardware UART0 and native USB-Serial-JTAG.

---

## Hardware Platform

The runtime targets the official ESP32-C6-DevKitC-1 development board:

- **SoC:** Espressif ESP32-C6 (QFN40, revision v0.0)
- **High-Performance (HP) Core:** Single-core 32-bit RISC-V RV32IMAC operating at 160 MHz (clocked from a 480 MHz PLL)
- **Low-Power (LP) Core:** 32-bit RISC-V coprocessor operating at 20 MHz with 16 KB LP SRAM
- **Memory Architecture:**
  - 512 KB Internal HP SRAM partitioned into Instruction RAM (164 KB IRAM) and Data RAM (348 KB DRAM) to enforce execute/data segregation
  - 16 KB Low-Power RTC SRAM
- **External Storage:** 8 MB SPI NOR Flash (Quad/Dual SPI) mapped via hardware cache
- **Console & Communication:**
  - On-chip native USB-Serial-JTAG (CDC-ACM)
  - Hardware UART0 (GPIO 16 TX, GPIO 17 RX)
- **Wireless Capabilities:**
  - 2.4 GHz Wi-Fi 6 (802.11ax)
  - Bluetooth 5 (LE)
  - IEEE 802.15.4 (Zigbee and Thread support)

---

## Directory Topology

```
iron-v/
├── Makefile                                     # Build, test, flash, and serial monitor automation
├── README.md                                    # Project documentation
├── ld/                                          # Linker scripts
│   ├── link.ld                                  # Master Harvard-partitioned linker script
│   └── rom/                                     # Espressif ROM symbol table linker scripts
├── lp_core/                                     # Low-Power coprocessor firmware
│   ├── main.c                                   # LP core entry and firmware logic
│   └── link.ld                                  # LP core memory map
├── libs/                                        # Vendor static libraries
│   └── esp32c6/                                 # Static wireless and PHY libraries
├── scripts/                                     # Automation & hardware validation scripts
│   ├── board-validation.py                      # Physical silicon test runner via MCP server
│   └── gen_headers.py                           # SVD-to-C header generation utility
├── src/                                         # Kernel and peripheral drivers
│   ├── crt0.S                                   # Reset vector, CSR initialization, and C runtime boot
│   ├── trap_entry.S / trap.c                    # Machine-mode trap entry, exception decoding, panic dump
│   ├── main.c                                   # Kernel initialization and interactive shell
│   ├── clock.c                                  # System clock configuration (160 MHz CPU, 40 MHz APB)
│   ├── wdt.c                                    # Multi-tier watchdog supervisor (TIMG0 and SWD)
│   ├── console.c / uart.c / usb_serial.c        # Dual multiplexed console (UART0 & USB CDC-ACM)
│   ├── arena.c / arena.h                        # Static memory allocator and linear scratchpad
│   ├── dpc.c / dpc.h                            # Lock-free SPSC deferred procedure call ring buffer
│   ├── interrupt.c / systimer.c / timer.c       # Interrupt matrix, high-res SYSTIMER, and periodic timer
│   ├── task.c / task_switch.S                   # Cooperative coroutine task engine
│   ├── pmp.c / power.c / lp_core.c              # RISC-V PMP isolation, sleep control, and LP mailbox
│   ├── gpio.c / gdma.c                          # GPIO Matrix routing and multi-channel GDMA engine
│   ├── modem.c / ble.c / wifi.c / ieee802154.c  # Wireless baseband, BLE GAP/GATT, Wi-Fi 6, 802.15.4
│   ├── net.c / tcp.c                            # Lightweight TCP/IP stack (IPv4, ARP, ICMP, UDP, TCP)
│   ├── wifi_os_adapter.c                        # Freestanding OSAL for vendor Wi-Fi binary blobs
│   ├── wifi_regulatory.c / wifi_regulatory.h    # Native 2.4 GHz regulatory rules and country mapping
│   ├── wifi_ftm_cal.c / wifi_ftm_cal.h          # Native 802.11mc FTM timing calibration parameters
│   ├── wifi_phy_data.c / wifi_phy_data.h        # Native canonical 128-byte PHY initialization parameters
│   ├── wifi_vendor_types.h                      # Unified Wi-Fi binary blob types and OSAL dispatch
│   └── regs/                                    # Register definition headers
└── tests/                                       # Host unit testing and static analysis
    ├── README.md                                # Test suite overview
    ├── test_freestanding.c                      # Host C unit test harness (466+ assertions)
    └── test_runner.py                           # Automated test runner and static ELF analysis
```

---

## Requirements & Prerequisites

### Toolchain

1. **RISC-V Bare-Metal GCC Toolchain:**
   - `riscv64-unknown-elf-gcc` or `riscv64-elf-gcc` with `rv32imac` multilib support.
2. **Python 3 & esptool:**
   - `python3` (v3.8+)
   - `esptool.py` (v4.0+)
3. **Host Build Tools:**
   - GNU `make`
   - Host `gcc` (for running native host unit tests)
   - `picocom`, `minicom`, or standard terminal emulator

### Ubuntu / Debian Installation

```bash
# Install compiler and build utilities
sudo apt update
sudo apt install -y gcc-riscv64-unknown-elf gcc make python3 python3-pip picocom

# Install esptool for flashing
pip3 install esptool

# Grant user permissions for serial access
sudo usermod -aG dialout,uucp $USER
```

---

## Building, Flashing, and Running

All lifecycle steps are managed through GNU Make:

### 1. Compile Firmware
```bash
# Clean existing binaries
make clean

# Compile the LP firmware, kernel ELF, and final flashable image (firmware.bin)
make all
```

### 2. Run Test Suites
Executes the host unit tests and static binary analysis without requiring connected hardware:
```bash
make test
```

### 3. Flash to Target
Connect the ESP32-C6 board via USB and run:
```bash
# Automatically detects /dev/ttyACM0 or /dev/ttyUSB0
make flash

# Or specify a port manually:
make flash PORT=/dev/ttyACM0
```

### 4. Interactive Console
Open a serial terminal connected to the interactive shell at 115200 baud:
```bash
make monitor
```

---

## Credits & Attribution

- **[georgik/esp32-c6-swift-baremetal](https://github.com/georgik/esp32-c6-swift-baremetal)**: Reference implementation for bare-metal ESP32-C6 initialization, linker memory layouts, and ROM direct multi-segment image creation.
- **[pdlsurya/esp32-riscv-bare-metal-sdk](https://github.com/pdlsurya/esp32-riscv-bare-metal-sdk)**: Reference architecture for freestanding RISC-V startup sequences, assembly vector tables, and bare-metal hardware driver design.
- **[Espressif Systems](https://www.espressif.com/)**:
  - ESP32-C6 Technical Reference Manual, CMSIS-SVD peripheral maps, ROM linker mappings, and vendor static Wi-Fi/PHY closed-source libraries (`libnet80211.a`, `libpp.a`, `libphy.a`, `libcore.a`).
  - Portions of the Wi-Fi subsystem design choices—specifically the 802.11 regulatory channel rules and EIRP power constraints (`src/wifi_regulatory.c`), Fine Timing Measurement (FTM) calibration tables (`src/wifi_ftm_cal.c`), canonical 128-byte PHY initialization parameters (`src/wifi_phy_data.c`), and OSAL structure layouts (`src/wifi_vendor_types.h`)—are adapted from the [ESP-IDF](https://github.com/espressif/esp-idf) framework (Copyright (c) Espressif Systems (Shanghai) PTE LTD), licensed under the **Apache License, Version 2.0**.