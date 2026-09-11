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
SRCS = src/crt0.S src/trap_entry.S src/task_switch.S src/main.c src/string.c src/utils.c src/test.c src/clock.c src/wdt.c src/trap.c src/panic.c src/interrupt.c src/dpc.c src/usb_serial.c src/uart.c src/console.c src/timer.c src/arena.c src/systimer.c src/task.c src/pmp.c src/lp_core.c src/power.c src/gpio.c src/gdma.c src/modem.c src/ble.c

# Interface selection: 'usb' (default) or 'uart'
INTERFACE ?= usb

ifeq ($(INTERFACE),uart)
  PORT ?= /dev/ttyUSB0
  FLASH_BAUD ?= 460800
  MONITOR_FLAGS ?= -b $(MONITOR_BAUD)
else
  PORT ?= /dev/ttyACM0
  FLASH_BAUD ?= 460800
  MONITOR_FLAGS ?= --noreset --lower-rts --lower-dtr
endif

MONITOR_BAUD ?= 115200

# LP Core Firmware Targets
lp_core/lp_firmware.elf: lp_core/main.c lp_core/link.ld
	$(CC) -march=rv32imac_zicsr -mabi=ilp32 -Os -nostdlib -Wl,-T,lp_core/link.ld $< -o $@

lp_core/lp_firmware.bin: lp_core/lp_firmware.elf
	$(OBJCOPY) -O binary $< $@

src/lp_firmware_image.h: lp_core/lp_firmware.bin
	@python3 -c "with open('$<','rb') as f: d=f.read(); \
	open('$@','w').write('/* Auto-generated */\n#ifndef LP_FIRMWARE_IMAGE_H\n#define LP_FIRMWARE_IMAGE_H\n#include <stdint.h>\n#include <stddef.h>\nstatic const uint8_t g_lp_firmware_bin[] __attribute__((aligned(4))) = {' + ','.join(f'0x{b:02X}U' for b in d) + '};\nstatic const size_t g_lp_firmware_bin_len = ' + str(len(d)) + 'U;\n#endif\n')"

# Targets
all: firmware.bin

firmware.elf: src/lp_firmware_image.h $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(filter-out src/lp_firmware_image.h,$^) -lgcc -o $@

firmware.bin: firmware.elf
	esptool --chip esp32c6 elf2image --flash-mode dio --flash-size 8MB --flash-freq 80m -o $@ $<

flash: firmware.bin
	esptool --chip esp32c6 --port $(PORT) --baud $(FLASH_BAUD) write_flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 $<

erase_flash:
	esptool --chip esp32c6 --port $(PORT) erase_flash

monitor:
	picocom $(MONITOR_FLAGS) $(PORT)

tests/test_freestanding: tests/test_freestanding.c src/string.c src/string.h src/dpc.c src/dpc.h src/arena.c src/arena.h src/pmp.c src/pmp.h src/lp_core.c src/lp_core.h src/lp_firmware_image.h src/power.c src/power.h src/gpio.c src/gpio.h src/gdma.c src/gdma.h src/modem.c src/modem.h src/ble.c src/ble.h src/ble_gatt.h
	gcc -O2 -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror -Isrc tests/test_freestanding.c src/string.c src/dpc.c src/arena.c src/pmp.c src/lp_core.c src/power.c src/gpio.c src/gdma.c src/modem.c src/ble.c -o $@

do-test: firmware.elf firmware.bin tests/test_freestanding
	@./tests/test_freestanding
	@python3 tests/test_runner.py

test: do-test

clean:
	rm -f *.elf *.bin tests/test_freestanding lp_core/*.elf lp_core/*.bin src/lp_firmware_image.h