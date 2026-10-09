/*
 * src/memstat.h
 *
 * Live RAM budget (REV-32): the static layout of the HP SRAM from the linker symbols and the peaks of the
 * large buffers, shown by the `mem` shell command. Measurement only; nothing here changes behaviour.
 */
#ifndef IRON_V_MEMSTAT_H
#define IRON_V_MEMSTAT_H

/* crt0.S paints the main stack (_ebss .. _main_stack_top) with this word before main() runs; no U suffix,
 * the assembler reads it too */
#define MEMSTAT_STACK_FILL_WORD      0xA5A5A5A5

#ifndef __ASSEMBLER__
#include <stdint.h>
#include <stddef.h>

/* `mem reset` repaints the free main stack only this far below the caller's stack pointer */
#define MEMSTAT_REPAINT_GUARD_BYTES  256U

typedef struct {
    uint32_t iram_size;        /* hp_iram region */
    uint32_t iram_code;        /* .text: the IRAM keep-list */
    uint32_t dram_size;        /* hp_dram region */
    uint32_t dram_rodata;      /* .rodata left in DRAM */
    uint32_t dram_data;        /* .data */
    uint32_t dram_bss;         /* .bss */
    uint32_t main_stack;       /* _ebss .. _main_stack_top */
    uint32_t main_stack_min;   /* MAIN_STACK_MIN_SIZE (link-time budget) */
    uint32_t top_reserve;      /* _main_stack_top .. end of SRAM: ROM data and the ROM boot stack */
    uint32_t flash_xip;        /* code and constants run or read from flash */
} memstat_layout_t;

void memstat_get_layout(memstat_layout_t *out);

/* Bytes at the bottom of a painted stack that still hold MEMSTAT_STACK_FILL_WORD (never used) */
size_t memstat_stack_untouched(const uint32_t *bottom, size_t words);

/* Deepest main-stack use since boot or the last memstat_reset_peaks() */
uint32_t memstat_main_stack_peak(void);

/* Repaint the free main stack below the caller and restart every peak at its current value */
void memstat_reset_peaks(void);

/* Shell: `mem` prints the budget, `mem reset` restarts the peaks first */
void memstat_shell(const char *args);

#endif /* __ASSEMBLER__ */
#endif /* IRON_V_MEMSTAT_H */
