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
CFLAGS = -march=rv32imac_zicsr_zifencei -mabi=ilp32 -ffreestanding -nostdlib -Os -g -Wall -Wextra -Werror -Isrc

# Linker flags
LDFLAGS = -T ld/link.ld -T ld/rom/esp32c6.rom.ld -T ld/rom/esp32c6.rom.phy.ld -T ld/rom/esp32c6.rom.pp.ld -T ld/rom/esp32c6.rom.net80211.ld -T ld/rom/esp32c6.rom.coexist.ld -Llibs/esp32c6 -nostdlib -Wl,--wrap=ram_set_chan_freq_sw_start

# Baseline source files
SRCS = src/crt0.S src/trap_entry.S src/task_switch.S src/main.c src/string.c src/utils.c src/test.c src/clock.c src/mmu.c src/wdt.c src/trap.c src/panic.c src/interrupt.c src/dpc.c src/usb_serial.c src/uart.c src/console.c src/timer.c src/arena.c src/systimer.c src/task.c src/pmp.c src/lp_core.c src/power.c src/gpio.c src/gdma.c src/modem.c src/ble.c src/ble_npl.c src/wifi.c src/ieee802154.c src/net.c src/tcp.c src/wifi_os_adapter.c src/wifi_regulatory.c src/wifi_ftm_cal.c src/wifi_phy_data.c

# Auto-detect hardware ports
DETECTED_ACM ?= $(firstword $(wildcard /dev/ttyACM*))
DETECTED_USB ?= $(firstword $(wildcard /dev/ttyUSB*))

# Interface selection: auto-detect if not explicitly provided (prefers USB, falls back to UART)
ifeq ($(origin INTERFACE), undefined)
  ifneq ($(findstring ttyUSB,$(PORT)),)
    INTERFACE := uart
  else ifneq ($(DETECTED_ACM),)
    INTERFACE := usb
  else ifneq ($(DETECTED_USB),)
    INTERFACE := uart
  else
    INTERFACE := usb
  endif
endif

MONITOR_BAUD ?= 115200

ifeq ($(INTERFACE),uart)
  PORT ?= $(if $(DETECTED_USB),$(DETECTED_USB),/dev/ttyUSB0)
  FLASH_BAUD ?= 460800
  MONITOR_FLAGS ?= -b $(MONITOR_BAUD)
else
  PORT ?= $(if $(DETECTED_ACM),$(DETECTED_ACM),/dev/ttyACM0)
  FLASH_BAUD ?= 460800
  MONITOR_FLAGS ?= --noreset --lower-rts --lower-dtr
endif

.PHONY: all flash monitor clean test
all: firmware.bin

# LP Core Firmware Targets
lp_core/lp_firmware.elf: lp_core/main.c lp_core/link.ld
	$(CC) -march=rv32imac_zicsr -mabi=ilp32 -Os -nostdlib -Wl,-T,lp_core/link.ld $< -o $@

lp_core/lp_firmware.bin: lp_core/lp_firmware.elf
	$(OBJCOPY) -O binary $< $@

src/lp_firmware_image.h: lp_core/lp_firmware.bin
	@python3 -c "with open('$<','rb') as f: d=f.read(); \
	open('$@','w').write('/* Auto-generated */\n#ifndef LP_FIRMWARE_IMAGE_H\n#define LP_FIRMWARE_IMAGE_H\n#include <stdint.h>\n#include <stddef.h>\nstatic const uint8_t g_lp_firmware_bin[] __attribute__((aligned(4))) = {' + ','.join(f'0x{b:02X}U' for b in d) + '};\nstatic const size_t g_lp_firmware_bin_len = ' + str(len(d)) + 'U;\n#endif\n')"

# Targets


firmware.elf: src/lp_firmware_image.h $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(filter-out src/lp_firmware_image.h,$^) -Wl,--start-group -lnet80211 -lpp -lphy -lcore -Wl,--end-group -lgcc -o $@

