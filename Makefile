CROSS_COMPILE = riscv64-unknown-elf-

CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy

# Compiler flags
CFLAGS = -march=rv32imac -mabi=ilp32 -ffreestanding -nostdlib -g -Wall -Isrc

# Linker flags
LDFLAGS = -T ld/link.ld -nostdlib

# Source files
SRCS = src/crt0.S src/main.c src/string.c

# Targets
all: firmware.bin

firmware.elf: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

firmware.bin: firmware.elf
	esptool --chip esp32c6 elf2image --flash_mode dio --flash_size 4MB --flash_freq 80m -o $@ $<

clean:
	rm -f *.elf *.bin