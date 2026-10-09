/*
 * src/memstat.c
 *
 * Live RAM budget (REV-32). The layout comes from the linker symbols (ld/link.ld), the peaks from the
 * subsystems that own the large buffers. Shell only: runs in the main loop, never with the flash cache off.
 */
#include "memstat.h"
#include "console.h"
#include "utils.h"
#include "string.h"
#include "arena.h"
#include "tcp.h"
#include "wifi.h"
#include "wifi_os_adapter.h"

#if defined(__riscv)
extern char _iram_region_start[], _iram_region_end[];
extern char _dram_region_start[], _dram_region_end[];
extern char _stext[], _etext[];
extern char _srodata[], _erodata[], _sdata[], _edata[], _sbss[], _ebss[];
extern char _main_stack_top[];
extern char _sflash_xip[], _eflash_xip[];
extern char MAIN_STACK_MIN_SIZE[];   /* absolute symbol: its address is the value */

static uint32_t memstat_span(const char *from, const char *to)
{
    return (uint32_t)((uintptr_t)to - (uintptr_t)from);
}
#endif

void memstat_get_layout(memstat_layout_t *out)
{
    if (out == NULL)
    {
        return;
    }
    memset(out, 0, sizeof(*out));
#if defined(__riscv)
    out->iram_size      = memstat_span(_iram_region_start, _iram_region_end);
    out->iram_code      = memstat_span(_stext, _etext);
    out->dram_size      = memstat_span(_dram_region_start, _dram_region_end);
    out->dram_rodata    = memstat_span(_srodata, _erodata);
    out->dram_data      = memstat_span(_sdata, _edata);
    out->dram_bss       = memstat_span(_sbss, _ebss);
    out->main_stack     = memstat_span(_ebss, _main_stack_top);
    out->main_stack_min = (uint32_t)(uintptr_t)MAIN_STACK_MIN_SIZE;
    out->top_reserve    = memstat_span(_main_stack_top, _dram_region_end);
    out->flash_xip      = memstat_span(_sflash_xip, _eflash_xip);
#endif
}

size_t memstat_stack_untouched(const uint32_t *bottom, size_t words)
{
    size_t n = 0U;
    while (bottom != NULL && n < words && bottom[n] == (uint32_t)MEMSTAT_STACK_FILL_WORD)
    {
        n++;
    }
    return n * sizeof(uint32_t);
}

uint32_t memstat_main_stack_peak(void)
{
#if defined(__riscv)
    uint32_t size = memstat_span(_ebss, _main_stack_top);
    size_t untouched = memstat_stack_untouched((const uint32_t *)(void *)_ebss, size / sizeof(uint32_t));
    return size - (uint32_t)untouched;
#else
    return 0U;
#endif
}

void memstat_reset_peaks(void)
{
#if defined(__riscv)
    /* Everything below the stack pointer is free; ISR frames pushed meanwhile are dead once they return */
    uintptr_t sp;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    uintptr_t end = (sp - MEMSTAT_REPAINT_GUARD_BYTES) & ~(uintptr_t)(sizeof(uint32_t) - 1U);
    for (uint32_t *p = (uint32_t *)(void *)_ebss; (uintptr_t)p < end; p++)
    {
        *p = (uint32_t)MEMSTAT_STACK_FILL_WORD;
    }
#endif
    wifi_rx_ring_peak_reset();
    tcp_sndbuf_peak_reset();
    wifi_os_adapter_heap_peak_reset();
}

/* "  <label>peak <peak> of <size>" */
static void memstat_peak(const char *label, uint32_t peak, uint32_t size, const char *unit)
{
    console_puts(label);
    console_puts("peak ");
    put_dec(peak);
    console_puts(" of ");
    put_dec(size);
    console_puts(unit);
}

static void memstat_bytes(const char *label, uint32_t bytes)
{
    console_puts(label);
    put_dec(bytes);
    console_puts(" B");
}

void memstat_shell(const char *args)
{
    while (args != NULL && *args == ' ')
    {
        args++;
    }
    if (args != NULL && strcmp(args, "reset") == 0)
    {
        memstat_reset_peaks();
        console_puts("Peaks restarted at their current values.\r\n");
    }
    else if (args != NULL && *args != '\0')
    {
        console_puts("usage: mem [reset]\r\n");
        return;
    }

    memstat_layout_t l;
    memstat_get_layout(&l);
    console_puts("RAM budget (peaks since boot or `mem reset`)\r\n");
    memstat_bytes("  IRAM ", l.iram_size);
    memstat_bytes(": code ", l.iram_code);
    memstat_bytes(", unused ", l.iram_size - l.iram_code);
    memstat_bytes("\r\n  DRAM ", l.dram_size);
    memstat_bytes(": constants ", l.dram_rodata);
    memstat_bytes(", data ", l.dram_data);
    memstat_bytes(", bss ", l.dram_bss);
    memstat_bytes(", main stack ", l.main_stack);
    memstat_bytes(", top reserve ", l.top_reserve);
    memstat_peak("\r\n  main stack: ", memstat_main_stack_peak(), l.main_stack, " B");
    memstat_bytes(" (link budget ", l.main_stack_min);
    memstat_bytes(")\r\n  flash: code + constants ", l.flash_xip);

    size_t heap_used = 0U, heap_free = 0U, heap_peak = 0U;
    wifi_os_adapter_get_heap_stats(&heap_used, &heap_free, &heap_peak);
    memstat_peak("\r\n  Wi-Fi heap: ", (uint32_t)heap_peak, (uint32_t)(heap_used + heap_free), " B");
    memstat_bytes(", now ", (uint32_t)heap_used);

#if defined(__riscv)
    uint32_t task_size = 0U, task_req = 0U;
    uint32_t task_free = wifi_os_adapter_task_stack_free(&task_size, &task_req);
    memstat_peak("\r\n  Wi-Fi task stack: ", task_size - task_free, task_size, " B");
    memstat_bytes(" (blob asked for ", task_req);
    console_puts(")");
#endif

    wifi_telemetry_t wt;
    memset(&wt, 0, sizeof(wt));
    (void)wifi_get_telemetry(&wt);
    memstat_peak("\r\n  RX ring: ", wt.rx_ring_peak, PACKET_RING_COUNT, " frames");
    memstat_bytes(" (", (uint32_t)sizeof(net_packet_t));
    console_puts(" each), now ");
    put_dec(wt.rx_ring_queued);
    console_puts(", dropped ");
    put_dec(wt.ring_full_drops);

    uint32_t chunks_now = 0U, chunks_peak = 0U;
    tcp_sndbuf_usage(&chunks_now, &chunks_peak);
    memstat_peak("\r\n  TCP send pool: ", chunks_peak, TCP_SNDBUF_CHUNKS, " chunks");
    memstat_bytes(" (", TCP_SNDBUF_CHUNK_SIZE);
    console_puts(" each), now ");
    put_dec(chunks_now);

    arena_telemetry_t at;
    arena_get_stats(&at);
    memstat_peak("\r\n  scratch arena: ", (uint32_t)at.scratch.high_watermark, (uint32_t)at.scratch.capacity, " B");
    memstat_peak("\r\n  pools: small ", at.small_pool.high_watermark, at.small_pool.block_count, " blocks");
    memstat_peak(", medium ", at.medium_pool.high_watermark, at.medium_pool.block_count, " blocks");
    console_puts("\r\n");
}
