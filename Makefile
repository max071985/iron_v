CROSS_COMPILE ?= $(shell \
	if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then \
		echo "riscv64-unknown-elf-"; \
	elif command -v riscv64-elf-gcc >/dev/null 2>&1; then \
		echo "riscv64-elf-"; \
	else \
		echo "riscv64-unknown-elf-"; \
	fi)

CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy

# Compiler flags
CFLAGS = -march=rv32imac_zicsr_zifencei -mabi=ilp32 -ffreestanding -nostdlib -O2 -g -Wall -Wextra -Werror -Isrc

# Linker flags
LDFLAGS = -T ld/link.ld -nostdlib

# Baseline source files
SRCS = src/crt0.S src/trap_entry.S src/main.c src/string.c src/utils.c src/test.c src/clock.c src/wdt.c src/trap.c src/panic.c src/interrupt.c src/dpc.c src/usb_serial.c src/uart.c src/console.c src/timer.c

# Flashing parameters
PORT ?= /dev/ttyUSB0
FLASH_BAUD ?= 921600
MONITOR_BAUD ?= 115200

# Targets
all: firmware.bin

firmware.elf: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

firmware.bin: firmware.elf
	esptool --chip esp32c6 elf2image --flash-mode dio --flash-size 8MB --flash-freq 80m -o $@ $<

flash: firmware.bin
	esptool --chip esp32c6 --port $(PORT) --baud $(FLASH_BAUD) write_flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 $<

erase_flash:
	esptool --chip esp32c6 --port $(PORT) erase_flash

monitor:
	picocom -b $(MONITOR_BAUD) $(PORT)

tests/test_freestanding: tests/test_freestanding.c src/string.c src/string.h src/dpc.c src/dpc.h
	gcc -O2 -Wall -Wextra -Werror -Isrc tests/test_freestanding.c src/string.c src/dpc.c -o $@

do-test: firmware.elf firmware.bin tests/test_freestanding
	@./tests/test_freestanding
	@python3 tests/test_runner.py

test: do-test

clean:
	rm -f *.elf *.bin tests/test_freestanding