firmware.bin: firmware.elf
	esptool --chip esp32c6 elf2image --flash-mode dio --flash-size 8MB --flash-freq 80m -o $@ $<

flash: firmware.bin
	esptool --chip esp32c6 --port $(PORT) --baud $(FLASH_BAUD) write_flash --flash-mode dio --flash-size 8MB --flash-freq 80m 0x0 $<

erase_flash:
	esptool --chip esp32c6 --port $(PORT) erase_flash

monitor:
	@port="$(PORT)"; \
	flags="$(MONITOR_FLAGS)"; \
	if [ ! -e "$$port" ]; then \
		echo "Port '$$port' not found."; \
		if [ -e /dev/ttyACM0 ]; then \
			echo "Found USB port (/dev/ttyACM0). Connecting via USB..."; \
			port="/dev/ttyACM0"; \
			flags="--noreset --lower-rts --lower-dtr"; \
		elif [ -n "$(DETECTED_ACM)" ] && [ -e "$(DETECTED_ACM)" ]; then \
			echo "Found USB port ($(DETECTED_ACM)). Connecting via USB..."; \
			port="$(DETECTED_ACM)"; \
			flags="--noreset --lower-rts --lower-dtr"; \
		elif [ -e /dev/ttyUSB0 ]; then \
			echo "Found UART port (/dev/ttyUSB0). Connecting via UART at $(MONITOR_BAUD) baud..."; \
			port="/dev/ttyUSB0"; \
			flags="-b $(MONITOR_BAUD)"; \
		elif [ -n "$(DETECTED_USB)" ] && [ -e "$(DETECTED_USB)" ]; then \
			echo "Found UART port ($(DETECTED_USB)). Connecting via UART at $(MONITOR_BAUD) baud..."; \
			port="$(DETECTED_USB)"; \
			flags="-b $(MONITOR_BAUD)"; \
		else \
			echo "Error: Neither USB (/dev/ttyACM*) nor UART (/dev/ttyUSB*) port is available." >&2; \
			echo "Please connect the ESP32-C6 board via USB or UART." >&2; \
			exit 1; \
		fi; \
	fi; \
	echo "Connecting to $$port with picocom $$flags..."; \
	exec picocom $$flags "$$port"

tests/test_freestanding: tests/test_freestanding.c src/string.c src/string.h src/mmu.c src/mmu.h src/dpc.c src/dpc.h src/arena.c src/arena.h src/pmp.c src/pmp.h src/lp_core.c src/lp_core.h src/lp_firmware_image.h src/power.c src/power.h src/gpio.c src/gpio.h src/gdma.c src/gdma.h src/modem.c src/modem.h src/ble.c src/ble.h src/ble_gatt.h src/ble_npl.c src/ble_npl.h src/wifi.c src/wifi.h src/ieee802154.c src/ieee802154.h src/config.h src/net.c src/net.h src/tcp.c src/tcp.h src/wifi_os_adapter.c src/wifi_os_adapter.h src/wifi_vendor_types.h src/wifi_regulatory.h src/wifi_ftm_cal.h src/wifi_phy_data.h src/wifi_regulatory.c src/wifi_ftm_cal.c src/wifi_phy_data.c
	gcc -O2 -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror -Isrc tests/test_freestanding.c src/string.c src/mmu.c src/dpc.c src/arena.c src/pmp.c src/lp_core.c src/power.c src/gpio.c src/gdma.c src/modem.c src/ble.c src/ble_npl.c src/wifi.c src/ieee802154.c src/net.c src/tcp.c src/wifi_os_adapter.c src/wifi_regulatory.c src/wifi_ftm_cal.c src/wifi_phy_data.c -o $@

do-test: firmware.elf firmware.bin tests/test_freestanding
	@./tests/test_freestanding
	@python3 tests/test_runner.py

test: do-test

clean:
	rm -f *.elf *.bin tests/test_freestanding lp_core/*.elf lp_core/*.bin src/lp_firmware_image.h
