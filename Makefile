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
CFLAGS = -march=rv32imac_zicsr_zifencei -mabi=ilp32 -ffreestanding -nostdlib -O2 -g -Wall -Wextra -Isrc

# Linker flags
LDFLAGS = -T ld/link.ld -nostdlib

# Baseline source files
SRCS = src/crt0.S src/main.c src/string.c src/utils.c src/test.c

# Targets
all: firmware.bin

firmware.elf: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

firmware.bin: firmware.elf
	esptool --chip esp32c6 elf2image --flash_mode dio --flash_size 4MB --flash_freq 80m -o $@ $<

clean:
	rm -f *.elf *.bin