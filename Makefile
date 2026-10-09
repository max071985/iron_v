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
OBJDUMP = $(CROSS_COMPILE)objdump

# All build outputs live under $(BUILD)
BUILD ?= build
OBJ_DIR = $(BUILD)/obj
GEN_DIR = $(BUILD)/gen
LP_DIR = $(BUILD)/lp_core
HOST_DIR = $(BUILD)/host
ELF = $(BUILD)/firmware.elf
BIN = $(BUILD)/firmware.bin
LP_IMAGE_H = $(GEN_DIR)/lp_firmware_image.h
# Local configuration: .config (untracked, see config.example) -> generated header
CONFIG_FILE ?= .config
CONFIG_GEN_H = $(GEN_DIR)/config_gen.h
HOST_TEST_BIN = $(HOST_DIR)/test_freestanding
# Light page (REV-25): web/index.html -> minified, gzip-compressed C array served at /
WEB_SRC = web/index.html
WEB_GEN_DIR = $(BUILD)/web
WEB_GZ_H = $(WEB_GEN_DIR)/web_index_gz.h

# Compiler flags
CFLAGS = -march=rv32imac_zicsr_zifencei -mabi=ilp32 -ffreestanding -nostdlib -Os -g -Wall -Wextra -Werror -Isrc -I$(GEN_DIR) -I$(WEB_GEN_DIR)

DEPFLAGS = -MMD -MP

# Host test build: native compiler, stub LP image (tests/host/), no cross toolchain needed
HOST_CC ?= gcc
HOST_CFLAGS = -O2 -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror -Itests/host -Isrc -I$(WEB_GEN_DIR)
# Host tests always build with the defaults (tests/host/config_gen.h), never with .config

# Linker flags
LDFLAGS = -T ld/link.ld -T ld/rom/esp32c6.rom.ld -T ld/rom/esp32c6.rom.phy.ld -T ld/rom/esp32c6.rom.pp.ld -T ld/rom/esp32c6.rom.net80211.ld -T ld/rom/esp32c6.rom.coexist.ld -Llibs/esp32c6 -nostdlib -Wl,--wrap=ram_set_chan_freq_sw_start

SRCS = src/crt0.S src/trap_entry.S src/task_switch.S src/main.c src/string.c src/utils.c src/test.c src/clock.c src/mmu.c src/wdt.c src/trap.c src/panic.c src/interrupt.c src/dpc.c src/usb_serial.c src/uart.c src/console.c src/timer.c src/arena.c src/systimer.c src/task.c src/pmp.c src/lp_core.c src/power.c src/gpio.c src/gdma.c src/modem.c src/wifi.c src/ieee802154.c src/net.c src/tcp.c src/dhcp.c src/wifi_os_adapter.c src/wifi_regulatory.c src/wifi_ftm_cal.c src/wifi_phy_data.c src/http_server.c src/speedtest.c src/shell.c src/efuse.c src/soak.c src/ota.c src/nvs.c src/provisioning.c src/wpa2_client.c src/wpa_ie.c src/mdns.c src/wifi_link.c src/button.c src/looptime.c src/flash_rom.c src/sha1_hw.c src/hw_rng.c src/json_lite.c src/rgb_led.c src/light.c src/mqtt.c src/api_v1.c

# Sources that only make sense on the target (startup, traps, console, timers, scheduler,
# the on-board self-test). Everything else in SRCS is also compiled into the host tests.
TARGET_ONLY_SRCS = src/crt0.S src/trap_entry.S src/task_switch.S src/main.c src/utils.c src/test.c src/clock.c src/wdt.c src/trap.c src/panic.c src/interrupt.c src/usb_serial.c src/uart.c src/console.c src/timer.c src/systimer.c src/task.c src/flash_rom.c src/sha1_hw.c src/hw_rng.c
HOST_SRCS = $(filter-out $(TARGET_ONLY_SRCS),$(SRCS))

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

.PHONY: all flash erase_flash monitor clean test host-test
all: $(BIN)

# The LP image header is generated into $(GEN_DIR); a leftover copy in src/ would shadow it
ifneq ($(wildcard src/lp_firmware_image.h),)
  $(error src/lp_firmware_image.h is stale (now generated in $(GEN_DIR)); delete it)
