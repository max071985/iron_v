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

# Build profile (REV-33): profiles/<PROFILE>.config selects the modules (src/modules/<name>/) and
# the memory sizes; the local .config (see config.example) overrides any key of it. Plain `make`
# builds the production light image; the test build with the developer tools is PROFILE=dev.
PROFILE ?= light
PROFILES = $(sort $(basename $(notdir $(wildcard profiles/*.config))))
PROFILE_FILE = profiles/$(PROFILE).config

# All build outputs live under $(BUILD); each profile has its own directory
BUILD ?= build
OUT = $(BUILD)/$(PROFILE)
OBJ_DIR = $(OUT)/obj
GEN_DIR = $(OUT)/gen
LP_DIR = $(BUILD)/lp_core
HOST_DIR = $(BUILD)/host
ELF = $(OUT)/firmware.elf
BIN = $(OUT)/firmware.bin
MAP = $(OUT)/firmware.map
LP_IMAGE_H = $(LP_DIR)/lp_firmware_image.h
# Profile + local configuration -> generated header and make fragment (scripts/gen_config.py)
CONFIG_FILE ?= .config
CONFIG_GEN_H = $(GEN_DIR)/config_gen.h
CONFIG_MK = $(GEN_DIR)/config.mk
HOST_TEST_BIN = $(HOST_DIR)/test_freestanding
# Light page (REV-25): the light module's index.html -> minified, gzip-compressed C array served at /
WEB_SRC = src/modules/light/index.html
WEB_GEN_DIR = $(BUILD)/web
WEB_GZ_H = $(WEB_GEN_DIR)/web_index_gz.h

# Modules: each src/modules/<name>/module.mk sets MODULE_SRCS_<name>, MODULE_REQUIRES_<name> and
# optionally MODULE_TARGET_ONLY_<name> (sources left out of the host tests)
ALL_MODULES = $(sort $(notdir $(patsubst %/module.mk,%,$(wildcard src/modules/*/module.mk))))
include $(wildcard src/modules/*/module.mk)

# Goals that need no profile configuration; every other goal builds $(PROFILE)
NO_PROFILE_GOALS = clean host-test test profiles monitor erase_flash
ifneq ($(filter-out $(NO_PROFILE_GOALS),$(or $(MAKECMDGOALS),all)),)
  ifeq ($(wildcard $(PROFILE_FILE)),)
    $(error PROFILE=$(PROFILE): no $(PROFILE_FILE) (profiles: $(PROFILES)))
  endif
  # Runs on every make; both outputs are rewritten only when the configuration changes
  GEN_CONFIG_STATUS := $(shell python3 scripts/gen_config.py --profile $(PROFILE) $(CONFIG_GEN_H) $(CONFIG_MK) $(PROFILE_FILE) $(wildcard $(CONFIG_FILE)) >&2; echo $$?)
  ifneq ($(GEN_CONFIG_STATUS),0)
    $(error scripts/gen_config.py failed for PROFILE=$(PROFILE))
  endif
  include $(CONFIG_MK)
  $(foreach m,$(PROFILE_MODULES),$(foreach r,$(MODULE_REQUIRES_$(m)),$(if $(filter $(r),$(PROFILE_MODULES)),,$(error module $(m) needs module $(r) (MODULES=$(PROFILE_MODULES))))))
endif

# Compiler flags (module headers: only the selected modules, so the core cannot depend on one)
MODULE_INC = $(foreach m,$(PROFILE_MODULES),-Isrc/modules/$(m))
CFLAGS = -march=rv32imac_zicsr_zifencei -mabi=ilp32 -ffreestanding -nostdlib -Os -g -Wall -Wextra -Werror -Isrc $(MODULE_INC) -I$(GEN_DIR) -I$(LP_DIR) -I$(WEB_GEN_DIR)

DEPFLAGS = -MMD -MP

# Host test build: native compiler, stub LP image (tests/host/), no cross toolchain needed.
# Host tests always build with the defaults and every module (tests/host/config_gen.h), never with .config
HOST_CC ?= gcc
HOST_CFLAGS = -O2 -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror -Itests/host -Isrc $(foreach m,$(ALL_MODULES),-Isrc/modules/$(m)) -I$(WEB_GEN_DIR)

# Linker flags; the main-stack budget comes from the profile (MAIN_STACK_MIN)
LDFLAGS = -T ld/link.ld -T ld/rom/esp32c6.rom.ld -T ld/rom/esp32c6.rom.phy.ld -T ld/rom/esp32c6.rom.pp.ld -T ld/rom/esp32c6.rom.net80211.ld -T ld/rom/esp32c6.rom.coexist.ld -Llibs/esp32c6 -nostdlib -Wl,--wrap=ram_set_chan_freq_sw_start -Wl,--defsym=MAIN_STACK_MIN_SIZE=$(PROFILE_MAIN_STACK_MIN)

# The core: in every image
CORE_SRCS = src/crt0.S src/trap_entry.S src/task_switch.S src/main.c src/string.c src/utils.c src/clock.c src/mmu.c src/wdt.c src/trap.c src/panic.c src/interrupt.c src/dpc.c src/usb_serial.c src/uart.c src/console.c src/timer.c src/arena.c src/systimer.c src/task.c src/pmp.c src/lp_core.c src/power.c src/gpio.c src/gdma.c src/modem.c src/wifi.c src/net.c src/tcp.c src/dhcp.c src/wifi_os_adapter.c src/wifi_regulatory.c src/wifi_ftm_cal.c src/wifi_phy_data.c src/http_server.c src/shell.c src/efuse.c src/ota.c src/nvs.c src/provisioning.c src/wpa2_client.c src/wpa_ie.c src/mdns.c src/wifi_link.c src/button.c src/looptime.c src/flash_rom.c src/sha1_hw.c src/hw_rng.c src/json_lite.c src/api_v1.c src/memstat.c src/module.c src/device.c

# Sources that only make sense on the target (startup, traps, console, timers, scheduler,
# the on-board self-test). Everything else is also compiled into the host tests.
TARGET_ONLY_SRCS = src/crt0.S src/trap_entry.S src/task_switch.S src/main.c src/utils.c src/clock.c src/wdt.c src/trap.c src/panic.c src/interrupt.c src/usb_serial.c src/uart.c src/console.c src/timer.c src/systimer.c src/task.c src/flash_rom.c src/sha1_hw.c src/hw_rng.c $(foreach m,$(ALL_MODULES),$(MODULE_TARGET_ONLY_$(m)))

# Firmware: the core plus the profile's modules; host tests: the core plus every module
SRCS = $(CORE_SRCS) $(foreach m,$(PROFILE_MODULES),$(MODULE_SRCS_$(m)))
HOST_SRCS = $(filter-out $(TARGET_ONLY_SRCS),$(CORE_SRCS) $(foreach m,$(ALL_MODULES),$(MODULE_SRCS_$(m))))

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

.PHONY: all flash erase_flash monitor clean test host-test elf-test profiles
all: $(BIN)

# The LP image header is generated into $(LP_DIR) (shared by all profiles); a leftover copy in src/ would shadow it
ifneq ($(wildcard src/lp_firmware_image.h),)
  $(error src/lp_firmware_image.h is stale (now generated in $(LP_DIR)); delete it)
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

# Rewritten only when the compressed page changes
$(WEB_GZ_H): $(WEB_SRC) scripts/gen_web.py
	@python3 scripts/gen_web.py $(WEB_SRC) $@

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

# Relinks when the module selection or the stack budget changes (config.mk is rewritten only then)
$(ELF): $(OBJS) $(wildcard ld/*.ld ld/rom/*.ld) $(VENDOR_STAMP) $(CONFIG_MK)
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

# ELF/BIN checks of one profile's image (layout, budgets, which modules are linked)
elf-test: $(ELF) $(BIN) $(HOST_TEST_BIN)
	@python3 tests/test_runner.py --elf $(ELF) --bin $(BIN) --host-test $(HOST_TEST_BIN) --objdump $(OBJDUMP) \
		--profile $(PROFILE) --modules "$(PROFILE_MODULES)" --main-stack-min $(PROFILE_MAIN_STACK_MIN)

# Host tests, then every profile built and checked: the test build and the production builds
test: host-test
	@for p in $(PROFILES); do $(MAKE) --no-print-directory PROFILE=$$p elf-test || exit 1; done

# Build every profile (build/<profile>/firmware.bin)
profiles:
	@for p in $(PROFILES); do $(MAKE) --no-print-directory PROFILE=$$p all || exit 1; done

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d) $(HOST_OBJS:.o=.d)

# Optional local, untracked targets (e.g. docs)
-include local.mk