endif

# LP Core Firmware Targets
$(LP_DIR)/lp_firmware.elf: lp_core/main.c lp_core/link.ld
	@mkdir -p $(@D)
	$(CC) -march=rv32imac_zicsr -mabi=ilp32 -Os -nostdlib -Wl,-T,lp_core/link.ld $< -o $@

$(LP_DIR)/lp_firmware.bin: $(LP_DIR)/lp_firmware.elf
	$(OBJCOPY) -O binary $< $@

$(LP_IMAGE_H): $(LP_DIR)/lp_firmware.bin
	@mkdir -p $(@D)
	@python3 -c "with open('$<','rb') as f: d=f.read(); \
	open('$@','w').write('/* Auto-generated */\n#ifndef LP_FIRMWARE_IMAGE_H\n#define LP_FIRMWARE_IMAGE_H\n#include <stdint.h>\n#include <stddef.h>\nstatic const uint8_t g_lp_firmware_bin[] __attribute__((aligned(4))) = {' + ','.join(f'0x{b:02X}U' for b in d) + '};\nstatic const size_t g_lp_firmware_bin_len = ' + str(len(d)) + 'U;\n#endif\n')"

# Regenerated on every make; the file only changes (and triggers rebuilds via the .d files)
# when .config changes. A missing .config gives an empty header: defaults only.
$(CONFIG_GEN_H): FORCE
	@python3 scripts/gen_config.py $(CONFIG_FILE) $@

# Rewritten only when the compressed page changes
$(WEB_GZ_H): $(WEB_SRC) scripts/gen_web.py
	@python3 scripts/gen_web.py $(WEB_SRC) $@

.PHONY: FORCE
FORCE:

# Firmware
OBJS = $(patsubst src/%,$(OBJ_DIR)/%.o,$(basename $(SRCS)))

$(OBJ_DIR)/%.o: src/%.c $(CONFIG_GEN_H) | $(LP_IMAGE_H) $(WEB_GZ_H)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: src/%.S $(CONFIG_GEN_H) | $(LP_IMAGE_H) $(WEB_GZ_H)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

VENDOR_LIBS = $(wildcard libs/esp32c6/*.a)
VENDOR_STAMP = $(BUILD)/vendor_libs.ok

# Vendor libraries must match libs/esp32c6/SHA256SUMS (see libs/esp32c6/VERSION)
$(VENDOR_STAMP): $(VENDOR_LIBS) libs/esp32c6/SHA256SUMS libs/esp32c6/LICENSE
	@mkdir -p $(@D)
	@cd libs/esp32c6 && sha256sum --quiet -c SHA256SUMS
	@touch $@

MAP = $(BUILD)/firmware.map

$(ELF): $(OBJS) $(wildcard ld/*.ld ld/rom/*.ld) $(VENDOR_STAMP)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -Wl,--start-group -lnet80211 -lpp -lphy -lcore -Wl,--end-group -lgcc -Wl,-Map=$(MAP) -Wl,--print-memory-usage -o $@

$(BIN): $(ELF)
	esptool --chip esp32c6 elf2image --flash-mode dio --flash-size 8MB --flash-freq 80m -o $@ $<

flash: $(BIN)
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

# Host tests
HOST_OBJS = $(patsubst %.c,$(HOST_DIR)/%.o,tests/test_freestanding.c $(HOST_SRCS))

$(HOST_DIR)/%.o: %.c | $(WEB_GZ_H)
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(HOST_TEST_BIN): $(HOST_OBJS)
	$(HOST_CC) $(HOST_OBJS) -o $@

host-test: $(HOST_TEST_BIN)
	@python3 scripts/gen_config.py --self-test
	@python3 scripts/gen_web.py --self-test $(WEB_SRC)
	@./$(HOST_TEST_BIN)
	@python3 tests/test_companion_app.py

test: host-test $(ELF) $(BIN)
	@python3 tests/test_runner.py --elf $(ELF) --bin $(BIN) --host-test $(HOST_TEST_BIN) --objdump $(OBJDUMP)

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d) $(HOST_OBJS:.o=.d)

# Optional local, untracked targets (e.g. docs)
-include local.mk
