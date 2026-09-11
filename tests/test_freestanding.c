/*
 * tests/test_freestanding.c
 *
 * Authentic host-native unit test harness for Iron V freestanding runtime library.
 * Compiles natively with host GCC: gcc -O2 -Wall -Wextra -Isrc tests/test_freestanding.c src/string.c src/dpc.c -o tests/test_freestanding
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "string.h"
#include "dpc.h"
#include "console.h"
#include "arena.h"
#include "systimer.h"
#include "task.h"
#include "pmp.h"
#include "lp_core.h"
#include "power.h"
#include "gpio.h"
#include "gdma.h"
#include "modem.h"
#include "ble.h"
#include "ble_gatt.h"
#include "wifi.h"
#include "ieee802154.h"
#include "net.h"
#include "tcp.h"

/* Freestanding function aliases matching runtime naming conventions */
static inline size_t s_strlen(const char *s)
{
    return strlen(s);
}

static inline int s_strcmp(const char *s1, const char *s2)
{
    return strcmp(s1, s2);
}

static inline int s_strncmp(const char *s1, const char *s2, size_t n)
{
    return strncmp(s1, s2, n);
}

/* Freestanding integer to decimal ASCII string conversion */
static char *s_itoa(uint32_t val, char *buf, size_t buf_len)
{
    if (!buf || buf_len < 2) return NULL;
    if (val == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }

    char temp[16];
    int idx = 0;
    while (val > 0 && idx < 15)
    {
        temp[idx++] = (char)('0' + (val % 10));
        val /= 10;
    }

    if ((size_t)(idx + 1) > buf_len) return NULL;

    for (int i = 0; i < idx; i++)
    {
        buf[i] = temp[idx - 1 - i];
    }
    buf[idx] = '\0';
    return buf;
}

/* Freestanding integer to hexadecimal ASCII string conversion (prefixed with 0x) */
static char *s_hextoa(uint32_t val, char *buf, size_t buf_len)
{
    if (!buf || buf_len < 4) return NULL;
    const char hex_chars[] = "0123456789ABCDEF";
    char temp[16];
    int idx = 0;

    if (val == 0)
    {
        temp[idx++] = '0';
    }
    else
    {
        while (val > 0 && idx < 15)
        {
            temp[idx++] = hex_chars[val & 0x0F];
            val >>= 4;
        }
    }

    if ((size_t)(idx + 3) > buf_len) return NULL;

    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < idx; i++)
    {
        buf[2 + i] = temp[idx - 1 - i];
    }
    buf[2 + idx] = '\0';
    return buf;
}

static int g_assert_failures = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
        g_assert_failures++; \
    } \
} while (0)

static void test_s_strlen(void)
{
    printf("  [TEST] s_strlen & strlen...\n");
    TEST_ASSERT(s_strlen("") == 0, "empty string length must be 0");
    TEST_ASSERT(strlen("") == 0, "standard strlen empty check");
    TEST_ASSERT(s_strlen("a") == 1, "single character length must be 1");
    TEST_ASSERT(s_strlen("iron_v") == 6, "iron_v length must be 6");
    TEST_ASSERT(s_strlen("ESP32-C6") == 8, "ESP32-C6 length must be 8");
    TEST_ASSERT(s_strlen("IRON_V_RODATA_TEST_PATTERN") == 26, "rodata pattern length must be 26");
}

static void test_s_strcmp(void)
{
    printf("  [TEST] s_strcmp & strcmp...\n");
    TEST_ASSERT(s_strcmp("iron_v", "iron_v") == 0, "identical strings must compare equal");
    TEST_ASSERT(strcmp("iron_v", "iron_v") == 0, "standard strcmp identical check");
    TEST_ASSERT(s_strcmp("apple", "banana") < 0, "lexicographical less-than");
    TEST_ASSERT(s_strcmp("banana", "apple") > 0, "lexicographical greater-than");
    TEST_ASSERT(s_strcmp("", "") == 0, "two empty strings must be equal");
    TEST_ASSERT(s_strcmp("abc", "abcd") < 0, "shorter prefix must be less-than");
    TEST_ASSERT(s_strcmp("abcd", "abc") > 0, "longer string must be greater-than");
    TEST_ASSERT(s_strcmp("test1", "test2") < 0, "numeric suffix comparison");
}

static void test_s_strncmp(void)
{
    printf("  [TEST] s_strncmp & strncmp...\n");
    TEST_ASSERT(s_strncmp("iron_v_runtime", "iron_v_kernel", 6) == 0, "first 6 characters match");
    TEST_ASSERT(strncmp("iron_v_runtime", "iron_v_kernel", 6) == 0, "standard strncmp match");
    TEST_ASSERT(s_strncmp("iron_v_runtime", "iron_v_kernel", 8) != 0, "mismatch at character 7");
    TEST_ASSERT(s_strncmp("abc", "xyz", 0) == 0, "n=0 must always compare equal");
    TEST_ASSERT(s_strncmp("prefix_match", "prefix_diff", 7) == 0, "prefix match 7 chars");
    TEST_ASSERT(s_strncmp("prefix_match", "prefix_diff", 8) != 0, "prefix mismatch 8 chars");
}

static void test_s_htoi(void)
{
    printf("  [TEST] s_htoi hex parsing...\n");
    uint32_t val = 0;

    char h1[] = "0x40800000";
    char *p1 = h1;
    TEST_ASSERT(s_htoi(&p1, &val) == 1, "0x40800000 parsed successfully");
    TEST_ASSERT(val == 0x40800000U, "0x40800000 value matched");
    TEST_ASSERT(*p1 == '\0', "pointer advanced to end of string");

    char h2[] = "0XDEADBEEF";
    char *p2 = h2;
    TEST_ASSERT(s_htoi(&p2, &val) == 1, "0XDEADBEEF uppercase prefix parsed");
    TEST_ASSERT(val == 0xDEADBEEFU, "0xDEADBEEF value matched");
    TEST_ASSERT(*p2 == '\0', "pointer at terminator");

    char h3[] = "CAFEBABE";
    char *p3 = h3;
    TEST_ASSERT(s_htoi(&p3, &val) == 1, "CAFEBABE without prefix parsed");
    TEST_ASSERT(val == 0xCAFEBABEU, "0xCAFEBABE value matched");
    TEST_ASSERT(*p3 == '\0', "pointer at terminator");

    char h4[] = "0x0";
    char *p4 = h4;
    TEST_ASSERT(s_htoi(&p4, &val) == 1, "0x0 parsed successfully");
    TEST_ASSERT(val == 0, "0x0 value 0");

    char h5[] = "   0x1234 tail";
    char *p5 = h5;
    TEST_ASSERT(s_htoi(&p5, &val) == 1, "0x1234 with leading space parsed");
    TEST_ASSERT(val == 0x1234U, "0x1234 value matched");
    skip_space(&p5);
    TEST_ASSERT(strcmp(p5, "tail") == 0, "tail remainder verified");

    char h6[] = "0xXYZ";
    char *p6 = h6;
    TEST_ASSERT(s_htoi(&p6, &val) == 0, "invalid hex rejected");

    char h7[] = "";
    char *p7 = h7;
    TEST_ASSERT(s_htoi(&p7, &val) == 0, "empty string rejected");

    char h8[] = "   ";
    char *p8 = h8;
    TEST_ASSERT(s_htoi(&p8, &val) == 0, "whitespace-only rejected");
}

static void test_s_itoa(void)
{
    printf("  [TEST] s_itoa decimal formatting...\n");
    char buf[32];

    char *res = s_itoa(0, buf, sizeof(buf));
    TEST_ASSERT(res != NULL && strcmp(buf, "0") == 0, "itoa 0 formatting");

    res = s_itoa(1234, buf, sizeof(buf));
    TEST_ASSERT(res != NULL && strcmp(buf, "1234") == 0, "itoa 1234 formatting");

    res = s_itoa(40800000, buf, sizeof(buf));
    TEST_ASSERT(res != NULL && strcmp(buf, "40800000") == 0, "itoa 40800000 formatting");

    res = s_itoa(999, buf, 2);
    TEST_ASSERT(res == NULL, "itoa buffer overflow protection");
}

static void test_s_hextoa(void)
{
    printf("  [TEST] s_hextoa hex formatting...\n");
    char buf[32];

    char *res = s_hextoa(0, buf, sizeof(buf));
    TEST_ASSERT(res != NULL && strcmp(buf, "0x0") == 0, "hextoa 0 formatting");

    res = s_hextoa(0x1234, buf, sizeof(buf));
    TEST_ASSERT(res != NULL && strcmp(buf, "0x1234") == 0, "hextoa 0x1234 formatting");

    res = s_hextoa(0xDEADBEEF, buf, sizeof(buf));
    TEST_ASSERT(res != NULL && strcmp(buf, "0xDEADBEEF") == 0, "hextoa 0xDEADBEEF formatting");

    res = s_hextoa(0xCAFE, buf, 4);
    TEST_ASSERT(res == NULL, "hextoa buffer overflow protection");
}

static void test_memory_utils(void)
{
    printf("  [TEST] memset & memcpy...\n");
    uint8_t buffer[32];
    void *ret = memset(buffer, 0xA5, sizeof(buffer));
    TEST_ASSERT(ret == (void *)buffer, "memset returns target buffer");
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
        TEST_ASSERT(buffer[i] == 0xA5, "memset buffer content check");
    }

    memset(buffer + 8, 0x00, 16);
    for (size_t i = 0; i < 8; i++) TEST_ASSERT(buffer[i] == 0xA5, "memset prefix check");
    for (size_t i = 8; i < 24; i++) TEST_ASSERT(buffer[i] == 0x00, "memset span check");
    for (size_t i = 24; i < 32; i++) TEST_ASSERT(buffer[i] == 0xA5, "memset suffix check");

    const uint8_t src[16] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
                             0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0xFF};
    uint8_t dest[16];
    memset(dest, 0, sizeof(dest));
    void *cp_ret = memcpy(dest, src, sizeof(dest));
    TEST_ASSERT(cp_ret == (void *)dest, "memcpy returns dest buffer");
    for (size_t i = 0; i < sizeof(dest); i++)
    {
        TEST_ASSERT(dest[i] == src[i], "memcpy byte exact match");
    }
}

static void test_char_helpers(void)
{
    printf("  [TEST] is_hex & skip_space...\n");
    for (char c = '0'; c <= '9'; c++) TEST_ASSERT(is_hex(c) == (c - '0'), "digit hex decode");
    for (char c = 'a'; c <= 'f'; c++) TEST_ASSERT(is_hex(c) == (c - 'a' + 10), "lowercase hex decode");
    for (char c = 'A'; c <= 'F'; c++) TEST_ASSERT(is_hex(c) == (c - 'A' + 10), "uppercase hex decode");

    TEST_ASSERT(is_hex('g') == -1, "g is not hex");
    TEST_ASSERT(is_hex('G') == -1, "G is not hex");
    TEST_ASSERT(is_hex('x') == -1, "x is not hex");
    TEST_ASSERT(is_hex(' ') == -1, "space is not hex");
    TEST_ASSERT(is_hex('\0') == -1, "null terminator is not hex");

    char s1[] = "   \t\t  hello";
    char *p1 = s1;
    skip_space(&p1);
    TEST_ASSERT(strcmp(p1, "hello") == 0, "skip_space whitespace trimmed");

    char s2[] = "nowhitespace";
    char *p2 = s2;
    skip_space(&p2);
    TEST_ASSERT(strcmp(p2, "nowhitespace") == 0, "skip_space no whitespace");

    char s3[] = "   ";
    char *p3 = s3;
    skip_space(&p3);
    TEST_ASSERT(*p3 == '\0', "skip_space all whitespace reaches null");
}

static uint32_t g_host_dpc_handler_hit = 0;
static uint32_t g_host_dpc_last_arg0 = 0;
static uint32_t g_host_dpc_last_arg1 = 0;

static void host_test_dpc_handler(uint32_t a0, uint32_t a1)
{
    g_host_dpc_handler_hit++;
    g_host_dpc_last_arg0 = a0;
    g_host_dpc_last_arg1 = a1;
}

static void test_dpc_queue(void)
{
    printf("  [TEST] dpc_queue lock-free SPSC engine...\n");

    dpc_queue_t q;
    dpc_queue_init(&q);

    TEST_ASSERT(dpc_queue_size(&q) == 0, "initial queue size is 0");
    TEST_ASSERT(dpc_queue_is_empty(&q) == 1, "initial queue is empty");
    TEST_ASSERT(dpc_queue_is_full(&q) == 0, "initial queue is not full");
    TEST_ASSERT(q.drop_count == 0, "initial drop count is 0");

    dpc_event_t dummy;
    TEST_ASSERT(dpc_queue_dequeue(&q, &dummy) == DPC_STATUS_ERR_EMPTY, "dequeue from empty returns ERR_EMPTY");

    /* 1. Fill exactly DPC_QUEUE_CAPACITY (64) entries */
    for (uint32_t i = 0; i < DPC_QUEUE_CAPACITY; i++)
    {
        dpc_event_t ev;
        ev.type = DPC_TYPE_TIMER_TICK;
        ev.arg0 = i;
        ev.arg1 = i * 100U;
        ev.handler = host_test_dpc_handler;

        int res = dpc_queue_enqueue(&q, &ev);
        TEST_ASSERT(res == DPC_STATUS_OK, "enqueue within capacity succeeds");
    }

    TEST_ASSERT(dpc_queue_size(&q) == DPC_QUEUE_CAPACITY, "queue size is 64 when full");
    TEST_ASSERT(dpc_queue_is_full(&q) == 1, "queue is full");
    TEST_ASSERT(dpc_queue_is_empty(&q) == 0, "full queue is not empty");

    /* 2. Attempt 65th enqueue: assert failure and drop_count increment */
    dpc_event_t ev65;
    ev65.type = DPC_TYPE_WIFI_PACKET;
    ev65.arg0 = 999U;
    ev65.arg1 = 888U;
    ev65.handler = host_test_dpc_handler;

    int res65 = dpc_queue_enqueue(&q, &ev65);
    TEST_ASSERT(res65 == DPC_STATUS_ERR_FULL, "65th enqueue rejected with ERR_FULL");
    TEST_ASSERT(q.drop_count == 1, "drop_count incremented to 1");
    TEST_ASSERT(dpc_queue_size(&q) == DPC_QUEUE_CAPACITY, "queue size remains 64 after drop");

    /* 3. Drain all 64 entries and verify strict FIFO ordering */
    for (uint32_t i = 0; i < DPC_QUEUE_CAPACITY; i++)
    {
        dpc_event_t out;
        int dq_res = dpc_queue_dequeue(&q, &out);
        TEST_ASSERT(dq_res == DPC_STATUS_OK, "dequeue succeeds");
        TEST_ASSERT(out.type == DPC_TYPE_TIMER_TICK, "event type matches");
        TEST_ASSERT(out.arg0 == i, "FIFO order: arg0 matches index");
        TEST_ASSERT(out.arg1 == i * 100U, "FIFO order: arg1 matches expected");
        TEST_ASSERT(out.handler == host_test_dpc_handler, "handler pointer matches");
    }

    /* 4. Assert empty condition and head == tail */
    TEST_ASSERT(q.head == q.tail, "after drain: head == tail");
    TEST_ASSERT(dpc_queue_size(&q) == 0, "after drain: size == 0");
    TEST_ASSERT(dpc_queue_is_empty(&q) == 1, "after drain: queue is empty");
    TEST_ASSERT(dpc_queue_dequeue(&q, &dummy) == DPC_STATUS_ERR_EMPTY, "dequeue after drain returns ERR_EMPTY");

    /* 5. Circular wrap-around stress test */
    for (uint32_t round = 0; round < 4; round++)
    {
        for (uint32_t k = 0; k < 32; k++)
        {
            dpc_event_t ev;
            ev.type = DPC_TYPE_UART0_RX;
            ev.arg0 = round * 100U + k;
            ev.arg1 = k;
            ev.handler = host_test_dpc_handler;
            TEST_ASSERT(dpc_queue_enqueue(&q, &ev) == DPC_STATUS_OK, "wraparound enqueue succeeds");
        }
        for (uint32_t k = 0; k < 32; k++)
        {
            dpc_event_t out;
            TEST_ASSERT(dpc_queue_dequeue(&q, &out) == DPC_STATUS_OK, "wraparound dequeue succeeds");
            TEST_ASSERT(out.arg0 == round * 100U + k, "wraparound FIFO arg0 matches");
        }
    }
    TEST_ASSERT(q.head == q.tail, "after wraparound rounds: head == tail");

    /* 6. Global DPC system engine test */
    dpc_init();
    g_host_dpc_handler_hit = 0;
    TEST_ASSERT(dpc_enqueue(DPC_TYPE_USB_SERIAL_RX, 42U, 84U, host_test_dpc_handler) == DPC_STATUS_OK, "global enqueue succeeds");
    TEST_ASSERT(dpc_get_size() == 1, "global queue size is 1");
    int proc_res = dpc_process();
    TEST_ASSERT(proc_res == 1, "dpc_process dispatched 1 event");
    TEST_ASSERT(g_host_dpc_handler_hit == 1, "handler was executed");
    TEST_ASSERT(g_host_dpc_last_arg0 == 42U, "handler received arg0");
    TEST_ASSERT(g_host_dpc_last_arg1 == 84U, "handler received arg1");
    TEST_ASSERT(dpc_process() == 0, "dpc_process returns 0 when empty");

    /* 7. Timer tick DPC event enqueue & execution */
    g_host_dpc_handler_hit = 0;
    TEST_ASSERT(dpc_enqueue(DPC_TYPE_TIMER_TICK, 77U, 99U, host_test_dpc_handler) == DPC_STATUS_OK, "timer tick dpc enqueue succeeds");
    TEST_ASSERT(dpc_process() == 1, "timer tick dpc processed");
    TEST_ASSERT(g_host_dpc_handler_hit == 1, "timer tick handler hit");
    TEST_ASSERT(g_host_dpc_last_arg0 == 77U, "timer tick received tick count");
}

static int g_mock_uart_putc_calls = 0;
static int g_mock_usb_putc_calls = 0;
static char g_mock_uart_last_c = '\0';
static char g_mock_usb_last_c = '\0';

static void mock_uart_putc(char c)
{
    g_mock_uart_putc_calls++;
    g_mock_uart_last_c = c;
}

static void mock_usb_putc(char c)
{
    g_mock_usb_putc_calls++;
    g_mock_usb_last_c = c;
}

static int mock_uart_getc(char *c)
{
    *c = 'U';
    return 1;
}

static int mock_usb_getc(char *c)
{
    *c = 'J';
    return 1;
}

static void test_console_multiplexer(void)
{
    printf("  [TEST] console multiplexer backend structure & dispatch...\n");

    console_manager_t mgr;
    mgr.uart.putc = mock_uart_putc;
    mgr.uart.puts = NULL;
    mgr.uart.getc_nonblocking = mock_uart_getc;
    mgr.uart.flush = NULL;

    mgr.usb.putc = mock_usb_putc;
    mgr.usb.puts = NULL;
    mgr.usb.getc_nonblocking = mock_usb_getc;
    mgr.usb.flush = NULL;

    mgr.echo_enabled = 1U;
    mgr.active_mask = CONSOLE_MASK_ALL;

    TEST_ASSERT(mgr.active_mask == (CONSOLE_MASK_UART0 | CONSOLE_MASK_USB), "active mask includes both ports");
    TEST_ASSERT(mgr.echo_enabled == 1U, "echo enabled by default");

    /* Test UART putc dispatch */
    g_mock_uart_putc_calls = 0;
    mgr.uart.putc('A');
    TEST_ASSERT(g_mock_uart_putc_calls == 1, "mock uart putc invoked");
    TEST_ASSERT(g_mock_uart_last_c == 'A', "mock uart putc received character 'A'");

    /* Test USB putc dispatch */
    g_mock_usb_putc_calls = 0;
    mgr.usb.putc('B');
    TEST_ASSERT(g_mock_usb_putc_calls == 1, "mock usb putc invoked");
    TEST_ASSERT(g_mock_usb_last_c == 'B', "mock usb putc received character 'B'");

    /* Test getc */
    char c = '\0';
    TEST_ASSERT(mgr.uart.getc_nonblocking(&c) == 1, "mock uart getc succeeds");
    TEST_ASSERT(c == 'U', "mock uart getc received 'U'");

    TEST_ASSERT(mgr.usb.getc_nonblocking(&c) == 1, "mock usb getc succeeds");
    TEST_ASSERT(c == 'J', "mock usb getc received 'J'");
}

static void test_arena_allocator(void)
{
    printf("  [TEST] static arena memory allocator & linear scratch...\n");

    arena_init();

    /* 1. Initial State Verification */
    arena_telemetry_t init_telem;
    arena_get_stats(&init_telem);
    TEST_ASSERT(init_telem.small_pool.active_count == 0U, "small pool initial active count 0");
    TEST_ASSERT(init_telem.small_pool.allocated_mask == ARENA_BITMASK_EMPTY, "small pool initial mask empty");
    TEST_ASSERT(init_telem.medium_pool.active_count == 0U, "medium pool initial active count 0");
    TEST_ASSERT(init_telem.medium_pool.allocated_mask == ARENA_BITMASK_EMPTY, "medium pool initial mask empty");
    TEST_ASSERT(init_telem.scratch.current_offset == 0U, "scratch initial offset 0");

    /* 2. Small Pool Exhaustion & Alignment (32 blocks of 64 bytes) */
    void *small_ptrs[ARENA_POOL_BLOCK_COUNT_SMALL];
    for (uint32_t i = 0; i < ARENA_POOL_BLOCK_COUNT_SMALL; i++)
    {
        small_ptrs[i] = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
        TEST_ASSERT(small_ptrs[i] != NULL, "small allocation must succeed");
        TEST_ASSERT(((uintptr_t)small_ptrs[i] & ARENA_ALIGN_MASK) == 0, "small allocation 4-byte aligned");

        for (uint32_t j = 0; j < i; j++)
        {
            TEST_ASSERT(small_ptrs[j] != small_ptrs[i], "small allocation address must be unique");
        }
    }

    /* 33rd small allocation must fail (exhaustion guard) */
    void *small_overflow = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
    TEST_ASSERT(small_overflow == NULL, "33rd small allocation must return NULL");

    arena_pool_stats_t small_stats;
    arena_get_pool_stats(ARENA_POOL_SMALL, &small_stats);
    TEST_ASSERT(small_stats.active_count == ARENA_POOL_BLOCK_COUNT_SMALL, "small pool active count equals capacity");
    TEST_ASSERT(small_stats.allocated_mask == ARENA_BITMASK_FULL_SMALL, "small pool allocated mask full");

    /* 3. Free Guards (double free, unaligned, out-of-bounds) */
    void *block15 = small_ptrs[15];
    TEST_ASSERT(arena_free(block15) == ARENA_FREE_SUCCESS, "freeing block 15 succeeds");
    TEST_ASSERT(arena_free(block15) == ARENA_FREE_FAIL, "double free of block 15 rejected");
    TEST_ASSERT(arena_free((uint8_t *)block15 + 1) == ARENA_FREE_FAIL, "unaligned pointer free rejected");
    TEST_ASSERT(arena_free(NULL) == ARENA_FREE_FAIL, "freeing NULL rejected");
    uint32_t stack_dummy = 0;
    TEST_ASSERT(arena_free(&stack_dummy) == ARENA_FREE_FAIL, "freeing stack pointer rejected");

    /* Re-allocation after free must reuse block 15 in O(1) */
    void *realloc_block15 = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
    TEST_ASSERT(realloc_block15 == block15, "block 15 must be reused upon re-allocation");
    small_ptrs[15] = realloc_block15;

    /* Free all small blocks */
    for (uint32_t i = 0; i < ARENA_POOL_BLOCK_COUNT_SMALL; i++)
    {
        TEST_ASSERT(arena_free(small_ptrs[i]) == ARENA_FREE_SUCCESS, "freeing all small blocks");
    }
    arena_get_pool_stats(ARENA_POOL_SMALL, &small_stats);
    TEST_ASSERT(small_stats.active_count == 0U, "small pool active count 0 after freeing all");
    TEST_ASSERT(small_stats.allocated_mask == ARENA_BITMASK_EMPTY, "small pool mask empty after freeing all");

    /* 4. Medium Pool Exhaustion & Reuse (16 blocks of 256 bytes) */
    void *med_ptrs[ARENA_POOL_BLOCK_COUNT_MEDIUM];
    for (uint32_t i = 0; i < ARENA_POOL_BLOCK_COUNT_MEDIUM; i++)
    {
        med_ptrs[i] = arena_alloc(ARENA_POOL_BLOCK_SIZE_MEDIUM);
        TEST_ASSERT(med_ptrs[i] != NULL, "medium allocation must succeed");
        TEST_ASSERT(((uintptr_t)med_ptrs[i] & ARENA_ALIGN_MASK) == 0, "medium allocation 4-byte aligned");
        for (uint32_t j = 0; j < i; j++)
        {
            TEST_ASSERT(med_ptrs[j] != med_ptrs[i], "medium allocation address must be unique");
        }
    }

    void *med_overflow = arena_alloc(ARENA_POOL_BLOCK_SIZE_MEDIUM);
    TEST_ASSERT(med_overflow == NULL, "17th medium allocation must return NULL");

    void *block7 = med_ptrs[7];
    TEST_ASSERT(arena_free(block7) == ARENA_FREE_SUCCESS, "freeing medium block 7 succeeds");
    void *realloc_block7 = arena_alloc(ARENA_POOL_BLOCK_SIZE_MEDIUM);
    TEST_ASSERT(realloc_block7 == block7, "medium block 7 must be reused upon re-allocation");
    med_ptrs[7] = realloc_block7;

    for (uint32_t i = 0; i < ARENA_POOL_BLOCK_COUNT_MEDIUM; i++)
    {
        TEST_ASSERT(arena_free(med_ptrs[i]) == ARENA_FREE_SUCCESS, "freeing all medium blocks");
    }

    /* 5. Size-Based Dispatch Logic */
    TEST_ASSERT(arena_alloc(0U) == NULL, "alloc(0) returns NULL");
    void *alloc_1 = arena_alloc(1U);
    TEST_ASSERT(alloc_1 != NULL, "alloc(1) succeeds from small pool");
    void *alloc_64 = arena_alloc(64U);
    TEST_ASSERT(alloc_64 != NULL, "alloc(64) succeeds from small pool");
    void *alloc_65 = arena_alloc(65U);
    TEST_ASSERT(alloc_65 != NULL, "alloc(65) succeeds from medium pool");
    void *alloc_256 = arena_alloc(256U);
    TEST_ASSERT(alloc_256 != NULL, "alloc(256) succeeds from medium pool");
    void *alloc_257 = arena_alloc(257U);
    TEST_ASSERT(alloc_257 == NULL, "alloc(257) exceeds medium pool and returns NULL");

    TEST_ASSERT(arena_free(alloc_1) == ARENA_FREE_SUCCESS, "free alloc_1");
    TEST_ASSERT(arena_free(alloc_64) == ARENA_FREE_SUCCESS, "free alloc_64");
    TEST_ASSERT(arena_free(alloc_65) == ARENA_FREE_SUCCESS, "free alloc_65");
    TEST_ASSERT(arena_free(alloc_256) == ARENA_FREE_SUCCESS, "free alloc_256");

    /* 6. Linear Scratch Arena Mark & Reset */
    arena_scratch_reset(0U);
    arena_scratch_mark_t mark0 = arena_scratch_mark();
    TEST_ASSERT(mark0 == 0U, "initial scratch mark is 0");

    void *sc1 = arena_scratch_alloc(100U);
    TEST_ASSERT(sc1 != NULL, "scratch alloc 100 succeeds");
    TEST_ASSERT(((uintptr_t)sc1 & ARENA_ALIGN_MASK) == 0, "scratch alloc 4-byte aligned");

    void *sc2 = arena_scratch_alloc(200U);
    TEST_ASSERT(sc2 != NULL, "scratch alloc 200 succeeds");
    TEST_ASSERT(sc2 > sc1, "scratch alloc advances linearly");

    arena_scratch_mark_t mark1 = arena_scratch_mark();
    void *sc3 = arena_scratch_alloc(300U);
    TEST_ASSERT(sc3 != NULL, "scratch alloc 300 succeeds");

    arena_scratch_reset(mark1);
    void *sc4 = arena_scratch_alloc(300U);
    TEST_ASSERT(sc4 == sc3, "resetting to mark1 restores pointer for subsequent alloc");

    arena_scratch_reset(mark0);
    void *sc5 = arena_scratch_alloc(100U);
    TEST_ASSERT(sc5 == sc1, "resetting to mark0 restores original pointer sc1");

    /* Test scratch exhaustion guard & integer overflow attacks */
    arena_scratch_reset(0U);
    void *sc_huge = arena_scratch_alloc(ARENA_SCRATCH_TOTAL_SIZE + 1U);
    TEST_ASSERT(sc_huge == NULL, "scratch alloc exceeding capacity returns NULL");

    void *sc_overflow1 = arena_scratch_alloc((size_t)-1);
    TEST_ASSERT(sc_overflow1 == NULL, "scratch alloc (size_t)-1 rejected via overflow guard");

    void *sc_overflow2 = arena_scratch_alloc((size_t)-3);
    TEST_ASSERT(sc_overflow2 == NULL, "scratch alloc (size_t)-3 rejected via overflow guard");

    void *sc_overflow3 = arena_scratch_alloc(0xFFFFFFFFU);
    TEST_ASSERT(sc_overflow3 == NULL, "scratch alloc 0xFFFFFFFF rejected via overflow guard");

    /* Test mark/reset validation (unaligned mark and forward reset rejection) */
    void *sc_base = arena_scratch_alloc(64U);
    TEST_ASSERT(sc_base != NULL, "scratch alloc 64 succeeds");
    arena_scratch_mark_t current_mark = arena_scratch_mark();
    TEST_ASSERT(current_mark == 64U, "current scratch mark is 64");

    /* Attempt unaligned reset: must be ignored */
    arena_scratch_reset(3U);
    TEST_ASSERT(arena_scratch_mark() == 64U, "unaligned reset(3) rejected; mark unchanged");

    /* Attempt forward reset beyond current offset: must be ignored */
    arena_scratch_reset(128U);
    TEST_ASSERT(arena_scratch_mark() == 64U, "forward reset(128) rejected; mark unchanged");

    /* Valid rewind reset */
    arena_scratch_reset(0U);
    TEST_ASSERT(arena_scratch_mark() == 0U, "reset to 0 restores mark to 0");

    /* 7. Robustness and Telemetry Edge Cases */
    TEST_ASSERT(arena_alloc_pool((arena_pool_id_t)99) == NULL, "invalid pool_id alloc returns NULL");
    TEST_ASSERT(arena_free((void *)0x10) == ARENA_FREE_FAIL, "freeing low unmapped pointer rejected");
    TEST_ASSERT(arena_free((void *)64) == ARENA_FREE_FAIL, "freeing arbitrary low pointer rejected");

    arena_get_stats(NULL); /* Safe no-op */
    arena_get_scratch_stats(NULL); /* Safe no-op */
    arena_pool_stats_t invalid_pool_stats;
    arena_get_pool_stats((arena_pool_id_t)99, &invalid_pool_stats);
    TEST_ASSERT(invalid_pool_stats.block_size == 0U, "invalid pool stats returns zeroed struct");
}

static void test_systimer_timebase(void)
{
    /* 1. Tick conversion correctness */
    uint64_t ticks_1us = 16ULL;
    TEST_ASSERT((ticks_1us >> SYSTIMER_TICKS_TO_US_SHIFT) == 1ULL, "16 ticks is exactly 1 us via shift");
    TEST_ASSERT((ticks_1us / SYSTIMER_TICKS_PER_US) == 1ULL, "16 ticks is 1 us via division");

    uint64_t ticks_1s = SYSTIMER_TICKS_PER_SEC;
    uint64_t us_1s = ticks_1s >> SYSTIMER_TICKS_TO_US_SHIFT;
    TEST_ASSERT(us_1s == US_PER_SECOND, "16,000,000 ticks is 1,000,000 us");
    TEST_ASSERT((us_1s / US_PER_MS) == MS_PER_SECOND, "1,000,000 us is 1,000 ms");
    TEST_ASSERT((us_1s / US_PER_SECOND) == 1ULL, "1,000,000 us is 1 second");

    /* 2. Precision remainders */
    uint64_t arbitrary_us = 12345678ULL; /* 12.345678 seconds */
    uint32_t sec = (uint32_t)(arbitrary_us / US_PER_SECOND);
    uint32_t ms_rem = (uint32_t)((arbitrary_us % US_PER_SECOND) / US_PER_MS);
    uint32_t us_rem = (uint32_t)(arbitrary_us % US_PER_MS);
    TEST_ASSERT(sec == 12U, "12345678 us has 12 seconds");
    TEST_ASSERT(ms_rem == 345U, "12345678 us has 345 ms remainder");
    TEST_ASSERT(us_rem == 678U, "12345678 us has 678 us remainder");

    /* 3. Uptime calendar math */
    uint32_t uptime_test_sec = 90061U; /* 1 day (86400) + 1 hour (3600) + 1 min (60) + 1 sec (1) */
    uint32_t d = uptime_test_sec / SEC_PER_DAY;
    uint32_t rem = uptime_test_sec % SEC_PER_DAY;
    uint32_t h = rem / SEC_PER_HOUR;
    rem %= SEC_PER_HOUR;
    uint32_t m = rem / SEC_PER_MINUTE;
    uint32_t s = rem % SEC_PER_MINUTE;
    TEST_ASSERT(d == 1U, "90061s is 1 day");
    TEST_ASSERT(h == 1U, "90061s is 1 hour");
    TEST_ASSERT(m == 1U, "90061s is 1 minute");
    TEST_ASSERT(s == 1U, "90061s is 1 second");

    /* 4. Bitfield limits & clamp boundaries */
    TEST_ASSERT(SYSTIMER_MAX_COUNTER_TICKS == 0x000FFFFFFFFFFFFFULL, "52-bit counter maximum is 0xFFFFFFFFFFFFF");
    TEST_ASSERT(SYSTIMER_MAX_PERIOD_TICKS == 0x03FFFFFFU, "26-bit period maximum is 0x3FFFFFF");
    TEST_ASSERT(SYSTIMER_MAX_PERIOD_US == (0x03FFFFFFU / 16U), "Max period us matches tick shift");

    /* 5. Parameterized register calculation offsets */
    TEST_ASSERT((uintptr_t)SYSTIMER_UNIT_OP_REG(0) == (SYSTIMER_BASE_ADDR + 0x04U), "Unit 0 OP offset is 0x04");
    TEST_ASSERT((uintptr_t)SYSTIMER_UNIT_OP_REG(1) == (SYSTIMER_BASE_ADDR + 0x08U), "Unit 1 OP offset is 0x08");
    TEST_ASSERT((uintptr_t)SYSTIMER_TARGET_CONF_REG(0) == (SYSTIMER_BASE_ADDR + 0x34U), "Target 0 CONF offset is 0x34");
    TEST_ASSERT((uintptr_t)SYSTIMER_TARGET_CONF_REG(1) == (SYSTIMER_BASE_ADDR + 0x38U), "Target 1 CONF offset is 0x38");
    TEST_ASSERT((uintptr_t)SYSTIMER_TARGET_CONF_REG(2) == (SYSTIMER_BASE_ADDR + 0x3CU), "Target 2 CONF offset is 0x3C");
    TEST_ASSERT((uintptr_t)SYSTIMER_COMP_LOAD_REG(0) == (SYSTIMER_BASE_ADDR + 0x50U), "COMP0 LOAD offset is 0x50");
}

static void test_task_structures(void)
{
    /* 1. Alignment geometry */
    uint32_t base = 0x40821003U; /* Misaligned base */
    uint32_t size = 1024U;
    uint32_t top = (base + size) & ~(TASK_STACK_ALIGNMENT - 1U);
    TEST_ASSERT((top % TASK_STACK_ALIGNMENT) == 0U, "Stack top is 16-byte aligned");

    uint32_t frame_sp = top - TASK_FRAME_SIZE;
    TEST_ASSERT((frame_sp % TASK_STACK_ALIGNMENT) == 0U, "Frame SP is 16-byte aligned");
    TEST_ASSERT(TASK_FRAME_SIZE == 64U, "Task frame size is 64 bytes (16 words)");

    /* 2. Priority and Task Limits */
    TEST_ASSERT(TASK_MAX_COUNT == 8U, "Max tasks is 8");
    TEST_ASSERT(TASK_PRIORITY_MIN == 1U, "Min priority is 1");
    TEST_ASSERT(TASK_PRIORITY_MAX == 15U, "Max priority is 15");
    TEST_ASSERT(TASK_DEFAULT_STACK_SIZE == 2048U, "Default stack size is 2048");
}

static void test_pmp_apm_isolation(void)
{
    /* 1. NAPOT Encoding & Decoding Correctness */
    uint32_t pmpaddr = 0;
    uint32_t base = 0;
    uint32_t len = 0;

    /* 8-byte region */
    TEST_ASSERT(pmp_calc_napot(0x40800000U, 8U, &pmpaddr) == PMP_OK, "8B NAPOT encode succeeds");
    TEST_ASSERT(pmpaddr == (0x40800000U >> 2U), "8B NAPOT pmpaddr has 0 in trailing bit");
    TEST_ASSERT(pmp_decode_napot(pmpaddr, &base, &len) == PMP_OK, "8B NAPOT decode succeeds");
    TEST_ASSERT(base == 0x40800000U && len == 8U, "8B NAPOT decode matches 0x40800000 / 8B");

    /* 16-byte region */
    TEST_ASSERT(pmp_calc_napot(0x40800000U, 16U, &pmpaddr) == PMP_OK, "16B NAPOT encode succeeds");
    TEST_ASSERT(pmpaddr == ((0x40800000U >> 2U) | 1U), "16B NAPOT pmpaddr has 1 trailing one");
    TEST_ASSERT(pmp_decode_napot(pmpaddr, &base, &len) == PMP_OK, "16B NAPOT decode succeeds");
    TEST_ASSERT(base == 0x40800000U && len == 16U, "16B NAPOT decode matches 0x40800000 / 16B");

    /* 64KB region (Kernel Data DRAM: 0x40820000) */
    TEST_ASSERT(pmp_calc_napot(0x40820000U, 65536U, &pmpaddr) == PMP_OK, "64KB NAPOT encode succeeds");
    TEST_ASSERT(pmpaddr == 0x10209FFFU, "64KB NAPOT pmpaddr is exactly 0x10209FFF");
    TEST_ASSERT(pmp_decode_napot(pmpaddr, &base, &len) == PMP_OK, "64KB NAPOT decode succeeds");
    TEST_ASSERT(base == 0x40820000U && len == 65536U, "64KB NAPOT decode recovers base 0x40820000 and len 65536");

    /* 1MB region (Flash XIP: 0x42000000) */
    TEST_ASSERT(pmp_calc_napot(0x42000000U, 1048576U, &pmpaddr) == PMP_OK, "1MB NAPOT encode succeeds");
    TEST_ASSERT(pmp_decode_napot(pmpaddr, &base, &len) == PMP_OK, "1MB NAPOT decode succeeds");
    TEST_ASSERT(base == 0x42000000U && len == 1048576U, "1MB NAPOT decode recovers base 0x42000000 and len 1MB");

    /* 2. Error Rejection & Boundary Conditions */
    TEST_ASSERT(pmp_calc_napot(0x40800000U, 4U, &pmpaddr) == PMP_ERR_INVALID_LEN, "NAPOT rejects < 8B");
    TEST_ASSERT(pmp_calc_napot(0x40800000U, 100U, &pmpaddr) == PMP_ERR_INVALID_LEN, "NAPOT rejects non-power-of-2");
    TEST_ASSERT(pmp_calc_napot(0x40800004U, 64U, &pmpaddr) == PMP_ERR_INVALID_ALIGN, "NAPOT rejects misaligned base");
    TEST_ASSERT(pmp_calc_napot(0x40800000U, 64U, NULL) == PMP_ERR_NULL_PTR, "NAPOT rejects NULL output pointer");
    TEST_ASSERT(pmp_decode_napot(0x10209FFFU, NULL, &len) == PMP_ERR_NULL_PTR, "Decode rejects NULL base");

    /* 3. PMP Set / Get / Disable with Mock CSRs */
    pmp_init();
    pmp_region_cfg_t rcfg = {
        .region_idx = 0U,
        .start_addr = 0x40820000U,
        .length = 65536U,
        .read_allow = 1U,
        .write_allow = 0U,
        .execute_allow = 0U,
        .lock = 0U,
        .addr_mode = (uint8_t)PMP_ADDR_MODE_NAPOT
    };
    TEST_ASSERT(pmp_set_region(&rcfg) == PMP_OK, "pmp_set_region succeeds for Region 0");
    uint32_t cfg0 = pmp_read_cfg(0U);
    TEST_ASSERT((cfg0 & 0xFFU) == 0x19U, "pmpcfg0 byte 0 is 0x19 (R=1, NAPOT)");
    TEST_ASSERT(pmp_read_addr(0U) == 0x10209FFFU, "pmpaddr0 is 0x10209FFF");

    pmp_region_cfg_t rb_cfg;
    TEST_ASSERT(pmp_get_region(0U, &rb_cfg) == PMP_OK, "pmp_get_region succeeds");
    TEST_ASSERT(rb_cfg.start_addr == 0x40820000U, "readback start_addr is 0x40820000");
    TEST_ASSERT(rb_cfg.length == 65536U, "readback length is 65536");
    TEST_ASSERT(rb_cfg.read_allow == 1U && rb_cfg.write_allow == 0U && rb_cfg.execute_allow == 0U, "readback permissions match");
    TEST_ASSERT(rb_cfg.addr_mode == (uint8_t)PMP_ADDR_MODE_NAPOT, "readback addr_mode is NAPOT");

    TEST_ASSERT(pmp_disable_region(0U) == PMP_OK, "pmp_disable_region succeeds");
    TEST_ASSERT((pmp_read_cfg(0U) & 0xFFU) == 0U, "pmpcfg0 byte 0 cleared after disable");

    rcfg.region_idx = 4U;
    TEST_ASSERT(pmp_set_region(&rcfg) == PMP_ERR_INVALID_REGION, "pmp_set_region rejects region_idx >= 4");
    TEST_ASSERT(pmp_set_region(NULL) == PMP_ERR_NULL_PTR, "pmp_set_region rejects NULL cfg");

    /* Test TOR boundary validation */
    pmp_region_cfg_t tor_cfg = {
        .region_idx = 0U,
        .start_addr = 0x40820000U,
        .length = 0x1000U,
        .read_allow = 1U,
        .write_allow = 0U,
        .execute_allow = 0U,
        .lock = 0U,
        .addr_mode = (uint8_t)PMP_ADDR_MODE_TOR
    };
    TEST_ASSERT(pmp_set_region(&tor_cfg) == PMP_ERR_INVALID_ADDR, "TOR on Region 0 rejects non-zero start_addr");
    tor_cfg.start_addr = 0U;
    TEST_ASSERT(pmp_set_region(&tor_cfg) == PMP_OK, "TOR on Region 0 accepts start_addr = 0");
    pmp_disable_region(0U);

    /* 4. HP_APM Register Macros & Functions */
    TEST_ASSERT((uintptr_t)HP_APM_REGION_START_REG(0) == (HP_APM_BASE_ADDR + 0x04U), "APM Region 0 START is 0x60099004");
    TEST_ASSERT((uintptr_t)HP_APM_REGION_END_REG(0) == (HP_APM_BASE_ADDR + 0x08U), "APM Region 0 END is 0x60099008");
    TEST_ASSERT((uintptr_t)HP_APM_REGION_PMS_ATTR_REG(0) == (HP_APM_BASE_ADDR + 0x0CU), "APM Region 0 PMS is 0x6009900C");
    TEST_ASSERT((uintptr_t)HP_APM_REGION_START_REG(1) == (HP_APM_BASE_ADDR + 0x10U), "APM Region 1 START is 0x60099010");
    TEST_ASSERT((uintptr_t)HP_APM_REGION_START_REG(15) == (HP_APM_BASE_ADDR + 0xB8U), "APM Region 15 START is 0x600990B8");
    TEST_ASSERT((uintptr_t)HP_APM_FUNC_CONTROL_REG == (HP_APM_BASE_ADDR + 0xC4U), "APM FUNC_CONTROL is 0x600990C4");
    TEST_ASSERT((uintptr_t)HP_APM_M_STATUS_REG(0) == (HP_APM_BASE_ADDR + 0xC8U), "APM M0 STATUS is 0x600990C8");
    TEST_ASSERT((uintptr_t)HP_APM_M_STATUS_REG(1) == (HP_APM_BASE_ADDR + 0xD8U), "APM M1 STATUS is 0x600990D8");
    TEST_ASSERT((uintptr_t)HP_APM_M_STATUS_REG(2) == (HP_APM_BASE_ADDR + 0xE8U), "APM M2 STATUS is 0x600990E8");
    TEST_ASSERT((uintptr_t)HP_APM_M_STATUS_REG(3) == (HP_APM_BASE_ADDR + 0xF8U), "APM M3 STATUS is 0x600990F8");
    TEST_ASSERT((uintptr_t)HP_APM_CLK_GATE_REG == (HP_APM_BASE_ADDR + 0x10CU), "APM CLK_GATE is 0x6009910C");

    apm_init();

    /* Region 0 is reserved: disabling or clearing filter must be rejected */
    TEST_ASSERT(apm_disable_region(0U) == APM_ERR_RESERVED_REGION, "apm_disable_region rejects Region 0");
    apm_region_cfg_t apm_r0_cfg = {
        .region_idx = 0U,
        .start_addr = 0x40820000U,
        .end_addr = 0x40880000U,
        .filter_enable = 0U
    };
    TEST_ASSERT(apm_set_region(&apm_r0_cfg) == APM_ERR_RESERVED_REGION, "apm_set_region rejects disabling Region 0");

    /* Dynamic configurations on Region 1 */
    apm_region_cfg_t apm_cfg = {
        .region_idx = 1U,
        .start_addr = 0x40820000U,
        .end_addr = 0x40880000U,
        .read_allow = 1U,
        .write_allow = 1U,
        .execute_allow = 0U,
        .filter_enable = 1U
    };
    TEST_ASSERT(apm_set_region(&apm_cfg) == APM_OK, "apm_set_region succeeds for Region 1");
    apm_region_cfg_t apm_rb;
    TEST_ASSERT(apm_get_region(1U, &apm_rb) == APM_OK, "apm_get_region succeeds for Region 1");
    TEST_ASSERT(apm_rb.start_addr == 0x40820000U && apm_rb.end_addr == 0x40880000U, "APM readback addresses match");
    TEST_ASSERT(apm_rb.read_allow == 1U && apm_rb.write_allow == 1U && apm_rb.execute_allow == 0U, "APM permissions match");
    TEST_ASSERT(apm_rb.filter_enable == 1U, "APM filter enable matches");

    TEST_ASSERT(apm_enable_master(0U, 1) == APM_OK, "apm_enable_master(0, 1) succeeds");
    TEST_ASSERT(apm_enable_master(0U, 0) == APM_OK, "apm_enable_master(0, 0) succeeds");
    TEST_ASSERT(apm_disable_region(1U) == APM_OK, "apm_disable_region(1) succeeds");

    /* 5. Telemetry Query */
    pmp_telemetry_t tel;
    pmp_get_telemetry(&tel);
    TEST_ASSERT(tel.pmp_active_count == 0U, "telemetry reports 0 active PMP regions after disable");
    TEST_ASSERT(tel.apm_active_count == 1U, "telemetry reports 1 active APM region (Region 0 pass-through)");
}

static void test_lp_core_driver(void)
{
    printf("  [TEST] lp_core coprocessor driver (Task 4.1)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)LP_PERI_CLK_EN_REG == (LP_PERI_BASE_ADDR + LP_PERI_CLK_EN_OFFSET), "LP_PERI_CLK_EN_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_PERI_RESET_EN_REG == (LP_PERI_BASE_ADDR + LP_PERI_RESET_EN_OFFSET), "LP_PERI_RESET_EN_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_PERI_CPU_REG == (LP_PERI_BASE_ADDR + LP_PERI_CPU_OFFSET), "LP_PERI_CPU_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_CLKRST_LP_CLK_EN_REG == (LP_CLKRST_BASE_ADDR + LP_CLKRST_LP_CLK_EN_OFFSET), "LP_CLKRST_LP_CLK_EN_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_CLKRST_LPMEM_FORCE_REG == (LP_CLKRST_BASE_ADDR + LP_CLKRST_LPMEM_FORCE_OFFSET), "LP_CLKRST_LPMEM_FORCE_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_AON_LPBUS_REG == (LP_AON_BASE_ADDR + LP_AON_LPBUS_OFFSET), "LP_AON_LPBUS_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_APM_FUNC_CTRL_REG == (LP_APM_BASE_ADDR + LP_APM_FUNC_CTRL_OFFSET), "LP_APM_FUNC_CTRL_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_APM0_FUNC_CTRL_REG == (LP_APM0_BASE_ADDR + LP_APM0_FUNC_CTRL_OFFSET), "LP_APM0_FUNC_CTRL_REG address calculation");
    TEST_ASSERT((uintptr_t)PMU_INT_RAW_REG == (PMU_BASE_ADDR + PMU_INT_RAW_OFFSET), "PMU_INT_RAW_REG address calculation");
    TEST_ASSERT((uintptr_t)PMU_HP_INT_CLR_REG == (PMU_BASE_ADDR + PMU_HP_INT_CLR_OFFSET), "PMU_HP_INT_CLR_REG address calculation");
    TEST_ASSERT((uintptr_t)PMU_LP_CPU_PWR0_REG == (PMU_BASE_ADDR + PMU_LP_CPU_PWR0_OFFSET), "PMU_LP_CPU_PWR0_REG address calculation");
    TEST_ASSERT((uintptr_t)PMU_LP_CPU_PWR1_REG == (PMU_BASE_ADDR + PMU_LP_CPU_PWR1_OFFSET), "PMU_LP_CPU_PWR1_REG address calculation");
    TEST_ASSERT((uintptr_t)PMU_HP_LP_CPU_COMM_REG == (PMU_BASE_ADDR + PMU_HP_LP_CPU_COMM_OFFSET), "PMU_HP_LP_CPU_COMM_REG address calculation");

    /* 2. Embedded Default Firmware Header Validation */
    const lp_firmware_header_t *fw_hdr = lp_core_get_default_firmware();
    TEST_ASSERT(fw_hdr != NULL, "default firmware header must be non-NULL");
    TEST_ASSERT(fw_hdr->magic == LP_FIRMWARE_HEADER_MAGIC, "firmware magic must be 'IRON' (0x49524F4E)");
    TEST_ASSERT(fw_hdr->version == LP_FIRMWARE_VERSION_1_0, "firmware version must be 1.0 (0x00010000)");
    TEST_ASSERT(fw_hdr->entry_point == LP_SRAM_ENTRY_ADDR, "firmware entry must be 0x50000080");
    TEST_ASSERT(fw_hdr->size_bytes > 0U, "firmware size must be non-zero");
    TEST_ASSERT(fw_hdr->size_bytes <= (LP_SRAM_SIZE_BYTES / 2U), "firmware size within LP SRAM limit");
    TEST_ASSERT(fw_hdr->binary != NULL, "firmware binary payload pointer must be non-NULL");

    /* 3. Parameter Validation & Error Handling */
    TEST_ASSERT(lp_core_load_firmware(NULL, 100U) == LP_CORE_ERR_NULL_PTR, "load_firmware rejects NULL binary");
    TEST_ASSERT(lp_core_load_firmware(fw_hdr->binary, 0U) == LP_CORE_ERR_INVALID_SIZE, "load_firmware rejects 0 size");
    TEST_ASSERT(lp_core_load_firmware(fw_hdr->binary, LP_SRAM_SIZE_BYTES) == LP_CORE_ERR_INVALID_SIZE, "load_firmware rejects oversized payload");
    TEST_ASSERT(lp_core_load_header(NULL) == LP_CORE_ERR_NULL_PTR, "load_header rejects NULL header");

    lp_firmware_header_t invalid_hdr = *fw_hdr;
    invalid_hdr.magic = 0xDEADBEEFU;
    TEST_ASSERT(lp_core_load_header(&invalid_hdr) == LP_CORE_ERR_INVALID_MAGIC, "load_header rejects invalid magic");

    invalid_hdr = *fw_hdr;
    invalid_hdr.entry_point = 0x40800000U;
    TEST_ASSERT(lp_core_load_header(&invalid_hdr) == LP_CORE_ERR_INVALID_ENTRY, "load_header rejects non-LP entry point");

    TEST_ASSERT(lp_core_get_telemetry(NULL) == LP_CORE_ERR_NULL_PTR, "get_telemetry rejects NULL pointer");

    /* 4. Subsystem Initialization & Lifecycle */
    TEST_ASSERT(lp_core_init() == LP_CORE_OK, "lp_core_init succeeds");
    TEST_ASSERT(!lp_core_is_running(), "LP core must not be running immediately after init");

    lp_core_telemetry_t telem;
    TEST_ASSERT(lp_core_get_telemetry(&telem) == LP_CORE_OK, "get_telemetry succeeds after init");
    TEST_ASSERT(!telem.is_running, "telemetry reports is_running=false after init");
    TEST_ASSERT(!telem.clock_enabled, "telemetry reports clock_enabled=false after init");
    TEST_ASSERT(telem.in_reset, "telemetry reports in_reset=true after init");
    TEST_ASSERT(!telem.hp_trigger_active, "hp_trigger_active=false after init");
    TEST_ASSERT(!telem.lp_trigger_active, "lp_trigger_active=false after init");

    /* 5. Firmware Deployment into Retained Memory */
    TEST_ASSERT(lp_core_load_header(fw_hdr) == LP_CORE_OK, "load_header succeeds with default image");
    TEST_ASSERT(lp_core_read_magic() == 0U, "magic word cleared before boot");
    TEST_ASSERT(lp_core_read_counter() == 0U, "counter cleared before boot");

    /* 6. Execution Start & Mock Handshake */
    TEST_ASSERT(lp_core_start() == LP_CORE_OK, "lp_core_start succeeds");
    TEST_ASSERT(lp_core_is_running(), "LP core reports running after start");

    /* Verify PMU Trigger & Handshake */
    TEST_ASSERT(lp_core_trigger_lp() == LP_CORE_OK, "trigger_lp succeeds");
    TEST_ASSERT(lp_core_wait_handshake(100U) == LP_CORE_OK, "wait_handshake succeeds with mock response");
    TEST_ASSERT(lp_core_read_magic() == LP_TEST_MAGIC_EXPECTED, "handshake magic matches 0xCAFEBABE");
    TEST_ASSERT(lp_core_read_counter() >= 1U, "counter readback is non-zero");
    TEST_ASSERT(lp_core_get_lp_trigger() == 1U, "LP trigger is asserted");

    lp_core_clear_lp_trigger();
    TEST_ASSERT(lp_core_get_lp_trigger() == 0U, "clear_lp_trigger deasserts trigger flag");

    /* 7. Stop Subsystem */
    TEST_ASSERT(lp_core_stop() == LP_CORE_OK, "lp_core_stop succeeds");
    TEST_ASSERT(!lp_core_is_running(), "LP core reports stopped after stop");

    TEST_ASSERT(lp_core_get_telemetry(&telem) == LP_CORE_OK, "get_telemetry succeeds after stop");
    TEST_ASSERT(!telem.is_running, "telemetry reports is_running=false after stop");
    TEST_ASSERT(!telem.clock_enabled, "telemetry reports clock_enabled=false after stop");
    TEST_ASSERT(telem.in_reset, "telemetry reports in_reset=true after stop");
    TEST_ASSERT(telem.magic_readback == LP_TEST_MAGIC_EXPECTED, "telemetry retains magic word");
}

static void test_power_mailbox_subsystem(void)
{
    printf("  [TEST] LP SRAM shared mailbox & power state machine (Task 4.2)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)LP_AON_STORE_REG(0U) == (LP_AON_BASE_ADDR + LP_AON_STORE0_OFFSET), "LP_AON_STORE_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)LP_AON_STORE_REG(9U) == (LP_AON_BASE_ADDR + LP_AON_STORE0_OFFSET + 36U), "LP_AON_STORE_REG(9) address calculation");
    TEST_ASSERT((uintptr_t)PMU_SLP_HP_PERI_CONF_REG == (PMU_BASE_ADDR + PMU_SLP_HP_PERI_CONF_OFFSET), "PMU_SLP_HP_PERI_CONF_REG address calculation");

    /* 2. Mailbox Structure Geometry & Alignment Validation */
    TEST_ASSERT(sizeof(lp_shared_mailbox_t) == 28U, "lp_shared_mailbox_t size must be exactly 28 bytes (7 * 4 bytes)");
    TEST_ASSERT((LP_SHARED_MEM_BASE & 0x3U) == 0U, "LP_SHARED_MEM_BASE must be 4-byte aligned");
    TEST_ASSERT(LP_MAILBOX_MAGIC == 0x49524F4EU, "LP_MAILBOX_MAGIC must be 'IRON' (0x49524F4E)");

    /* 3. Parameter Validation & Error Handling */
    TEST_ASSERT(power_send_cmd(LP_CMD_NONE, 100U) == POWER_ERR_INVALID_CMD, "power_send_cmd rejects LP_CMD_NONE");
    TEST_ASSERT(power_sample_telemetry(NULL, 100U) == POWER_ERR_NULL_PTR, "power_sample_telemetry rejects NULL out pointer");
    TEST_ASSERT(power_get_telemetry(NULL) == POWER_ERR_NULL_PTR, "power_get_telemetry rejects NULL telem pointer");
    TEST_ASSERT(power_set_mode((power_mode_t)99) == POWER_ERR_INVALID_ARG, "power_set_mode rejects invalid power mode");
    TEST_ASSERT(power_read_retained_store(POWER_AON_STORE_COUNT) == 0U, "power_read_retained_store out-of-range returns 0");
    TEST_ASSERT(power_write_retained_store(POWER_AON_STORE_COUNT, 0x1234U) == POWER_ERR_INVALID_ARG, "power_write_retained_store out-of-range rejected");

    /* 4. Subsystem Initialization & Mailbox Setup */
    TEST_ASSERT(power_init() == POWER_OK, "power_init succeeds");
    TEST_ASSERT(power_get_mode() == PM_STATE_ACTIVE, "Initial power mode is PM_STATE_ACTIVE");

    volatile lp_shared_mailbox_t *mb = power_get_mailbox();
    TEST_ASSERT(mb != NULL, "power_get_mailbox returns non-NULL mailbox pointer");
    TEST_ASSERT(mb->magic == LP_MAILBOX_MAGIC, "Mailbox magic initialized to 0x49524F4E");
    TEST_ASSERT(mb->hp_to_lp_cmd == LP_CMD_NONE, "Initial hp_to_lp_cmd is LP_CMD_NONE");
    TEST_ASSERT(mb->lp_to_hp_ack == LP_CMD_NONE, "Initial lp_to_hp_ack is LP_CMD_NONE");
    TEST_ASSERT(mb->periodic_wake_count == 0U, "Initial periodic_wake_count is 0");

    /* 5. Retained LP_AON Scratchpad Store Access */
    TEST_ASSERT(power_write_retained_store(0U, 0xDEADBEEFU) == POWER_OK, "write STORE0 succeeds");
    TEST_ASSERT(power_read_retained_store(0U) == 0xDEADBEEFU, "read STORE0 matches written seed 0xDEADBEEF");
    TEST_ASSERT(power_write_retained_store(9U, 0xCAFE1234U) == POWER_OK, "write STORE9 succeeds");
    TEST_ASSERT(power_read_retained_store(9U) == 0xCAFE1234U, "read STORE9 matches written seed 0xCAFE1234");

    /* 6. Mailbox Command Dispatch & Telemetry Protocol */
    lp_core_init();
    lp_core_start();
    TEST_ASSERT(lp_core_is_running(), "LP core is running for mailbox handshake tests");

    TEST_ASSERT(power_send_cmd(LP_CMD_SAMPLE_TELEMETRY, POWER_HANDSHAKE_TIMEOUT_CYCLES) == POWER_OK, "power_send_cmd SAMPLE_TELEMETRY succeeds");
    TEST_ASSERT(mb->lp_to_hp_ack == LP_CMD_SAMPLE_TELEMETRY, "Mailbox ACK matches SAMPLE_TELEMETRY opcode");
    TEST_ASSERT(mb->periodic_wake_count >= 1U, "Mailbox periodic_wake_count incremented");

    uint32_t telem_word = 0U;
    TEST_ASSERT(power_sample_telemetry(&telem_word, POWER_HANDSHAKE_TIMEOUT_CYCLES) == POWER_OK, "power_sample_telemetry succeeds");
    TEST_ASSERT((telem_word & LP_TELEMETRY_HEADER_MASK) == LP_TELEMETRY_HEADER_MASK, "Sampled telemetry contains 0x4952 header");

    TEST_ASSERT(power_send_cmd(LP_CMD_RESET_STATS, POWER_HANDSHAKE_TIMEOUT_CYCLES) == POWER_OK, "power_send_cmd RESET_STATS succeeds");
    TEST_ASSERT(mb->lp_to_hp_ack == LP_CMD_RESET_STATS, "Mailbox ACK matches RESET_STATS opcode");

    /* 7. Power State Mode Transitions */
    TEST_ASSERT(power_set_mode(PM_STATE_LIGHT_SLEEP) == POWER_OK, "Transition to PM_STATE_LIGHT_SLEEP succeeds");
    TEST_ASSERT(power_get_mode() == PM_STATE_LIGHT_SLEEP, "Current mode is PM_STATE_LIGHT_SLEEP");
    TEST_ASSERT(mb->lp_to_hp_ack == LP_CMD_ENTER_SLEEP, "Entering light sleep dispatches ENTER_SLEEP cmd to LP core");

    TEST_ASSERT(power_set_mode(PM_STATE_DEEP_SLEEP) == POWER_OK, "Transition to PM_STATE_DEEP_SLEEP succeeds");
    TEST_ASSERT(power_get_mode() == PM_STATE_DEEP_SLEEP, "Current mode is PM_STATE_DEEP_SLEEP");

    TEST_ASSERT(power_set_mode(PM_STATE_ACTIVE) == POWER_OK, "Transition back to PM_STATE_ACTIVE succeeds");
    TEST_ASSERT(power_get_mode() == PM_STATE_ACTIVE, "Current mode is restored to PM_STATE_ACTIVE");

    /* 8. Full Power Telemetry Snapshot Query */
    power_telemetry_t pwr_telem;
    TEST_ASSERT(power_get_telemetry(&pwr_telem) == POWER_OK, "power_get_telemetry succeeds");
    TEST_ASSERT(pwr_telem.current_mode == PM_STATE_ACTIVE, "Telemetry reports current_mode == PM_STATE_ACTIVE");
    TEST_ASSERT(pwr_telem.mailbox_magic == LP_MAILBOX_MAGIC, "Telemetry reports mailbox_magic == 0x49524F4E");
    TEST_ASSERT(pwr_telem.aon_store0_val == 0xDEADBEEFU, "Telemetry reports aon_store0_val == 0xDEADBEEF");
    TEST_ASSERT(pwr_telem.lp_running, "Telemetry reports lp_running == true");

    /* Clean up LP core */
    lp_core_stop();
    TEST_ASSERT(!lp_core_is_running(), "LP core stopped cleanly after power tests");
}

static void test_gpio_subsystem(void)
{
    printf("  [TEST] GPIO matrix & IO_MUX pin routing (Task 4.3)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)IO_MUX_GPIO_REG(0U) == (IO_MUX_BASE_ADDR + IO_MUX_GPIO0_OFFSET), "IO_MUX_GPIO_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)IO_MUX_GPIO_REG(15U) == (IO_MUX_BASE_ADDR + IO_MUX_GPIO0_OFFSET + (15U * 4U)), "IO_MUX_GPIO_REG(15) address calculation");
    TEST_ASSERT((uintptr_t)IO_MUX_GPIO_REG(30U) == (IO_MUX_BASE_ADDR + IO_MUX_GPIO0_OFFSET + (30U * 4U)), "IO_MUX_GPIO_REG(30) address calculation");
    TEST_ASSERT((uintptr_t)GPIO_PIN_CONF_REG(15U) == (GPIO_BASE_ADDR + GPIO_PIN0_CONF_OFFSET + (15U * 4U)), "GPIO_PIN_CONF_REG(15) address calculation");
    TEST_ASSERT((uintptr_t)GPIO_FUNC_OUT_SEL_REG(15U) == (GPIO_BASE_ADDR + GPIO_FUNC_OUT_SEL_OFFSET + (15U * 4U)), "GPIO_FUNC_OUT_SEL_REG(15) address calculation");
    TEST_ASSERT((uintptr_t)GPIO_OUT_REG == (GPIO_BASE_ADDR + GPIO_OUT_OFFSET), "GPIO_OUT_REG address calculation");
    TEST_ASSERT((uintptr_t)GPIO_OUT_W1TS_REG == (GPIO_BASE_ADDR + GPIO_OUT_W1TS_OFFSET), "GPIO_OUT_W1TS_REG address calculation");
    TEST_ASSERT((uintptr_t)GPIO_OUT_W1TC_REG == (GPIO_BASE_ADDR + GPIO_OUT_W1TC_OFFSET), "GPIO_OUT_W1TC_REG address calculation");
    TEST_ASSERT((uintptr_t)GPIO_ENABLE_REG == (GPIO_BASE_ADDR + GPIO_ENABLE_OFFSET), "GPIO_ENABLE_REG address calculation");
    TEST_ASSERT((uintptr_t)GPIO_ENABLE_W1TS_REG == (GPIO_BASE_ADDR + GPIO_ENABLE_W1TS_OFFSET), "GPIO_ENABLE_W1TS_REG address calculation");
    TEST_ASSERT((uintptr_t)GPIO_ENABLE_W1TC_REG == (GPIO_BASE_ADDR + GPIO_ENABLE_W1TC_OFFSET), "GPIO_ENABLE_W1TC_REG address calculation");
    TEST_ASSERT((uintptr_t)GPIO_IN_REG == (GPIO_BASE_ADDR + GPIO_IN_OFFSET), "GPIO_IN_REG address calculation");
    TEST_ASSERT((uintptr_t)GPIO_STATUS_REG == (GPIO_BASE_ADDR + GPIO_STATUS_OFFSET), "GPIO_STATUS_REG address calculation");
    TEST_ASSERT((uintptr_t)PCR_IOMUX_CONF_REG == (PCR_BASE_ADDR + PCR_IOMUX_CONF_OFFSET), "PCR_IOMUX_CONF_REG address calculation");

    /* 2. Parameter Validation & Boundary Checks */
    TEST_ASSERT(gpio_set_direction(31U, GPIO_DIR_OUTPUT) == GPIO_ERR_INVALID_PIN, "gpio_set_direction rejects pin 31");
    TEST_ASSERT(gpio_set_direction(99U, GPIO_DIR_OUTPUT) == GPIO_ERR_INVALID_PIN, "gpio_set_direction rejects pin 99");
    TEST_ASSERT(gpio_set_pull(31U, GPIO_PULL_UP) == GPIO_ERR_INVALID_PIN, "gpio_set_pull rejects pin 31");
    TEST_ASSERT(gpio_set_drive_strength(31U, GPIO_DRIVE_0) == GPIO_ERR_INVALID_PIN, "gpio_set_drive_strength rejects pin 31");
    TEST_ASSERT(gpio_set_drive_strength(15U, (gpio_drive_strength_t)99) == GPIO_ERR_INVALID_ARG, "gpio_set_drive_strength rejects invalid drive level");
    TEST_ASSERT(gpio_set_function(31U, 1U) == GPIO_ERR_INVALID_PIN, "gpio_set_function rejects pin 31");
    TEST_ASSERT(gpio_set_function(15U, 8U) == GPIO_ERR_INVALID_ARG, "gpio_set_function rejects function > 7");
    TEST_ASSERT(gpio_set_drive_mode(31U, GPIO_MODE_OPEN_DRAIN) == GPIO_ERR_INVALID_PIN, "gpio_set_drive_mode rejects pin 31");
    TEST_ASSERT(gpio_set_level(31U, 1U) == GPIO_ERR_INVALID_PIN, "gpio_set_level rejects pin 31");
    TEST_ASSERT(gpio_get_level(31U) == GPIO_ERR_INVALID_PIN, "gpio_get_level rejects pin 31");
    TEST_ASSERT(gpio_get_output_level(31U) == GPIO_ERR_INVALID_PIN, "gpio_get_output_level rejects pin 31");
    TEST_ASSERT(gpio_toggle_level(31U) == GPIO_ERR_INVALID_PIN, "gpio_toggle_level rejects pin 31");
    TEST_ASSERT(gpio_set_intr_type(31U, GPIO_INTR_RISING_EDGE) == GPIO_ERR_INVALID_PIN, "gpio_set_intr_type rejects pin 31");
    TEST_ASSERT(gpio_intr_enable(31U) == GPIO_ERR_INVALID_PIN, "gpio_intr_enable rejects pin 31");
    TEST_ASSERT(gpio_intr_disable(31U) == GPIO_ERR_INVALID_PIN, "gpio_intr_disable rejects pin 31");
    TEST_ASSERT(gpio_intr_clear(31U) == GPIO_ERR_INVALID_PIN, "gpio_intr_clear rejects pin 31");
    TEST_ASSERT(gpio_get_telemetry(NULL) == GPIO_ERR_INVALID_ARG, "gpio_get_telemetry rejects NULL pointer");

    /* 3. Subsystem Initialization */
    TEST_ASSERT(gpio_init() == GPIO_OK, "gpio_init succeeds");

    /* 4. Direction & Output Control */
    TEST_ASSERT(gpio_set_direction(15U, GPIO_DIR_OUTPUT) == GPIO_OK, "set GPIO 15 as OUTPUT succeeds");
    TEST_ASSERT(gpio_set_function(15U, IO_MUX_MCU_SEL_FUNC1_GPIO) == GPIO_OK, "set GPIO 15 function to GPIO succeeds");
    TEST_ASSERT(gpio_set_level(15U, 1U) == GPIO_OK, "gpio_set_level 15 HIGH succeeds");
    TEST_ASSERT(gpio_get_output_level(15U) == 1, "gpio_get_output_level 15 reports 1");

    TEST_ASSERT(gpio_toggle_level(15U) == GPIO_OK, "gpio_toggle_level 15 succeeds");
    TEST_ASSERT(gpio_get_output_level(15U) == 0, "gpio_get_output_level 15 reports 0 after toggle");

    TEST_ASSERT(gpio_set_level(15U, 1U) == GPIO_OK, "gpio_set_level 15 HIGH succeeds");
    TEST_ASSERT(gpio_set_level(15U, 0U) == GPIO_OK, "gpio_set_level 15 LOW succeeds");
    TEST_ASSERT(gpio_get_output_level(15U) == 0, "gpio_get_output_level 15 reports 0");

    /* 5. Input Direction & Internal Pull Resistors */
    TEST_ASSERT(gpio_set_direction(16U, GPIO_DIR_INPUT) == GPIO_OK, "set GPIO 16 as INPUT succeeds");
    TEST_ASSERT(gpio_set_pull(16U, GPIO_PULL_UP) == GPIO_OK, "gpio_set_pull 16 UP succeeds");
    TEST_ASSERT(gpio_get_level(16U) == 1, "gpio_get_level 16 reads 1 with pull-up");

    TEST_ASSERT(gpio_set_pull(16U, GPIO_PULL_DOWN) == GPIO_OK, "gpio_set_pull 16 DOWN succeeds");
    TEST_ASSERT(gpio_get_level(16U) == 0, "gpio_get_level 16 reads 0 with pull-down");

    TEST_ASSERT(gpio_set_pull(16U, GPIO_PULL_NONE) == GPIO_OK, "gpio_set_pull 16 NONE succeeds");

    /* 6. Drive Strength & Open-Drain Configuration */
    TEST_ASSERT(gpio_set_drive_strength(15U, GPIO_DRIVE_3) == GPIO_OK, "gpio_set_drive_strength 15 DRIVE_3 succeeds");
    TEST_ASSERT(gpio_set_drive_mode(15U, GPIO_MODE_OPEN_DRAIN) == GPIO_OK, "gpio_set_drive_mode 15 OPEN_DRAIN succeeds");
    TEST_ASSERT(gpio_set_drive_mode(15U, GPIO_MODE_PUSH_PULL) == GPIO_OK, "gpio_set_drive_mode 15 PUSH_PULL succeeds");

    /* 7. Interrupt Configuration */
    TEST_ASSERT(gpio_set_intr_type(16U, GPIO_INTR_RISING_EDGE) == GPIO_OK, "gpio_set_intr_type 16 RISING_EDGE succeeds");
    TEST_ASSERT(gpio_intr_enable(16U) == GPIO_OK, "gpio_intr_enable 16 succeeds");
    TEST_ASSERT(gpio_intr_disable(16U) == GPIO_OK, "gpio_intr_disable 16 succeeds");
    TEST_ASSERT(gpio_intr_clear(16U) == GPIO_OK, "gpio_intr_clear 16 succeeds");

    /* 8. Telemetry Snapshot Query */
    gpio_telemetry_t telem;
    TEST_ASSERT(gpio_get_telemetry(&telem) == GPIO_OK, "gpio_get_telemetry succeeds");
    TEST_ASSERT((telem.enable_mask & (1U << 15U)) != 0U, "telemetry reports GPIO 15 output enabled");
}

static void test_gdma_subsystem(void)
{
    printf("  [TEST] GDMA multi-channel engine & descriptor rings (Task 4.4)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)GDMA_IN_CONF0_REG(0U) == 0x60080070U, "GDMA_IN_CONF0_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_IN_LINK_REG(0U) == 0x60080080U, "GDMA_IN_LINK_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_IN_DSCR_REG(0U) == 0x60080090U, "GDMA_IN_DSCR_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_OUT_CONF0_REG(0U) == 0x600800D0U, "GDMA_OUT_CONF0_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_OUT_LINK_REG(0U) == 0x600800E0U, "GDMA_OUT_LINK_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_OUT_DSCR_REG(0U) == 0x600800F0U, "GDMA_OUT_DSCR_REG(0) address calculation");

    TEST_ASSERT((uintptr_t)GDMA_IN_CONF0_REG(1U) == 0x60080130U, "GDMA_IN_CONF0_REG(1) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_OUT_CONF0_REG(1U) == 0x60080190U, "GDMA_OUT_CONF0_REG(1) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_IN_CONF0_REG(2U) == 0x600801F0U, "GDMA_IN_CONF0_REG(2) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_OUT_CONF0_REG(2U) == 0x60080250U, "GDMA_OUT_CONF0_REG(2) address calculation");

    TEST_ASSERT((uintptr_t)GDMA_IN_INT_RAW_REG(0U) == 0x60080000U, "GDMA_IN_INT_RAW_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_IN_INT_RAW_REG(1U) == 0x60080010U, "GDMA_IN_INT_RAW_REG(1) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_OUT_INT_RAW_REG(0U) == 0x60080030U, "GDMA_OUT_INT_RAW_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_OUT_INT_RAW_REG(1U) == 0x60080040U, "GDMA_OUT_INT_RAW_REG(1) address calculation");
    TEST_ASSERT((uintptr_t)GDMA_DATE_REG == 0x60080068U, "GDMA_DATE_REG address calculation");
    TEST_ASSERT((uintptr_t)PCR_GDMA_CONF_REG == 0x600960BCU, "PCR_GDMA_CONF_REG address calculation");

    /* 2. Descriptor Structure Geometry & Packing */
#if defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 4)
    TEST_ASSERT(sizeof(dma_descriptor_t) == 12U, "sizeof(dma_descriptor_t) must be exactly 12 bytes on 32-bit target");
#else
    TEST_ASSERT(sizeof(dma_descriptor_t) == 16U, "sizeof(dma_descriptor_t) is 16 bytes on 64-bit host platform (32-bit pointer in target hardware)");
#endif
    TEST_ASSERT(offsetof(dma_descriptor_t, buffer_addr) == 4U, "buffer_addr offset must be 4 bytes");
    TEST_ASSERT(offsetof(dma_descriptor_t, next_descriptor) == 8U, "next_descriptor offset must be 8 bytes");

    /* 3. Descriptor Formatting & Helper Functions */
    static dma_descriptor_t desc0 __attribute__((aligned(4)));
    static dma_descriptor_t desc1 __attribute__((aligned(4)));
    static uint8_t buf0[64] __attribute__((aligned(4)));
    static uint8_t buf1[64] __attribute__((aligned(4)));

    TEST_ASSERT(gdma_desc_init(&desc0, buf0, 64U, 0U, DMA_OWNER_DMA) == GDMA_OK, "gdma_desc_init desc0 succeeds");
    TEST_ASSERT(desc0.size == 64U, "desc0 size is 64");
    TEST_ASSERT(desc0.length == 0U, "desc0 length is 0");
    TEST_ASSERT(desc0.owner == DMA_OWNER_DMA, "desc0 owner is DMA");
    TEST_ASSERT(desc0.buffer_addr == (uint32_t)(uintptr_t)buf0, "desc0 buffer_addr matches buf0");
    TEST_ASSERT(desc0.next_descriptor == NULL, "desc0 next_descriptor initially NULL");

    TEST_ASSERT(gdma_desc_init(&desc1, buf1, 64U, 32U, DMA_OWNER_CPU) == GDMA_OK, "gdma_desc_init desc1 succeeds");
    TEST_ASSERT(desc1.size == 64U, "desc1 size is 64");
    TEST_ASSERT(desc1.length == 32U, "desc1 length is 32");
    TEST_ASSERT(desc1.owner == DMA_OWNER_CPU, "desc1 owner is CPU");
    TEST_ASSERT(desc1.buffer_addr == (uint32_t)(uintptr_t)buf1, "desc1 buffer_addr matches buf1");

    TEST_ASSERT(gdma_desc_link_circular(&desc0, &desc1) == GDMA_OK, "gdma_desc_link_circular succeeds");
    TEST_ASSERT(desc0.next_descriptor == &desc1, "desc0 points to desc1");
    TEST_ASSERT(desc1.next_descriptor == &desc0, "desc1 points back to desc0 (circular ring)");

    /* 4. Parameter & Boundary Validation */
    TEST_ASSERT(gdma_desc_init(NULL, buf0, 64U, 0U, DMA_OWNER_DMA) == GDMA_ERR_INVALID_ARG, "desc_init rejects NULL desc");
    TEST_ASSERT(gdma_desc_init(&desc0, buf0, 5000U, 0U, DMA_OWNER_DMA) == GDMA_ERR_INVALID_ARG, "desc_init rejects size > 4095");
    TEST_ASSERT(gdma_desc_init(&desc0, buf0, 64U, 5000U, DMA_OWNER_DMA) == GDMA_ERR_INVALID_ARG, "desc_init rejects length > 4095");
    TEST_ASSERT(gdma_desc_link_circular(NULL, &desc1) == GDMA_ERR_INVALID_ARG, "link_circular rejects NULL desc0");
    TEST_ASSERT(gdma_desc_link_circular(&desc0, NULL) == GDMA_ERR_INVALID_ARG, "link_circular rejects NULL desc1");

    /* 5. Driver Lifecycle & Channel Controls */
    TEST_ASSERT(gdma_init() == GDMA_OK, "gdma_init succeeds");
    TEST_ASSERT(gdma_get_date_version() == GDMA_HARDWARE_DATE_EXPECTED, "gdma_get_date_version matches expected");

    TEST_ASSERT(gdma_channel_init(GDMA_CHANNEL_0) == GDMA_OK, "gdma_channel_init(0) succeeds");
    TEST_ASSERT(gdma_channel_init(GDMA_CHANNEL_1) == GDMA_OK, "gdma_channel_init(1) succeeds");
    TEST_ASSERT(gdma_channel_init(GDMA_CHANNEL_2) == GDMA_OK, "gdma_channel_init(2) succeeds");
    TEST_ASSERT(gdma_channel_init(3U) == GDMA_ERR_OUT_OF_RANGE, "gdma_channel_init(3) rejected as out of range");

    TEST_ASSERT(gdma_inlink_set(GDMA_CHANNEL_0, &desc0) == GDMA_OK, "gdma_inlink_set(0, &desc0) succeeds");
    TEST_ASSERT(gdma_inlink_set(GDMA_CHANNEL_0, NULL) == GDMA_ERR_INVALID_ARG, "gdma_inlink_set rejects NULL desc");
    TEST_ASSERT(gdma_inlink_set(3U, &desc0) == GDMA_ERR_OUT_OF_RANGE, "gdma_inlink_set rejects channel 3");

    TEST_ASSERT(gdma_inlink_start(GDMA_CHANNEL_0) == GDMA_OK, "gdma_inlink_start(0) succeeds");
    TEST_ASSERT(gdma_inlink_stop(GDMA_CHANNEL_0) == GDMA_OK, "gdma_inlink_stop(0) succeeds");
    TEST_ASSERT(gdma_inlink_restart(GDMA_CHANNEL_0) == GDMA_OK, "gdma_inlink_restart(0) succeeds");

    TEST_ASSERT(gdma_outlink_set(GDMA_CHANNEL_0, &desc1) == GDMA_OK, "gdma_outlink_set(0, &desc1) succeeds");
    TEST_ASSERT(gdma_outlink_set(GDMA_CHANNEL_0, NULL) == GDMA_ERR_INVALID_ARG, "gdma_outlink_set rejects NULL desc");
    TEST_ASSERT(gdma_outlink_set(3U, &desc1) == GDMA_ERR_OUT_OF_RANGE, "gdma_outlink_set rejects channel 3");

    TEST_ASSERT(gdma_outlink_start(GDMA_CHANNEL_0) == GDMA_OK, "gdma_outlink_start(0) succeeds");
    TEST_ASSERT(gdma_outlink_stop(GDMA_CHANNEL_0) == GDMA_OK, "gdma_outlink_stop(0) succeeds");
    TEST_ASSERT(gdma_outlink_restart(GDMA_CHANNEL_0) == GDMA_OK, "gdma_outlink_restart(0) succeeds");

    TEST_ASSERT(gdma_channel_reset(GDMA_CHANNEL_0) == GDMA_OK, "gdma_channel_reset(0) succeeds");
    TEST_ASSERT(gdma_channel_reset(3U) == GDMA_ERR_OUT_OF_RANGE, "gdma_channel_reset(3) rejected as out of range");

    /* 6. Subsystem Telemetry Snapshot */
    gdma_telemetry_t telem;
    TEST_ASSERT(gdma_get_telemetry(&telem) == GDMA_OK, "gdma_get_telemetry succeeds");
    TEST_ASSERT(telem.date_version == GDMA_HARDWARE_DATE_EXPECTED, "telemetry reports expected date version");
    TEST_ASSERT(gdma_get_telemetry(NULL) == GDMA_ERR_INVALID_ARG, "gdma_get_telemetry rejects NULL");

    gdma_channel_telemetry_t ch_telem;
    TEST_ASSERT(gdma_get_channel_telemetry(GDMA_CHANNEL_0, &ch_telem) == GDMA_OK, "gdma_get_channel_telemetry(0) succeeds");
    TEST_ASSERT(gdma_get_channel_telemetry(3U, &ch_telem) == GDMA_ERR_OUT_OF_RANGE, "gdma_get_channel_telemetry(3) rejected as out of range");
    TEST_ASSERT(gdma_get_channel_telemetry(GDMA_CHANNEL_0, NULL) == GDMA_ERR_INVALID_ARG, "gdma_get_channel_telemetry rejects NULL");
}

static void test_modem_subsystem(void)
{
    printf("  [TEST] Modem clock & power control driver (Task 5.1)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)PCR_MODEM_APB_CONF_REG == 0x60096108U, "PCR_MODEM_APB_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_SYSCON_CLK_CONF_REG == 0x600A9804U, "MODEM_SYSCON_CLK_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_SYSCON_CLK_CONF_FORCE_ON_REG == 0x600A9808U, "MODEM_SYSCON_CLK_CONF_FORCE_ON_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_SYSCON_CLK_PWR_ST_REG == 0x600A980CU, "MODEM_SYSCON_CLK_PWR_ST_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_SYSCON_MODEM_RST_CONF_REG == 0x600A9810U, "MODEM_SYSCON_MODEM_RST_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_SYSCON_CLK_CONF1_REG == 0x600A9814U, "MODEM_SYSCON_CLK_CONF1_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_SYSCON_MEM_CONF_REG == 0x600A9818U, "MODEM_SYSCON_MEM_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_SYSCON_DATE_REG == 0x600A9824U, "MODEM_SYSCON_DATE_REG address calculation");

    TEST_ASSERT((uintptr_t)MODEM_LPCON_COEX_LP_CLK_CONF_REG == 0x600AF008U, "MODEM_LPCON_COEX_LP_CLK_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_LPCON_CLK_CONF_REG == 0x600AF018U, "MODEM_LPCON_CLK_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_LPCON_DATE_REG == 0x600AF02CU, "MODEM_LPCON_DATE_REG address calculation");

    TEST_ASSERT((uintptr_t)IEEE802154_COMMAND_REG == 0x600A3000U, "IEEE802154_COMMAND_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_CTRL_CFG_REG == 0x600A3004U, "IEEE802154_CTRL_CFG_REG address calculation");

    /* 2. Concrete Data Structure Geometry */
    TEST_ASSERT(sizeof(modem_clock_state_t) == 4U, "sizeof(modem_clock_state_t) must be 4 bytes");

    /* 3. Driver Lifecycle Initialization */
    TEST_ASSERT(modem_init() == MODEM_OK, "modem_init succeeds");
    TEST_ASSERT(modem_get_syscon_date() == MODEM_SYSCON_DATE_EXPECTED, "modem_get_syscon_date matches expected");
    TEST_ASSERT(modem_get_lpcon_date() == MODEM_LPCON_DATE_EXPECTED, "modem_get_lpcon_date matches expected");

    /* 4. Initial State Assertions */
    modem_clock_state_t st;
    TEST_ASSERT(modem_get_clock_state(NULL) == MODEM_ERR_INVALID_ARG, "modem_get_clock_state rejects NULL pointer");
    TEST_ASSERT(modem_get_clock_state(&st) == MODEM_OK, "modem_get_clock_state succeeds");
    TEST_ASSERT(st.wifi_clk_enabled == 0U, "Wi-Fi clock initially disabled");
    TEST_ASSERT(st.ble_clk_enabled == 1U, "BLE clock enabled after modem_init");
    TEST_ASSERT(st.ieee802154_clk_enabled == 0U, "IEEE 802.15.4 clock initially disabled");
    TEST_ASSERT(st.coexistence_enabled == 1U, "Coexistence enabled after modem_init");

    TEST_ASSERT(!modem_is_wifi_enabled(), "modem_is_wifi_enabled reports false initially");
    TEST_ASSERT(modem_is_ble_enabled(), "modem_is_ble_enabled reports true initially");
    TEST_ASSERT(!modem_is_ieee802154_enabled(), "modem_is_ieee802154_enabled reports false initially");
    TEST_ASSERT(modem_is_coex_enabled(), "modem_is_coex_enabled reports true initially");

    /* 5. Wi-Fi Clock Control Transitions */
    TEST_ASSERT(modem_enable_wifi_clocks() == MODEM_OK, "modem_enable_wifi_clocks succeeds");
    TEST_ASSERT(modem_is_wifi_enabled(), "modem_is_wifi_enabled reports true");
    TEST_ASSERT(modem_disable_wifi_clocks() == MODEM_OK, "modem_disable_wifi_clocks succeeds");
    TEST_ASSERT(!modem_is_wifi_enabled(), "modem_is_wifi_enabled reports false");

    /* 6. Bluetooth Clock Control Transitions */
    TEST_ASSERT(modem_enable_ble_clocks() == MODEM_OK, "modem_enable_ble_clocks succeeds");
    TEST_ASSERT(modem_is_ble_enabled(), "modem_is_ble_enabled reports true");
    TEST_ASSERT(modem_disable_ble_clocks() == MODEM_OK, "modem_disable_ble_clocks succeeds");
    TEST_ASSERT(!modem_is_ble_enabled(), "modem_is_ble_enabled reports false");

    /* 7. IEEE 802.15.4 Clock Control Transitions */
    TEST_ASSERT(modem_enable_ieee802154_clocks() == MODEM_OK, "modem_enable_ieee802154_clocks succeeds");
    TEST_ASSERT(modem_is_ieee802154_enabled(), "modem_is_ieee802154_enabled reports true");
    TEST_ASSERT(modem_disable_ieee802154_clocks() == MODEM_OK, "modem_disable_ieee802154_clocks succeeds");
    TEST_ASSERT(!modem_is_ieee802154_enabled(), "modem_is_ieee802154_enabled reports false");

    /* 8. Coexistence Clock Transitions */
    TEST_ASSERT(modem_disable_coexistence() == MODEM_OK, "modem_disable_coexistence succeeds");
    TEST_ASSERT(!modem_is_coex_enabled(), "modem_is_coex_enabled reports false");
    TEST_ASSERT(modem_enable_coexistence() == MODEM_OK, "modem_enable_coexistence succeeds");
    TEST_ASSERT(modem_is_coex_enabled(), "modem_is_coex_enabled reports true");

    /* 9. Orchestrated Wireless Subsystems Activation (TEST 29 Stimulus) */
    TEST_ASSERT(modem_enable_all_clocks() == MODEM_OK, "modem_enable_all_clocks succeeds");
    TEST_ASSERT(modem_get_clock_state(&st) == MODEM_OK, "modem_get_clock_state succeeds after all enabled");
    TEST_ASSERT(st.wifi_clk_enabled == 1U, "Wi-Fi clock confirmed enabled");
    TEST_ASSERT(st.ble_clk_enabled == 1U, "BLE clock confirmed enabled");
    TEST_ASSERT(st.ieee802154_clk_enabled == 1U, "IEEE 802.15.4 clock confirmed enabled");
    TEST_ASSERT(st.coexistence_enabled == 1U, "Coexistence confirmed enabled");
}

static void test_ble_gatt_subsystem(void)
{
    printf("  [TEST] Bluetooth 5 (LE) Controller Driver & Minimal GATT Server (Task 5.2)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)EFUSE_MAC_SYS_0_REG == 0x600B0844U, "EFUSE_MAC_SYS_0_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_MAC_SYS_1_REG == 0x600B0848U, "EFUSE_MAC_SYS_1_REG address calculation");

    /* 2. Concrete Data Structure Geometry & Packet Sizing */
    TEST_ASSERT(sizeof(ble_adv_packet_t) == 21U, "sizeof(ble_adv_packet_t) must be 21 bytes packed");
    TEST_ASSERT(sizeof(ble_telemetry_t) >= 28U, "sizeof(ble_telemetry_t) geometry check");
    TEST_ASSERT(sizeof(gatt_attribute_t) >= 12U, "sizeof(gatt_attribute_t) geometry check");

    /* 3. Subsystem Lifecycle & BD_ADDR Retrieval */
    TEST_ASSERT(ble_init() == BLE_OK, "ble_init succeeds");
    uint8_t mac[BLE_BD_ADDR_LEN] = {0};
    TEST_ASSERT(ble_get_bd_addr(NULL) == BLE_ERR_INVALID_ARG, "ble_get_bd_addr rejects NULL");
    TEST_ASSERT(ble_get_bd_addr(mac) == BLE_OK, "ble_get_bd_addr succeeds");
    TEST_ASSERT(mac[0] == 0x40U && mac[1] == 0x4CU && mac[2] == 0xCAU &&
                mac[3] == 0x45U && mac[4] == 0x1EU && mac[5] == 0x14U,
                "Authentic BD_ADDR matches hardware 40:4C:CA:45:1E:14");

    ble_telemetry_t telem;
    TEST_ASSERT(ble_get_telemetry(NULL) == BLE_ERR_INVALID_ARG, "ble_get_telemetry rejects NULL");
    TEST_ASSERT(ble_get_telemetry(&telem) == BLE_OK, "ble_get_telemetry succeeds");
    TEST_ASSERT(telem.state == BLE_STATE_STANDBY, "Initial state is BLE_STATE_STANDBY");
    TEST_ASSERT(ble_gap_get_state() == BLE_STATE_STANDBY, "ble_gap_get_state matches BLE_STATE_STANDBY");

    /* 4. HCI Reset Command Loopback (TEST 30 Sequence) */
    /* Command: [0x01, 0x03, 0x0C, 0x00] */
    uint8_t hci_reset_cmd[] = { HCI_PKT_TYPE_CMD, 0x03U, 0x0CU, 0x00U };
    uint8_t evt_resp[32];

    TEST_ASSERT(ble_hci_send_cmd(NULL, sizeof(hci_reset_cmd)) == BLE_ERR_INVALID_ARG, "ble_hci_send_cmd rejects NULL");
    TEST_ASSERT(ble_hci_send_cmd(hci_reset_cmd, 3U) == BLE_ERR_INVALID_ARG, "ble_hci_send_cmd rejects len < 4");
    uint8_t bad_type_cmd[] = { 0x02U, 0x03U, 0x0CU, 0x00U };
    TEST_ASSERT(ble_hci_send_cmd(bad_type_cmd, 4U) == BLE_ERR_INVALID_ARG, "ble_hci_send_cmd rejects non-command type");

    TEST_ASSERT(ble_hci_send_cmd(hci_reset_cmd, sizeof(hci_reset_cmd)) == BLE_OK, "HCI_Reset dispatch succeeds");
    TEST_ASSERT(ble_hci_has_event() == true, "ble_hci_has_event indicates event queued");

    TEST_ASSERT(ble_hci_recv_event(NULL, sizeof(evt_resp), BLE_DEFAULT_TIMEOUT_MS) == BLE_ERR_INVALID_ARG, "ble_hci_recv_event rejects NULL");
    TEST_ASSERT(ble_hci_recv_event(evt_resp, 0U, BLE_DEFAULT_TIMEOUT_MS) == BLE_ERR_INVALID_ARG, "ble_hci_recv_event rejects max_len 0");
    TEST_ASSERT(ble_hci_recv_event(evt_resp, sizeof(evt_resp), BLE_DEFAULT_TIMEOUT_MS) == BLE_OK, "HCI_Reset response received");

    /* Expected: [0x04, 0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00] */
    TEST_ASSERT(evt_resp[0] == HCI_PKT_TYPE_EVT, "Response packet type is EVT (0x04)");
    TEST_ASSERT(evt_resp[1] == HCI_EVT_COMMAND_COMPLETE, "Event code is COMMAND_COMPLETE (0x0E)");
    TEST_ASSERT(evt_resp[2] == 0x04U, "Param length is 4");
    TEST_ASSERT(evt_resp[3] == 0x01U, "Num_HCI_Command_Packets is 1");
    TEST_ASSERT(evt_resp[4] == 0x03U && evt_resp[5] == 0x0CU, "Opcode is HCI_Reset (0x0C03)");
    TEST_ASSERT(evt_resp[6] == HCI_STATUS_SUCCESS, "Status is HCI_STATUS_SUCCESS (0x00)");

    /* 5. HCI Read BD_ADDR & Read Local Version Commands */
    uint8_t read_bd_cmd[] = { HCI_PKT_TYPE_CMD, 0x02U, 0x10U, 0x00U };
    TEST_ASSERT(ble_hci_execute_cmd(read_bd_cmd, sizeof(read_bd_cmd), evt_resp, sizeof(evt_resp), BLE_DEFAULT_TIMEOUT_MS) == BLE_OK,
                "ble_hci_execute_cmd HCI_Read_BD_Addr succeeds");
    TEST_ASSERT(evt_resp[0] == HCI_PKT_TYPE_EVT && evt_resp[1] == HCI_EVT_COMMAND_COMPLETE, "Read_BD_Addr returns Command Complete");
    TEST_ASSERT(evt_resp[4] == 0x02U && evt_resp[5] == 0x10U, "Opcode matches HCI_Read_BD_Addr (0x1002)");
    TEST_ASSERT(evt_resp[6] == HCI_STATUS_SUCCESS, "Read_BD_Addr status is success");
    TEST_ASSERT(evt_resp[7] == 0x14U && evt_resp[8] == 0x1EU && evt_resp[9] == 0x45U &&
                evt_resp[10] == 0xCAU && evt_resp[11] == 0x4CU && evt_resp[12] == 0x40U,
                "Read_BD_Addr little-endian address matches hardware BD_ADDR");

    uint8_t read_ver_cmd[] = { HCI_PKT_TYPE_CMD, 0x01U, 0x10U, 0x00U };
    TEST_ASSERT(ble_hci_execute_cmd(read_ver_cmd, sizeof(read_ver_cmd), evt_resp, sizeof(evt_resp), BLE_DEFAULT_TIMEOUT_MS) == BLE_OK,
                "ble_hci_execute_cmd HCI_Read_Local_Version succeeds");
    TEST_ASSERT(evt_resp[6] == HCI_STATUS_SUCCESS, "Read_Local_Version status is success");
    TEST_ASSERT(evt_resp[7] == 0x0CU, "HCI version is 5.3 (0x0C)");
    TEST_ASSERT(evt_resp[11] == 0x02U && evt_resp[12] == 0x00U, "Manufacturer is Espressif (0x0002)");

    /* Unknown opcode handling */
    uint8_t unknown_cmd[] = { HCI_PKT_TYPE_CMD, 0xFEU, 0xFEU, 0x00U };
    TEST_ASSERT(ble_hci_execute_cmd(unknown_cmd, sizeof(unknown_cmd), evt_resp, sizeof(evt_resp), BLE_DEFAULT_TIMEOUT_MS) == BLE_OK,
                "Unknown HCI command dispatches cleanly");
    TEST_ASSERT(evt_resp[6] == HCI_STATUS_UNKNOWN_HCI_CMD, "Unknown command returns HCI_STATUS_UNKNOWN_HCI_CMD");

    /* 6. GAP Advertising State Machine */
    TEST_ASSERT(ble_gap_start_advertising() == BLE_OK, "ble_gap_start_advertising succeeds");
    TEST_ASSERT(ble_gap_get_state() == BLE_STATE_ADVERTISING, "State transitions to BLE_STATE_ADVERTISING");
    TEST_ASSERT(ble_get_telemetry(&telem) == BLE_OK, "ble_get_telemetry succeeds");
    TEST_ASSERT(telem.adv_start_count >= 1U, "adv_start_count incremented");

    TEST_ASSERT(ble_gap_stop_advertising() == BLE_OK, "ble_gap_stop_advertising succeeds");
    TEST_ASSERT(ble_gap_get_state() == BLE_STATE_STANDBY, "State returns to BLE_STATE_STANDBY");
    TEST_ASSERT(ble_get_telemetry(&telem) == BLE_OK, "ble_get_telemetry succeeds");
    TEST_ASSERT(telem.adv_stop_count >= 1U, "adv_stop_count incremented");

    /* 7. Static Zero-Allocation GATT Database */
    TEST_ASSERT(gatt_db_init() == GATT_OK, "gatt_db_init succeeds");
    TEST_ASSERT(gatt_db_get_count() == 16U, "GATT database contains exactly 16 static attributes");

    /* Lookup attributes by handle */
    const gatt_attribute_t *attr1 = gatt_db_find_by_handle(0x0001U);
    TEST_ASSERT(attr1 != NULL && attr1->uuid == GATT_UUID_PRIMARY_SERVICE, "Handle 0x0001 is Primary Service");
    const gatt_attribute_t *attr3 = gatt_db_find_by_handle(0x0003U);
    TEST_ASSERT(attr3 != NULL && attr3->uuid == GATT_UUID_CHAR_DEVICE_NAME, "Handle 0x0003 is Device Name");
    const gatt_attribute_t *attr6 = gatt_db_find_by_handle(0x0006U);
    TEST_ASSERT(attr6 != NULL && attr6->uuid == GATT_UUID_PRIMARY_SERVICE, "Handle 0x0006 is Primary Service (DevInfo)");
    const gatt_attribute_t *attr13 = gatt_db_find_by_handle(0x000DU);
    TEST_ASSERT(attr13 != NULL && attr13->uuid == GATT_UUID_PRIMARY_SERVICE, "Handle 0x000D is Primary Service (Custom 0xFFE0)");
    const gatt_attribute_t *attr15 = gatt_db_find_by_handle(0x000FU);
    TEST_ASSERT(attr15 != NULL && attr15->uuid == GATT_UUID_CHAR_CUSTOM_DATA, "Handle 0x000F is Custom Data");

    /* Lookup attributes by UUID */
    TEST_ASSERT(gatt_db_find_by_uuid(GATT_UUID_CHAR_DEVICE_NAME) == attr3, "Find by UUID 0x2A00 matches attr3");
    TEST_ASSERT(gatt_db_find_by_uuid(0xDEADU) == NULL, "Find by non-existent UUID returns NULL");

    /* Read attribute values */
    uint8_t read_buf[64];
    uint16_t read_len = 0U;
    TEST_ASSERT(gatt_db_read(0x0003U, read_buf, sizeof(read_buf), &read_len) == GATT_OK, "Read Device Name succeeds");
    TEST_ASSERT(read_len == 9U && s_strncmp((char *)read_buf, "IRON-V-C6", 9) == 0, "Device Name is 'IRON-V-C6'");

    TEST_ASSERT(gatt_db_read(0x000CU, read_buf, sizeof(read_buf), &read_len) == GATT_OK, "Read Firmware Revision succeeds");
    TEST_ASSERT(read_len == 5U && s_strncmp((char *)read_buf, "1.0.0", 5) == 0, "Firmware Revision is '1.0.0'");

    /* Write attribute values and verify readback */
    uint8_t write_data[] = "IRON-V-BLE-GATT-TEST-PAYLOAD";
    TEST_ASSERT(gatt_db_write(0x000FU, write_data, (uint16_t)sizeof(write_data)) == GATT_OK, "Write Custom Data (Handle 0x000F) succeeds");
    TEST_ASSERT(gatt_db_read(0x000FU, read_buf, sizeof(read_buf), &read_len) == GATT_OK, "Readback Custom Data succeeds");
    TEST_ASSERT(read_len == sizeof(write_data) && s_strcmp((char *)read_buf, (char *)write_data) == 0, "Readback matches written payload");

    /* Permission enforcement & error checking */
    TEST_ASSERT(gatt_db_write(0x0003U, write_data, 10U) == GATT_ERR_WRITE_NOT_PERMITTED, "Write to read-only Device Name rejected");
    TEST_ASSERT(gatt_db_read(0x9999U, read_buf, sizeof(read_buf), &read_len) == GATT_ERR_INVALID_HANDLE, "Read invalid handle rejected");
    TEST_ASSERT(gatt_db_write(0x9999U, write_data, 10U) == GATT_ERR_INVALID_HANDLE, "Write invalid handle rejected");
    TEST_ASSERT(gatt_db_read(0x0001U, NULL, sizeof(read_buf), &read_len) == GATT_ERR_INVALID_ARG, "Read with NULL buffer rejected");
    TEST_ASSERT(gatt_db_write(0x000FU, NULL, 10U) == GATT_ERR_INVALID_ARG, "Write with NULL data rejected");
}

static void test_wifi_mac_subsystem(void)
{
    printf("  [TEST] 802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Packet Ring (Task 5.3)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)EFUSE_MAC_SYS_0_REG == 0x600B0844U, "EFUSE_MAC_SYS_0_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_MAC_SYS_1_REG == 0x600B0848U, "EFUSE_MAC_SYS_1_REG address calculation");
    TEST_ASSERT(WIFI_GDMA_CHANNEL == GDMA_CHANNEL_1, "Wi-Fi bound to GDMA Channel 1");

    /* 2. Concrete Data Structure Geometry & Memory Sizing */
    TEST_ASSERT(PACKET_BUFFER_SIZE == 1536U, "PACKET_BUFFER_SIZE must be exactly 1536 bytes");
    TEST_ASSERT(PACKET_RING_COUNT == 32U, "PACKET_RING_COUNT must be exactly 32 descriptors");
    TEST_ASSERT(sizeof(net_packet_t) == (sizeof(dma_descriptor_t) + PACKET_BUFFER_SIZE), "net_packet_t layout packed with descriptor and buffer");
    TEST_ASSERT((sizeof(net_packet_t) % 4U) == 0U, "net_packet_t must be 4-byte aligned");

    /* 3. Subsystem Lifecycle & MAC Address Retrieval */
    TEST_ASSERT(wifi_init() == WIFI_OK, "wifi_init succeeds");
    TEST_ASSERT(wifi_get_state() == WIFI_STATE_IDLE, "Wi-Fi initial state is WIFI_STATE_IDLE");

    uint8_t mac[WIFI_MAC_ADDR_LEN] = {0};
    TEST_ASSERT(wifi_get_mac_addr(NULL) == WIFI_ERR_INVALID_ARG, "wifi_get_mac_addr rejects NULL");
    TEST_ASSERT(wifi_get_mac_addr(mac) == WIFI_OK, "wifi_get_mac_addr succeeds");
    TEST_ASSERT(mac[0] == 0x40U && mac[1] == 0x4CU && mac[2] == 0xCAU &&
                mac[3] == 0x45U && mac[4] == 0x1EU && mac[5] == 0x14U,
                "Authentic Wi-Fi Station MAC matches hardware 40:4C:CA:45:1E:14");

    wifi_telemetry_t telem;
    TEST_ASSERT(wifi_get_telemetry(NULL) == WIFI_ERR_INVALID_ARG, "wifi_get_telemetry rejects NULL");
    TEST_ASSERT(wifi_get_telemetry(&telem) == WIFI_OK, "wifi_get_telemetry succeeds");
    TEST_ASSERT(telem.rx_ring_capacity == PACKET_RING_COUNT, "RX ring capacity is 32");
    TEST_ASSERT(telem.tx_ring_capacity == WIFI_TX_RING_COUNT, "TX ring capacity is 8");

    /* 4. Circular Packet Ring Integrity & Boundary Traversal (TEST 31) */
    uint32_t visited_count = 0U;
    TEST_ASSERT(wifi_verify_rx_ring(NULL) == WIFI_ERR_INVALID_ARG, "wifi_verify_rx_ring rejects NULL");
    TEST_ASSERT(wifi_verify_rx_ring(&visited_count) == WIFI_OK, "wifi_verify_rx_ring succeeds");
    TEST_ASSERT(visited_count == PACKET_RING_COUNT, "Circular traversal visits all 32 descriptors and loops back");

    /* 5. Zero-Copy Packet Reception Polling & Buffer Release */
    net_packet_t *rx_pkt = NULL;
    uint16_t rx_len = 0U;
    TEST_ASSERT(wifi_rx_poll(&rx_pkt, &rx_len) == WIFI_ERR_RING_EMPTY, "wifi_rx_poll returns RING_EMPTY when no frames arrived");

    /* Simulate hardware DMA completing reception of a 64-byte 802.11 frame */
    net_packet_t *active_pkt = (net_packet_t *)wifi_get_rx_packet(0U);
    TEST_ASSERT(active_pkt != NULL, "Get active packet pointer");
    active_pkt->dma_desc.length = 64U;
    active_pkt->dma_desc.owner = DMA_OWNER_CPU; /* Hardware transfers ownership to CPU */
    memcpy(active_pkt->payload, "WIFI_80211_FRAME_TEST_PAYLOAD_ABCDEF", 36);

    TEST_ASSERT(wifi_rx_poll(&rx_pkt, &rx_len) == WIFI_OK, "wifi_rx_poll succeeds when DMA_OWNER_CPU");
    TEST_ASSERT(rx_pkt == active_pkt, "Zero-copy: returned buffer pointer matches static ring entry");
    TEST_ASSERT(rx_len == 64U, "Reported packet length matches 64 bytes");
    TEST_ASSERT(s_strncmp((char *)rx_pkt->payload, "WIFI_80211_FRAME_TEST_PAYLOAD", 29) == 0, "Packet payload matches without copy");

    /* Release packet buffer back to DMA */
    TEST_ASSERT(wifi_rx_release(rx_pkt) == WIFI_OK, "wifi_rx_release succeeds");
    TEST_ASSERT(active_pkt->dma_desc.owner == DMA_OWNER_DMA, "Ownership restored to DMA");
    TEST_ASSERT(active_pkt->dma_desc.length == 0U, "Descriptor length reset to 0");
    TEST_ASSERT(wifi_rx_poll(&rx_pkt, &rx_len) == WIFI_ERR_RING_EMPTY, "Ring empty after packet released");

    /* 6. Packet Transmission Routine */
    uint8_t tx_frame[128];
    memset(tx_frame, 0x5AU, sizeof(tx_frame));
    TEST_ASSERT(wifi_tx_packet(NULL, sizeof(tx_frame)) == WIFI_ERR_INVALID_ARG, "wifi_tx_packet rejects NULL payload");
    TEST_ASSERT(wifi_tx_packet(tx_frame, 0U) == WIFI_ERR_INVALID_ARG, "wifi_tx_packet rejects length 0");
    TEST_ASSERT(wifi_tx_packet(tx_frame, 2048U) == WIFI_ERR_INVALID_ARG, "wifi_tx_packet rejects length > 1536");
    TEST_ASSERT(wifi_tx_packet(tx_frame, sizeof(tx_frame)) == WIFI_OK, "wifi_tx_packet succeeds");

    TEST_ASSERT(wifi_get_telemetry(&telem) == WIFI_OK, "wifi_get_telemetry succeeds");
    TEST_ASSERT(telem.tx_packets >= 1U, "tx_packets telemetry incremented");
    TEST_ASSERT(telem.tx_bytes >= sizeof(tx_frame), "tx_bytes telemetry incremented");
}

static void test_ieee802154_subsystem(void)
{
    printf("  [TEST] IEEE 802.15.4 Radio Transceiver Driver (Task 5.4)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)IEEE802154_COMMAND_REG == 0x600A3000U, "IEEE802154_COMMAND_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_CTRL_CFG_REG == 0x600A3004U, "IEEE802154_CTRL_CFG_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_INF0_SHORT_ADDR_REG == 0x600A3008U, "IEEE802154_INF0_SHORT_ADDR_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_INF0_PAN_ID_REG == 0x600A300CU, "IEEE802154_INF0_PAN_ID_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_INF0_EXTEND_ADDR0_REG == 0x600A3010U, "IEEE802154_INF0_EXTEND_ADDR0_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_INF0_EXTEND_ADDR1_REG == 0x600A3014U, "IEEE802154_INF0_EXTEND_ADDR1_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_CHANNEL_REG == 0x600A3048U, "IEEE802154_CHANNEL_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_TX_POWER_REG == 0x600A304CU, "IEEE802154_TX_POWER_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_RX_STATUS_REG == 0x600A3080U, "IEEE802154_RX_STATUS_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_TX_STATUS_REG == 0x600A3084U, "IEEE802154_TX_STATUS_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_TXRX_STATUS_REG == 0x600A3088U, "IEEE802154_TXRX_STATUS_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_MAC_DATE_REG == 0x600A3184U, "IEEE802154_MAC_DATE_REG address calculation");

    /* 2. Subsystem Lifecycle & Default Configuration */
    TEST_ASSERT(ieee802154_init() == IEEE802154_OK, "ieee802154_init succeeds");
    TEST_ASSERT(ieee802154_get_state() == IEEE802154_STATE_TRX_OFF, "Initial radio state is TRX_OFF");
    TEST_ASSERT(ieee802154_get_date_version() == IEEE802154_MAC_DATE_EXPECTED, "Silicon date version matches expected");

    ieee802154_telemetry_t telem;
    TEST_ASSERT(ieee802154_get_telemetry(NULL) == IEEE802154_ERR_INVALID_ARG, "get_telemetry rejects NULL");
    TEST_ASSERT(ieee802154_get_telemetry(&telem) == IEEE802154_OK, "get_telemetry succeeds");
    TEST_ASSERT(telem.channel == IEEE802154_CHANNEL_DEFAULT, "Default channel is 15");
    TEST_ASSERT(telem.freq_mhz == 2425U, "Default channel 15 frequency is 2425 MHz");
    TEST_ASSERT(telem.short_addr == IEEE802154_DEFAULT_SHORT_ADDR, "Default short address is 0x1234");
    TEST_ASSERT(telem.pan_id == IEEE802154_DEFAULT_PAN_ID, "Default PAN ID is 0x1A2B");
    TEST_ASSERT(telem.auto_ack_tx == true, "Auto-ACK TX enabled by default");
    TEST_ASSERT(telem.auto_ack_rx == true, "Auto-ACK RX enabled by default");

    /* 3. RF Channel & Frequency Range Validation */
    TEST_ASSERT(ieee802154_set_channel(10U) == IEEE802154_ERR_INVALID_ARG, "Channel 10 below range rejected");
    TEST_ASSERT(ieee802154_set_channel(27U) == IEEE802154_ERR_INVALID_ARG, "Channel 27 above range rejected");
    TEST_ASSERT(ieee802154_set_channel(11U) == IEEE802154_OK, "Channel 11 accepted");
    TEST_ASSERT(ieee802154_get_channel() == 11U, "Channel readback is 11");
    TEST_ASSERT(ieee802154_get_freq_mhz(11U) == 2405U, "Channel 11 frequency is 2405 MHz");
    TEST_ASSERT(ieee802154_set_channel(26U) == IEEE802154_OK, "Channel 26 accepted");
    TEST_ASSERT(ieee802154_get_freq_mhz(26U) == 2480U, "Channel 26 frequency is 2480 MHz");
    TEST_ASSERT(ieee802154_set_channel(15U) == IEEE802154_OK, "Channel 15 restored");

    /* 4. Addressing Controls (Short, PAN ID, EUI-64) */
    TEST_ASSERT(ieee802154_set_short_address(0xABCDU) == IEEE802154_OK, "Set short address succeeds");
    TEST_ASSERT(ieee802154_get_short_address() == 0xABCDU, "Short address readback matches 0xABCD");
    TEST_ASSERT(ieee802154_set_short_address(0x1234U) == IEEE802154_OK, "Short address 0x1234 restored");

    TEST_ASSERT(ieee802154_set_pan_id(0xCAFEU) == IEEE802154_OK, "Set PAN ID succeeds");
    TEST_ASSERT(ieee802154_get_pan_id() == 0xCAFEU, "PAN ID readback matches 0xCAFE");
    TEST_ASSERT(ieee802154_set_pan_id(0x1A2BU) == IEEE802154_OK, "PAN ID 0x1A2B restored");

    uint8_t ext_addr[IEEE802154_EXT_ADDR_LEN] = {0};
    TEST_ASSERT(ieee802154_get_extended_address(NULL) == IEEE802154_ERR_INVALID_ARG, "get_extended_address rejects NULL");
    TEST_ASSERT(ieee802154_get_extended_address(ext_addr) == IEEE802154_OK, "get_extended_address succeeds");
    TEST_ASSERT(ext_addr[0] == 0x40U && ext_addr[1] == 0x4CU && ext_addr[2] == 0xCAU &&
                ext_addr[3] == 0xFFU && ext_addr[4] == 0xFEU && ext_addr[5] == 0x45U &&
                ext_addr[6] == 0x1EU && ext_addr[7] == 0x14U,
                "Authentic EUI-64 matches eFuse MAC 40:4C:CA:FF:FE:45:1E:14");

    /* 5. Auto-ACK, Promiscuous Mode & TX Power */
    TEST_ASSERT(ieee802154_set_auto_ack(false, true) == IEEE802154_OK, "Disable TX ACK succeeds");
    TEST_ASSERT(ieee802154_set_promiscuous(true) == IEEE802154_OK, "Enable promiscuous mode succeeds");
    TEST_ASSERT(ieee802154_set_auto_ack(true, true) == IEEE802154_OK, "Restore auto-ACK succeeds");
    TEST_ASSERT(ieee802154_set_promiscuous(false) == IEEE802154_OK, "Disable promiscuous mode succeeds");

    TEST_ASSERT(ieee802154_set_tx_power(0x20U) == IEEE802154_ERR_INVALID_ARG, "TX power > 31 rejected");
    TEST_ASSERT(ieee802154_set_tx_power(0x15U) == IEEE802154_OK, "Set TX power 0x15 succeeds");
    TEST_ASSERT(ieee802154_get_tx_power() == 0x15U, "TX power readback matches 0x15");
    TEST_ASSERT(ieee802154_set_tx_power(IEEE802154_TX_POWER_DEFAULT) == IEEE802154_OK, "Restore default TX power");

    /* 6. Command Dispatcher & State Machine */
    TEST_ASSERT(ieee802154_cmd((ieee802154_cmd_t)0xFFU) == IEEE802154_ERR_INVALID_ARG, "Invalid command opcode rejected");
    TEST_ASSERT(ieee802154_cmd(IEEE802154_CMD_RX_START) == IEEE802154_OK, "CMD_RX_START succeeds");
    TEST_ASSERT(ieee802154_get_state() == IEEE802154_STATE_RX, "State is IEEE802154_STATE_RX");
    TEST_ASSERT(ieee802154_cmd(IEEE802154_CMD_TX_START) == IEEE802154_OK, "CMD_TX_START succeeds");
    TEST_ASSERT(ieee802154_get_state() == IEEE802154_STATE_TX, "State is IEEE802154_STATE_TX");
    TEST_ASSERT(ieee802154_cmd(IEEE802154_CMD_FORCE_TRX_OFF) == IEEE802154_OK, "CMD_FORCE_TRX_OFF succeeds");
    TEST_ASSERT(ieee802154_get_state() == IEEE802154_STATE_TRX_OFF, "State returns to TRX_OFF");
}

static void test_tcpip_subsystem(void)
{
    printf("  [TEST] Bare-Metal TCP/IP Stack & Lightweight Protocol Engine (Task 5.5)...\n");
    /* 1. Subsystem Initialization & IP Configuration */
    TEST_ASSERT(net_init() == NET_OK, "net_init succeeds");
    TEST_ASSERT(tcp_init() == TCP_OK, "tcp_init succeeds");

    net_config_t cfg;
    TEST_ASSERT(net_get_config(NULL) == NET_ERR_INVALID_ARG, "get_config rejects NULL");
    TEST_ASSERT(net_get_config(&cfg) == NET_OK, "get_config succeeds");
    TEST_ASSERT(cfg.ip == NET_DEFAULT_IP, "Default IP is 192.168.1.100");
    TEST_ASSERT(cfg.netmask == NET_DEFAULT_NETMASK, "Default netmask is 255.255.255.0");
    TEST_ASSERT(cfg.gateway == NET_DEFAULT_GATEWAY, "Default gateway is 192.168.1.1");

    /* IP String Conversions */
    char ip_str[NET_IP_STR_BUF_LEN];
    net_ip_to_str(cfg.ip, ip_str, sizeof(ip_str));
    TEST_ASSERT(strcmp(ip_str, "192.168.1.100") == 0, "net_ip_to_str produces 192.168.1.100");
    uint32_t parsed_ip = net_str_to_ip("192.168.1.55");
    TEST_ASSERT(parsed_ip == NET_IP4_ADDR(192U, 168U, 1U, 55U), "net_str_to_ip parses 192.168.1.55");
    TEST_ASSERT(net_set_ip(parsed_ip, cfg.netmask, cfg.gateway) == NET_OK, "net_set_ip succeeds");
    TEST_ASSERT(net_set_ip(NET_DEFAULT_IP, cfg.netmask, cfg.gateway) == NET_OK, "restore default IP");

    /* 2. RFC 1071 Internet Checksum Calculations */
    static const uint8_t s_rfc1071_test_header[RFC1071_TEST_HDR_LEN] = {
        0x45, 0x00, 0x00, 0x3c,
        0x1c, 0x46, 0x40, 0x00,
        0x40, 0x06, 0x00, 0x00,
        0xac, 0x10, 0x0a, 0x63,
        0xac, 0x10, 0x0a, 0x0c
    };
    uint16_t computed_chk = net_checksum(s_rfc1071_test_header, RFC1071_TEST_HDR_LEN);
    TEST_ASSERT(computed_chk == RFC1071_TEST_EXPECTED_CHECKSUM, "RFC 1071 checksum is 0xB1E6");

    uint8_t verified_hdr[RFC1071_TEST_HDR_LEN];
    memcpy(verified_hdr, s_rfc1071_test_header, RFC1071_TEST_HDR_LEN);
    verified_hdr[10] = (uint8_t)(computed_chk >> 8U);
    verified_hdr[11] = (uint8_t)(computed_chk & 0xFFU);
    uint16_t verify_chk = net_checksum(verified_hdr, RFC1071_TEST_HDR_LEN);
    TEST_ASSERT(verify_chk == 0x0000U, "Completed header checksum validates to 0x0000");

    /* 3. ARP Cache Operations */
    uint8_t found_mac[ETH_ADDR_LEN] = {0};
    TEST_ASSERT(arp_lookup(NET_IP4_ADDR(10U, 0U, 0U, 1U), found_mac) == NET_ERR_NOT_FOUND, "Lookup unmapped IP returns NOT_FOUND");
    uint8_t test_mac[ETH_ADDR_LEN] = { 0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U };
    TEST_ASSERT(arp_insert(NET_IP4_ADDR(10U, 0U, 0U, 1U), test_mac) == NET_OK, "arp_insert succeeds");
    TEST_ASSERT(arp_lookup(NET_IP4_ADDR(10U, 0U, 0U, 1U), found_mac) == NET_OK, "Lookup mapped IP succeeds");
    TEST_ASSERT(found_mac[0] == test_mac[0] && found_mac[1] == test_mac[1] &&
                found_mac[2] == test_mac[2] && found_mac[3] == test_mac[3] &&
                found_mac[4] == test_mac[4] && found_mac[5] == test_mac[5],
                "MAC readback matches inserted MAC");

    /* 4. Synthetic ARP Request -> Reply Generation */
    arp_frame_t arp_req;
    memset(&arp_req, 0, sizeof(arp_req));
    memset(arp_req.eth.dest_mac, 0xFF, ETH_ADDR_LEN);
    memcpy(arp_req.eth.src_mac, test_mac, ETH_ADDR_LEN);
    arp_req.eth.ethertype = NET_HTONS(ETHERTYPE_ARP);
    arp_req.arp.hw_type = NET_HTONS(ARP_HW_TYPE_ETHERNET);
    arp_req.arp.proto_type = NET_HTONS(ARP_PROTO_IPV4);
    arp_req.arp.hw_size = ETH_ADDR_LEN;
    arp_req.arp.proto_size = IPV4_ADDR_LEN;
    arp_req.arp.opcode = NET_HTONS(ARP_OPCODE_REQUEST);
    memcpy(arp_req.arp.sender_mac, test_mac, ETH_ADDR_LEN);
    arp_req.arp.sender_ip = NET_HTONL(NET_IP4_ADDR(10U, 0U, 0U, 1U));
    arp_req.arp.target_ip = NET_HTONL(NET_DEFAULT_IP);

    uint8_t reply_buf[64] = {0};
    uint16_t reply_len = 0U;
    TEST_ASSERT(arp_process_packet((const uint8_t *)&arp_req, sizeof(arp_req), reply_buf, sizeof(reply_buf), &reply_len) == NET_OK, "arp_process_packet succeeds");
    TEST_ASSERT(reply_len == sizeof(arp_frame_t), "Reply length is 42 bytes");
    const arp_frame_t *reply_f = (const arp_frame_t *)reply_buf;
    TEST_ASSERT(reply_f->arp.opcode == NET_HTONS(ARP_OPCODE_REPLY), "Opcode is ARP_OPCODE_REPLY");
    TEST_ASSERT(reply_f->arp.sender_ip == NET_HTONL(NET_DEFAULT_IP), "Sender IP in reply is our IP");
    TEST_ASSERT(reply_f->arp.target_ip == NET_HTONL(NET_IP4_ADDR(10U, 0U, 0U, 1U)), "Target IP in reply is requester IP");

    /* 5. Synthetic ICMP Echo Request -> Echo Reply Generation */
    uint8_t icmp_frame_buf[128] = {0};
    ethernet_header_t *eth_req = (ethernet_header_t *)icmp_frame_buf;
    ipv4_header_t *ip_req = (ipv4_header_t *)(icmp_frame_buf + ETH_HDR_LEN);
    icmp_header_t *icmp_req = (icmp_header_t *)(icmp_frame_buf + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    uint8_t *icmp_payload = icmp_frame_buf + ETH_HDR_LEN + IPV4_MIN_HDR_LEN + ICMP_MIN_HDR_LEN;

    memcpy(eth_req->src_mac, test_mac, ETH_ADDR_LEN);
    memcpy(eth_req->dest_mac, cfg.mac, ETH_ADDR_LEN);
    eth_req->ethertype = NET_HTONS(ETHERTYPE_IPV4);

    ip_req->ver_ihl = IPV4_VER_IHL_DEFAULT;
    ip_req->total_len = NET_HTONS(IPV4_MIN_HDR_LEN + ICMP_MIN_HDR_LEN + 4U);
    ip_req->protocol = IPV4_PROTO_ICMP;
    ip_req->ttl = 64U;
    ip_req->src_ip = NET_HTONL(NET_IP4_ADDR(10U, 0U, 0U, 1U));
    ip_req->dest_ip = NET_HTONL(NET_DEFAULT_IP);
    ip_req->checksum = net_ipv4_checksum(ip_req);

    icmp_req->type = ICMP_TYPE_ECHO_REQUEST;
    icmp_req->code = ICMP_CODE_ECHO;
    icmp_req->id = NET_HTONS(0x1234U);
    icmp_req->sequence = NET_HTONS(1U);
    icmp_payload[0] = 'P'; icmp_payload[1] = 'I'; icmp_payload[2] = 'N'; icmp_payload[3] = 'G';
    icmp_req->checksum = net_checksum(icmp_req, ICMP_MIN_HDR_LEN + 4U);

    uint16_t req_len = ETH_HDR_LEN + IPV4_MIN_HDR_LEN + ICMP_MIN_HDR_LEN + 4U;
    uint8_t icmp_rep_buf[128] = {0};
    uint16_t icmp_rep_len = 0U;
    TEST_ASSERT(icmp_process_packet(icmp_frame_buf, req_len, icmp_rep_buf, sizeof(icmp_rep_buf), &icmp_rep_len) == NET_OK, "icmp_process_packet succeeds");
    TEST_ASSERT(icmp_rep_len == req_len, "ICMP reply length matches request length");
    const icmp_header_t *rep_icmp = (const icmp_header_t *)(icmp_rep_buf + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    TEST_ASSERT(rep_icmp->type == ICMP_TYPE_ECHO_REPLY, "Echo reply type is 0");
    TEST_ASSERT(rep_icmp->code == ICMP_CODE_ECHO, "Echo reply code is 0");

    /* 6. TCP State Machine & PCB Lifecycle */
    tcp_pcb_t *pcb = tcp_new();
    TEST_ASSERT(pcb != NULL, "tcp_new allocates PCB");
    TEST_ASSERT(pcb->state == TCP_STATE_CLOSED, "Initial PCB state is CLOSED");
    TEST_ASSERT(tcp_bind(pcb, 80U) == TCP_OK, "tcp_bind to port 80 succeeds");
    TEST_ASSERT(tcp_listen(pcb, NULL) == TCP_OK, "tcp_listen succeeds");
    TEST_ASSERT(pcb->state == TCP_STATE_LISTEN, "State transitions to LISTEN");
    TEST_ASSERT(tcp_close(pcb) == TCP_OK, "tcp_close succeeds");
    TEST_ASSERT(pcb->state == TCP_STATE_CLOSED, "State returns to CLOSED");
}

/* ========================================================================= */
/* Phase 0-3 Host Test Hardening: Cross-Module Integration & Edge Case Tests */
/* ========================================================================= */

#define TEST_COROUTINE_ROUNDS               5U
#define TEST_COROUTINE_TIMER_INTERVAL_US    1000U
#define TEST_COROUTINE_TICKS_PER_US         16U
#define TEST_COROUTINE_TOTAL_EVENTS         10U
#define TEST_DPC_ARG_MAGIC_A                0xAAAA0000U
#define TEST_DPC_ARG_MAGIC_B                0xBBBB0000U
#define TEST_DPC_RECORD_CAPACITY            64U

typedef struct {
    uint32_t arg0;
    uint32_t arg1;
    uint32_t seq;
} test_dpc_event_record_t;

static test_dpc_event_record_t s_test_dpc_records[TEST_DPC_RECORD_CAPACITY];
static uint32_t s_test_dpc_event_count = 0U;

static void test_dpc_integration_handler(uint32_t arg0, uint32_t arg1)
{
    if (s_test_dpc_event_count < TEST_DPC_RECORD_CAPACITY)
    {
        s_test_dpc_records[s_test_dpc_event_count].arg0 = arg0;
        s_test_dpc_records[s_test_dpc_event_count].arg1 = arg1;
        s_test_dpc_records[s_test_dpc_event_count].seq  = s_test_dpc_event_count;
        s_test_dpc_event_count++;
    }
}

/*
 * Test 15: Cross-module Coroutine + SYSTIMER + DPC Integration
 * Tests Task A and Task B cooperatively yielding while simulated SYSTIMER tick alarms
 * enqueue DPC items, and a Worker task drains the DPC queue via dpc_process_all().
 */
static void test_coroutine_systimer_dpc_integration(void)
{
    printf("  [TEST] cross-module coroutine + systimer + dpc integration...\n");

    dpc_init();
    s_test_dpc_event_count = 0U;

    task_control_block_t task_a;
    task_control_block_t task_b;
    task_control_block_t task_worker;

    task_a.id = 1U;
    task_a.name = "Task_A";
    task_a.state = TASK_STATE_READY;
    task_a.priority = 10U;
    task_a.yield_count = 0U;
    task_a.runtime_ticks = 0U;

    task_b.id = 2U;
    task_b.name = "Task_B";
    task_b.state = TASK_STATE_READY;
    task_b.priority = 10U;
    task_b.yield_count = 0U;
    task_b.runtime_ticks = 0U;

    task_worker.id = 3U;
    task_worker.name = "Task_Worker";
    task_worker.state = TASK_STATE_READY;
    task_worker.priority = 12U;
    task_worker.yield_count = 0U;
    task_worker.runtime_ticks = 0U;

    for (uint32_t r = 0U; r < TEST_COROUTINE_ROUNDS; r++)
    {
        /* 1. Dispatch Task A */
        TEST_ASSERT(task_a.state == TASK_STATE_READY, "Task A ready before dispatch");
        task_a.state = TASK_STATE_RUNNING;
        TEST_ASSERT(task_a.state == TASK_STATE_RUNNING, "Task A in running state");

        /* Simulate SYSTIMER tick alarm firing during Task A execution */
        uint32_t tick_a_lo = (r * 2U + 0U) * TEST_COROUTINE_TIMER_INTERVAL_US * TEST_COROUTINE_TICKS_PER_US;
        uint32_t magic_a = TEST_DPC_ARG_MAGIC_A | r;
        int enq_a = dpc_enqueue(DPC_TYPE_TIMER_TICK, magic_a, tick_a_lo, test_dpc_integration_handler);
        TEST_ASSERT(enq_a == DPC_STATUS_OK, "Task A timer tick DPC enqueue succeeds");

        /* Task A yields */
        task_a.state = TASK_STATE_READY;
        task_a.yield_count++;
        task_a.runtime_ticks += 100U;
        TEST_ASSERT(task_a.state == TASK_STATE_READY, "Task A yielded to ready state");

        /* 2. Dispatch Task B */
        TEST_ASSERT(task_b.state == TASK_STATE_READY, "Task B ready before dispatch");
        task_b.state = TASK_STATE_RUNNING;
        TEST_ASSERT(task_b.state == TASK_STATE_RUNNING, "Task B in running state");

        /* Simulate SYSTIMER tick alarm firing during Task B execution */
        uint32_t tick_b_lo = (r * 2U + 1U) * TEST_COROUTINE_TIMER_INTERVAL_US * TEST_COROUTINE_TICKS_PER_US;
        uint32_t magic_b = TEST_DPC_ARG_MAGIC_B | r;
        int enq_b = dpc_enqueue(DPC_TYPE_TIMER_TICK, magic_b, tick_b_lo, test_dpc_integration_handler);
        TEST_ASSERT(enq_b == DPC_STATUS_OK, "Task B timer tick DPC enqueue succeeds");

        /* Task B yields */
        task_b.state = TASK_STATE_READY;
        task_b.yield_count++;
        task_b.runtime_ticks += 150U;
        TEST_ASSERT(task_b.state == TASK_STATE_READY, "Task B yielded to ready state");

        /* 3. Dispatch Worker Task */
        TEST_ASSERT(task_worker.state == TASK_STATE_READY, "Worker ready before dispatch");
        task_worker.state = TASK_STATE_RUNNING;
        TEST_ASSERT(task_worker.state == TASK_STATE_RUNNING, "Worker in running state");

        TEST_ASSERT(dpc_get_size() == 2U, "Worker observes exactly 2 pending DPC events");
        uint32_t drained = dpc_process_all();
        TEST_ASSERT(drained == 2U, "Worker drains exactly 2 DPC events");
        TEST_ASSERT(dpc_get_size() == 0U, "DPC queue empty after worker drain");

        /* Worker yields */
        task_worker.state = TASK_STATE_READY;
        task_worker.yield_count++;
        task_worker.runtime_ticks += 50U;
        TEST_ASSERT(task_worker.state == TASK_STATE_READY, "Worker yielded to ready state");
    }

    /* Terminate tasks after cooperative loop completion */
    task_a.state = TASK_STATE_TERMINATED;
    task_b.state = TASK_STATE_TERMINATED;
    task_worker.state = TASK_STATE_TERMINATED;

    TEST_ASSERT(task_a.state == TASK_STATE_TERMINATED, "Task A cleanly transitioned to TERMINATED");
    TEST_ASSERT(task_b.state == TASK_STATE_TERMINATED, "Task B cleanly transitioned to TERMINATED");
    TEST_ASSERT(task_worker.state == TASK_STATE_TERMINATED, "Worker cleanly transitioned to TERMINATED");
    TEST_ASSERT(task_a.yield_count == TEST_COROUTINE_ROUNDS, "Task A yield count matches expected rounds");
    TEST_ASSERT(task_b.yield_count == TEST_COROUTINE_ROUNDS, "Task B yield count matches expected rounds");
    TEST_ASSERT(task_worker.yield_count == TEST_COROUTINE_ROUNDS, "Worker yield count matches expected rounds");

    /* Verify global DPC statistics */
    TEST_ASSERT(dpc_get_drop_count() == 0U, "DPC queue zero drop count confirmed");
    TEST_ASSERT(dpc_get_processed_count() == TEST_COROUTINE_TOTAL_EVENTS, "Total DPC processed count equals 10");
    TEST_ASSERT(s_test_dpc_event_count == TEST_COROUTINE_TOTAL_EVENTS, "Recorded callback count equals 10");

    /* Assert strict FIFO ordering and exact event arguments */
    for (uint32_t i = 0U; i < TEST_COROUTINE_TOTAL_EVENTS; i++)
    {
        uint32_t round_idx = i / 2U;
        if ((i % 2U) == 0U)
        {
            uint32_t expected_magic_a = TEST_DPC_ARG_MAGIC_A | round_idx;
            uint32_t expected_tick_a = i * TEST_COROUTINE_TIMER_INTERVAL_US * TEST_COROUTINE_TICKS_PER_US;
            TEST_ASSERT(s_test_dpc_records[i].arg0 == expected_magic_a, "FIFO ordering: Task A magic matches");
            TEST_ASSERT(s_test_dpc_records[i].arg1 == expected_tick_a, "Exact event argument: Task A tick matches");
        }
        else
        {
            uint32_t expected_magic_b = TEST_DPC_ARG_MAGIC_B | round_idx;
            uint32_t expected_tick_b = i * TEST_COROUTINE_TIMER_INTERVAL_US * TEST_COROUTINE_TICKS_PER_US;
            TEST_ASSERT(s_test_dpc_records[i].arg0 == expected_magic_b, "FIFO ordering: Task B magic matches");
            TEST_ASSERT(s_test_dpc_records[i].arg1 == expected_tick_b, "Exact event argument: Task B tick matches");
        }
    }
}

#define TEST_ARENA_TASK1_SMALL_BLOCKS       20U
#define TEST_ARENA_TASK2_SMALL_BLOCKS       12U
#define TEST_ARENA_FREED_SMALL_SUBSET       4U
#define TEST_ARENA_TASK_A_MED_BLOCKS        10U
#define TEST_ARENA_TASK_B_MED_BLOCKS        6U
#define TEST_ARENA_FREED_MED_SUBSET         3U

/*
 * Test 16: Static Arena Pool Exhaustion & Concurrency
 * Tests multi-task contention on small/medium pools, full pool exhaustion,
 * subset freeing, bitmask updates, immediate reusability, and scratch arena mark/reset.
 */
static void test_arena_concurrency_exhaustion(void)
{
    printf("  [TEST] static arena pool exhaustion & multi-task concurrency...\n");

    arena_init();

    /* --- Part 1: Small Pool Multi-Task Contention (32 blocks x 64B) --- */
    void *task1_small_ptrs[TEST_ARENA_TASK1_SMALL_BLOCKS];
    void *task2_small_ptrs[TEST_ARENA_TASK2_SMALL_BLOCKS];

    /* Task 1 allocates 20 small blocks */
    for (uint32_t i = 0U; i < TEST_ARENA_TASK1_SMALL_BLOCKS; i++)
    {
        task1_small_ptrs[i] = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
        TEST_ASSERT(task1_small_ptrs[i] != NULL, "Task 1 small block alloc succeeds");
        TEST_ASSERT(((uintptr_t)task1_small_ptrs[i] & ARENA_ALIGN_MASK) == 0U, "Task 1 small block 4-byte aligned");
        for (uint32_t j = 0U; j < i; j++)
        {
            TEST_ASSERT(task1_small_ptrs[j] != task1_small_ptrs[i], "Task 1 block address unique");
        }
    }

    arena_pool_stats_t small_stats;
    arena_get_pool_stats(ARENA_POOL_SMALL, &small_stats);
    TEST_ASSERT(small_stats.active_count == TEST_ARENA_TASK1_SMALL_BLOCKS, "Small pool active count is 20 after Task 1");
    TEST_ASSERT(small_stats.allocated_mask == 0x000FFFFFU, "Small pool mask has lowest 20 bits set");

    /* Task 2 allocates 12 small blocks (exhausting the pool) */
    for (uint32_t i = 0U; i < TEST_ARENA_TASK2_SMALL_BLOCKS; i++)
    {
        task2_small_ptrs[i] = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
        TEST_ASSERT(task2_small_ptrs[i] != NULL, "Task 2 small block alloc succeeds");
        TEST_ASSERT(((uintptr_t)task2_small_ptrs[i] & ARENA_ALIGN_MASK) == 0U, "Task 2 small block 4-byte aligned");
        for (uint32_t j = 0U; j < TEST_ARENA_TASK1_SMALL_BLOCKS; j++)
        {
            TEST_ASSERT(task1_small_ptrs[j] != task2_small_ptrs[i], "Task 2 block does not collide with Task 1");
        }
    }

    arena_get_pool_stats(ARENA_POOL_SMALL, &small_stats);
    TEST_ASSERT(small_stats.active_count == ARENA_POOL_BLOCK_COUNT_SMALL, "Small pool active count at max capacity (32)");
    TEST_ASSERT(small_stats.allocated_mask == ARENA_BITMASK_FULL_SMALL, "Small pool mask fully populated (0xFFFFFFFF)");

    /* Full pool exhaustion: both tasks attempt additional allocations */
    void *exhaustion_1 = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
    TEST_ASSERT(exhaustion_1 == NULL, "Small pool exhaustion returns NULL to Task 1");
    void *exhaustion_2 = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
    TEST_ASSERT(exhaustion_2 == NULL, "Small pool exhaustion returns NULL to Task 2");

    arena_get_pool_stats(ARENA_POOL_SMALL, &small_stats);
    TEST_ASSERT(small_stats.active_count == ARENA_POOL_BLOCK_COUNT_SMALL, "Active count remains 32 after failed allocs");
    TEST_ASSERT(small_stats.allocated_mask == ARENA_BITMASK_FULL_SMALL, "Mask remains full after failed allocs");

    /* Task 2 frees a subset of 4 blocks (indices 0, 3, 6, 9 within task 2 array) */
    uint32_t freed_indices[TEST_ARENA_FREED_SMALL_SUBSET] = {0U, 3U, 6U, 9U};
    void *freed_ptrs[TEST_ARENA_FREED_SMALL_SUBSET];
    for (uint32_t k = 0U; k < TEST_ARENA_FREED_SMALL_SUBSET; k++)
    {
        uint32_t idx = freed_indices[k];
        freed_ptrs[k] = task2_small_ptrs[idx];
        TEST_ASSERT(arena_free(task2_small_ptrs[idx]) == ARENA_FREE_SUCCESS, "Task 2 block free succeeds");
    }

    arena_get_pool_stats(ARENA_POOL_SMALL, &small_stats);
    TEST_ASSERT(small_stats.active_count == (ARENA_POOL_BLOCK_COUNT_SMALL - TEST_ARENA_FREED_SMALL_SUBSET), "Active count decreased by 4");

    /* Immediate reusability: Task 1 re-allocates 4 blocks */
    void *realloc_ptrs[TEST_ARENA_FREED_SMALL_SUBSET];
    for (uint32_t k = 0U; k < TEST_ARENA_FREED_SMALL_SUBSET; k++)
    {
        realloc_ptrs[k] = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
        TEST_ASSERT(realloc_ptrs[k] != NULL, "Re-allocation of freed small block succeeds");
        /* Verify re-allocated pointer was one of the freed blocks */
        int found = 0;
        for (uint32_t m = 0U; m < TEST_ARENA_FREED_SMALL_SUBSET; m++)
        {
            if (realloc_ptrs[k] == freed_ptrs[m])
            {
                found = 1;
                break;
            }
        }
        TEST_ASSERT(found == 1, "Re-allocated block reuses an immediately freed block");
    }

    arena_get_pool_stats(ARENA_POOL_SMALL, &small_stats);
    TEST_ASSERT(small_stats.active_count == ARENA_POOL_BLOCK_COUNT_SMALL, "Active count returned to 32");
    TEST_ASSERT(small_stats.allocated_mask == ARENA_BITMASK_FULL_SMALL, "Mask returned to full");

    /* Free all Task 1 blocks, reallocated blocks, and remaining Task 2 blocks */
    for (uint32_t i = 0U; i < TEST_ARENA_TASK1_SMALL_BLOCKS; i++)
    {
        TEST_ASSERT(arena_free(task1_small_ptrs[i]) == ARENA_FREE_SUCCESS, "Clean free Task 1 small block");
    }
    for (uint32_t k = 0U; k < TEST_ARENA_FREED_SMALL_SUBSET; k++)
    {
        TEST_ASSERT(arena_free(realloc_ptrs[k]) == ARENA_FREE_SUCCESS, "Clean free reallocated block");
    }
    for (uint32_t i = 0U; i < TEST_ARENA_TASK2_SMALL_BLOCKS; i++)
    {
        if (i != 0U && i != 3U && i != 6U && i != 9U)
        {
            TEST_ASSERT(arena_free(task2_small_ptrs[i]) == ARENA_FREE_SUCCESS, "Clean free remaining Task 2 small block");
        }
    }

    arena_get_pool_stats(ARENA_POOL_SMALL, &small_stats);
    TEST_ASSERT(small_stats.active_count == 0U, "Small pool active count is 0 after full cleanup");
    TEST_ASSERT(small_stats.allocated_mask == ARENA_BITMASK_EMPTY, "Small pool mask is empty after full cleanup");

    /* --- Part 2: Medium Pool Multi-Task Contention (16 blocks x 256B) --- */
    void *task_a_med_ptrs[TEST_ARENA_TASK_A_MED_BLOCKS];
    void *task_b_med_ptrs[TEST_ARENA_TASK_B_MED_BLOCKS];

    for (uint32_t i = 0U; i < TEST_ARENA_TASK_A_MED_BLOCKS; i++)
    {
        task_a_med_ptrs[i] = arena_alloc(ARENA_POOL_BLOCK_SIZE_MEDIUM);
        TEST_ASSERT(task_a_med_ptrs[i] != NULL, "Task A medium block alloc succeeds");
        TEST_ASSERT(((uintptr_t)task_a_med_ptrs[i] & ARENA_ALIGN_MASK) == 0U, "Task A medium block 4-byte aligned");
    }

    for (uint32_t i = 0U; i < TEST_ARENA_TASK_B_MED_BLOCKS; i++)
    {
        task_b_med_ptrs[i] = arena_alloc(ARENA_POOL_BLOCK_SIZE_MEDIUM);
        TEST_ASSERT(task_b_med_ptrs[i] != NULL, "Task B medium block alloc succeeds");
        TEST_ASSERT(((uintptr_t)task_b_med_ptrs[i] & ARENA_ALIGN_MASK) == 0U, "Task B medium block 4-byte aligned");
    }

    arena_pool_stats_t med_stats;
    arena_get_pool_stats(ARENA_POOL_MEDIUM, &med_stats);
    TEST_ASSERT(med_stats.active_count == ARENA_POOL_BLOCK_COUNT_MEDIUM, "Medium pool active count at max capacity (16)");
    TEST_ASSERT(med_stats.allocated_mask == ARENA_BITMASK_FULL_MEDIUM, "Medium pool mask fully populated (0xFFFF)");

    /* Medium pool exhaustion check */
    TEST_ASSERT(arena_alloc(ARENA_POOL_BLOCK_SIZE_MEDIUM) == NULL, "Medium pool exhaustion returns NULL to Task A");
    TEST_ASSERT(arena_alloc(ARENA_POOL_BLOCK_SIZE_MEDIUM) == NULL, "Medium pool exhaustion returns NULL to Task B");

    /* Task A frees 3 blocks (indices 1, 4, 7) */
    void *freed_med_ptrs[TEST_ARENA_FREED_MED_SUBSET] = {task_a_med_ptrs[1], task_a_med_ptrs[4], task_a_med_ptrs[7]};
    for (uint32_t k = 0U; k < TEST_ARENA_FREED_MED_SUBSET; k++)
    {
        TEST_ASSERT(arena_free(freed_med_ptrs[k]) == ARENA_FREE_SUCCESS, "Task A medium block free succeeds");
    }

    arena_get_pool_stats(ARENA_POOL_MEDIUM, &med_stats);
    TEST_ASSERT(med_stats.active_count == (ARENA_POOL_BLOCK_COUNT_MEDIUM - TEST_ARENA_FREED_MED_SUBSET), "Medium active count decreased by 3");

    /* Task B immediately reuses the freed blocks */
    void *realloc_med_ptrs[TEST_ARENA_FREED_MED_SUBSET];
    for (uint32_t k = 0U; k < TEST_ARENA_FREED_MED_SUBSET; k++)
    {
        realloc_med_ptrs[k] = arena_alloc(ARENA_POOL_BLOCK_SIZE_MEDIUM);
        TEST_ASSERT(realloc_med_ptrs[k] != NULL, "Task B medium re-allocation succeeds");
    }

    /* Cleanup all medium blocks */
    for (uint32_t i = 0U; i < TEST_ARENA_TASK_A_MED_BLOCKS; i++)
    {
        if (i != 1U && i != 4U && i != 7U)
        {
            TEST_ASSERT(arena_free(task_a_med_ptrs[i]) == ARENA_FREE_SUCCESS, "Clean free Task A medium block");
        }
    }
    for (uint32_t k = 0U; k < TEST_ARENA_FREED_MED_SUBSET; k++)
    {
        TEST_ASSERT(arena_free(realloc_med_ptrs[k]) == ARENA_FREE_SUCCESS, "Clean free reallocated medium block");
    }
    for (uint32_t i = 0U; i < TEST_ARENA_TASK_B_MED_BLOCKS; i++)
    {
        TEST_ASSERT(arena_free(task_b_med_ptrs[i]) == ARENA_FREE_SUCCESS, "Clean free Task B medium block");
    }

    arena_get_pool_stats(ARENA_POOL_MEDIUM, &med_stats);
    TEST_ASSERT(med_stats.active_count == 0U, "Medium pool active count is 0 after full cleanup");
    TEST_ASSERT(med_stats.allocated_mask == ARENA_BITMASK_EMPTY, "Medium pool mask is empty after cleanup");

    /* --- Part 3: Scratch Arena Alignment Boundaries, Forward Reset Rejection & Overflow Guards --- */
    arena_scratch_reset(0U);
    TEST_ASSERT(arena_scratch_mark() == 0U, "Scratch arena starts at mark 0");

    void *sc_t1 = arena_scratch_alloc(128U);
    TEST_ASSERT(sc_t1 != NULL, "Task 1 scratch alloc 128 succeeds");
    TEST_ASSERT(((uintptr_t)sc_t1 & ARENA_ALIGN_MASK) == 0U, "Task 1 scratch alloc 4-byte aligned");

    arena_scratch_mark_t mark_t1 = arena_scratch_mark();
    TEST_ASSERT(mark_t1 == 128U, "Mark after Task 1 alloc is 128");

    void *sc_t2 = arena_scratch_alloc(256U);
    TEST_ASSERT(sc_t2 != NULL, "Task 2 scratch alloc 256 succeeds");
    TEST_ASSERT(((uintptr_t)sc_t2 & ARENA_ALIGN_MASK) == 0U, "Task 2 scratch alloc 4-byte aligned");
    TEST_ASSERT((uintptr_t)sc_t2 > (uintptr_t)sc_t1, "Task 2 scratch pointer advances beyond Task 1");

    arena_scratch_mark_t mark_t2 = arena_scratch_mark();
    TEST_ASSERT(mark_t2 == (128U + 256U), "Mark after Task 2 alloc is 384");

    void *sc_t3 = arena_scratch_alloc(64U);
    TEST_ASSERT(sc_t3 != NULL, "Task 1 additional scratch alloc 64 succeeds");
    arena_scratch_mark_t mark_t3 = arena_scratch_mark();
    TEST_ASSERT(mark_t3 == 448U, "Mark after third alloc is 448");

    /* Forward reset rejection: resetting beyond 448 must be rejected */
    arena_scratch_reset(512U);
    TEST_ASSERT(arena_scratch_mark() == 448U, "Forward reset to 512 rejected; mark unchanged");
    arena_scratch_reset(1024U);
    TEST_ASSERT(arena_scratch_mark() == 448U, "Forward reset to 1024 rejected; mark unchanged");

    /* Unaligned reset rejection */
    arena_scratch_reset(mark_t2 + 1U);
    TEST_ASSERT(arena_scratch_mark() == 448U, "Unaligned reset (+1) rejected; mark unchanged");
    arena_scratch_reset(mark_t2 + 2U);
    TEST_ASSERT(arena_scratch_mark() == 448U, "Unaligned reset (+2) rejected; mark unchanged");
    arena_scratch_reset(mark_t2 + 3U);
    TEST_ASSERT(arena_scratch_mark() == 448U, "Unaligned reset (+3) rejected; mark unchanged");

    /* Valid backward resets */
    arena_scratch_reset(mark_t2);
    TEST_ASSERT(arena_scratch_mark() == mark_t2, "Backward reset to mark_t2 (384) succeeds");

    arena_scratch_reset(mark_t1);
    TEST_ASSERT(arena_scratch_mark() == mark_t1, "Backward reset to mark_t1 (128) succeeds");

    /* Scratch overflow guards */
    TEST_ASSERT(arena_scratch_alloc(ARENA_SCRATCH_TOTAL_SIZE + 1U) == NULL, "Capacity overflow returns NULL");
    TEST_ASSERT(arena_scratch_alloc((size_t)-1) == NULL, "Integer overflow (size_t)-1 returns NULL");
    TEST_ASSERT(arena_scratch_alloc((size_t)-64) == NULL, "Integer overflow (size_t)-64 returns NULL");
    TEST_ASSERT(arena_scratch_alloc(0xFFFFFFFFU) == NULL, "Integer overflow 0xFFFFFFFF returns NULL");

    arena_scratch_reset(0U);
    TEST_ASSERT(arena_scratch_mark() == 0U, "Reset to 0 restores clean scratch arena");
}

#define TEST_PMP_TOR_REGION_0_LEN           0x1000U
#define TEST_PMP_TOR_REGION_1_START         0x1000U
#define TEST_PMP_TOR_REGION_1_LEN           0x4000U
#define TEST_PMP_TOR_REGION_2_START         0x5000U
#define TEST_PMP_TOR_REGION_2_LEN           0xB000U
#define TEST_PMP_TOR_REGION_3_START         0x10000U
#define TEST_PMP_TOR_INVALID_START_1        0x2000U
#define TEST_PMP_TOR_INVALID_START_2        0x6000U

/*
 * Test 17: PMP Chained Top-of-Range (TOR) Boundary Edge Cases & Locked Regions
 * Tests chained multi-region TOR (0, 1, 2), invalid non-contiguous chained TOR rejection,
 * zero-length TOR handling, locked region enforcement (PMP_CFG_L_BIT), and readback decode.
 */
static void test_pmp_chained_tor_and_locked_regions(void)
{
    printf("  [TEST] pmp chained top-of-range (TOR) & locked region enforcement...\n");

    pmp_init();

    /* --- Part 1: Chained Multi-Region TOR Configuration ---
     * Region 0: [0, 0x1000)
     * Region 1: [0x1000, 0x5000)
     * Region 2: [0x5000, 0x10000)
     */
    pmp_region_cfg_t r0 = {
        .region_idx = 0U,
        .start_addr = 0U,
        .length = TEST_PMP_TOR_REGION_0_LEN,
        .read_allow = 1U,
        .write_allow = 0U,
        .execute_allow = 0U,
        .lock = 0U,
        .addr_mode = (uint8_t)PMP_ADDR_MODE_TOR
    };
    TEST_ASSERT(pmp_set_region(&r0) == PMP_OK, "TOR Region 0 [0, 0x1000) set succeeds");
    TEST_ASSERT(pmp_read_addr(0U) == (TEST_PMP_TOR_REGION_0_LEN >> PMP_ADDR_SHIFT), "pmpaddr0 holds top address (0x1000 >> 2)");

    pmp_region_cfg_t r1 = {
        .region_idx = 1U,
        .start_addr = TEST_PMP_TOR_REGION_1_START,
        .length = TEST_PMP_TOR_REGION_1_LEN,
        .read_allow = 1U,
        .write_allow = 1U,
        .execute_allow = 0U,
        .lock = 0U,
        .addr_mode = (uint8_t)PMP_ADDR_MODE_TOR
    };
    TEST_ASSERT(pmp_set_region(&r1) == PMP_OK, "TOR Region 1 [0x1000, 0x5000) chained set succeeds");
    TEST_ASSERT(pmp_read_addr(1U) == ((TEST_PMP_TOR_REGION_1_START + TEST_PMP_TOR_REGION_1_LEN) >> PMP_ADDR_SHIFT),
                "pmpaddr1 holds top address (0x5000 >> 2)");

    pmp_region_cfg_t r2 = {
        .region_idx = 2U,
        .start_addr = TEST_PMP_TOR_REGION_2_START,
        .length = TEST_PMP_TOR_REGION_2_LEN,
        .read_allow = 1U,
        .write_allow = 1U,
        .execute_allow = 1U,
        .lock = 0U,
        .addr_mode = (uint8_t)PMP_ADDR_MODE_TOR
    };
    TEST_ASSERT(pmp_set_region(&r2) == PMP_OK, "TOR Region 2 [0x5000, 0x10000) chained set succeeds");
    TEST_ASSERT(pmp_read_addr(2U) == ((TEST_PMP_TOR_REGION_2_START + TEST_PMP_TOR_REGION_2_LEN) >> PMP_ADDR_SHIFT),
                "pmpaddr2 holds top address (0x10000 >> 2)");

    /* --- Part 2: Readback & Decode via pmp_get_region --- */
    pmp_region_cfg_t rb0;
    TEST_ASSERT(pmp_get_region(0U, &rb0) == PMP_OK, "pmp_get_region succeeds for Region 0");
    TEST_ASSERT(rb0.start_addr == 0U, "Region 0 readback start_addr is 0");
    TEST_ASSERT(rb0.length == TEST_PMP_TOR_REGION_0_LEN, "Region 0 readback length is 0x1000");
    TEST_ASSERT(rb0.read_allow == 1U && rb0.write_allow == 0U && rb0.execute_allow == 0U, "Region 0 permissions match");
    TEST_ASSERT(rb0.addr_mode == (uint8_t)PMP_ADDR_MODE_TOR, "Region 0 addr_mode is TOR");

    pmp_region_cfg_t rb1;
    TEST_ASSERT(pmp_get_region(1U, &rb1) == PMP_OK, "pmp_get_region succeeds for Region 1");
    TEST_ASSERT(rb1.start_addr == TEST_PMP_TOR_REGION_1_START, "Region 1 readback start_addr is 0x1000");
    TEST_ASSERT(rb1.length == TEST_PMP_TOR_REGION_1_LEN, "Region 1 readback length is 0x4000");
    TEST_ASSERT(rb1.read_allow == 1U && rb1.write_allow == 1U && rb1.execute_allow == 0U, "Region 1 permissions match");
    TEST_ASSERT(rb1.addr_mode == (uint8_t)PMP_ADDR_MODE_TOR, "Region 1 addr_mode is TOR");

    pmp_region_cfg_t rb2;
    TEST_ASSERT(pmp_get_region(2U, &rb2) == PMP_OK, "pmp_get_region succeeds for Region 2");
    TEST_ASSERT(rb2.start_addr == TEST_PMP_TOR_REGION_2_START, "Region 2 readback start_addr is 0x5000");
    TEST_ASSERT(rb2.length == TEST_PMP_TOR_REGION_2_LEN, "Region 2 readback length is 0xB000");
    TEST_ASSERT(rb2.read_allow == 1U && rb2.write_allow == 1U && rb2.execute_allow == 1U, "Region 2 permissions match");
    TEST_ASSERT(rb2.addr_mode == (uint8_t)PMP_ADDR_MODE_TOR, "Region 2 addr_mode is TOR");

    /* --- Part 3: Invalid Non-Contiguous Chained TOR Rejection --- */
    pmp_region_cfg_t r3_invalid = {
        .region_idx = 3U,
        .start_addr = TEST_PMP_TOR_INVALID_START_2, /* 0x6000 != Region 2 top 0x10000 */
        .length = 0x2000U,
        .read_allow = 1U,
        .write_allow = 0U,
        .execute_allow = 0U,
        .lock = 0U,
        .addr_mode = (uint8_t)PMP_ADDR_MODE_TOR
    };
    TEST_ASSERT(pmp_set_region(&r3_invalid) == PMP_ERR_INVALID_ADDR, "Non-contiguous chained TOR Region 3 rejected");

    r3_invalid.start_addr = 0x12000U; /* Non-contiguous gap */
    TEST_ASSERT(pmp_set_region(&r3_invalid) == PMP_ERR_INVALID_ADDR, "Non-contiguous gap TOR Region 3 rejected");

    /* --- Part 4: Zero-Length TOR Region Handling --- */
    pmp_region_cfg_t r3_zero = {
        .region_idx = 3U,
        .start_addr = TEST_PMP_TOR_REGION_3_START, /* exactly matches Region 2 top */
        .length = 0U,                               /* zero length */
        .read_allow = 0U,
        .write_allow = 0U,
        .execute_allow = 0U,
        .lock = 0U,
        .addr_mode = (uint8_t)PMP_ADDR_MODE_TOR
    };
    TEST_ASSERT(pmp_set_region(&r3_zero) == PMP_OK, "Zero-length TOR Region 3 set succeeds");
    TEST_ASSERT(pmp_read_addr(3U) == (TEST_PMP_TOR_REGION_3_START >> PMP_ADDR_SHIFT), "Zero-length pmpaddr3 matches top of region 2");

    pmp_region_cfg_t rb3;
    TEST_ASSERT(pmp_get_region(3U, &rb3) == PMP_OK, "pmp_get_region succeeds for zero-length Region 3");
    TEST_ASSERT(rb3.start_addr == TEST_PMP_TOR_REGION_3_START, "Zero-length readback start_addr matches");
    TEST_ASSERT(rb3.length == 0U, "Zero-length readback length is 0");

    /* Clean up regions 1, 2, 3 */
    TEST_ASSERT(pmp_disable_region(3U) == PMP_OK, "Disable Region 3 succeeds");
    TEST_ASSERT(pmp_disable_region(2U) == PMP_OK, "Disable Region 2 succeeds");
    TEST_ASSERT(pmp_disable_region(1U) == PMP_OK, "Disable Region 1 succeeds");
    TEST_ASSERT(pmp_disable_region(0U) == PMP_OK, "Disable Region 0 succeeds");

    /* --- Part 5: Locked Region Enforcement (PMP_CFG_L_BIT) --- */
    pmp_region_cfg_t lock_cfg = {
        .region_idx = 0U,
        .start_addr = 0x40820000U,
        .length = 65536U,
        .read_allow = 1U,
        .write_allow = 0U,
        .execute_allow = 0U,
        .lock = 1U, /* Lock bit set */
        .addr_mode = (uint8_t)PMP_ADDR_MODE_NAPOT
    };
    TEST_ASSERT(pmp_set_region(&lock_cfg) == PMP_OK, "Locked Region 0 set succeeds");
    uint32_t cfg0 = pmp_read_cfg(0U);
    TEST_ASSERT((cfg0 & PMP_CFG_L_BIT) != 0U, "pmpcfg0 bit 7 (L) is set");

    /* Subsequent pmp_set_region on locked region MUST return PMP_ERR_LOCKED */
    pmp_region_cfg_t modify_cfg = lock_cfg;
    modify_cfg.write_allow = 1U;
    TEST_ASSERT(pmp_set_region(&modify_cfg) == PMP_ERR_LOCKED, "Modifying locked region returns PMP_ERR_LOCKED");

    /* Subsequent pmp_disable_region on locked region MUST return PMP_ERR_LOCKED */
    TEST_ASSERT(pmp_disable_region(0U) == PMP_ERR_LOCKED, "Disabling locked region returns PMP_ERR_LOCKED");

    /* Calling pmp_init() preserves locked regions */
    TEST_ASSERT(pmp_init() == PMP_OK, "pmp_init() executes cleanly");
    cfg0 = pmp_read_cfg(0U);
    TEST_ASSERT((cfg0 & PMP_CFG_L_BIT) != 0U, "pmp_init preserves locked Region 0");

    /* Verify Region 0 readback is still locked and active */
    pmp_region_cfg_t locked_rb;
    TEST_ASSERT(pmp_get_region(0U, &locked_rb) == PMP_OK, "pmp_get_region succeeds on locked region");
    TEST_ASSERT(locked_rb.lock == 1U, "Readback confirms lock is active");
    TEST_ASSERT(locked_rb.start_addr == 0x40820000U, "Locked region start_addr preserved");
    TEST_ASSERT(locked_rb.length == 65536U, "Locked region length preserved");

    /* Clear mock hardware state for subsequent test runs */
    pmp_write_cfg(0U, 0U);
    pmp_write_addr(0U, 0U);
    pmp_init();
    TEST_ASSERT((pmp_read_cfg(0U) & 0xFFU) == 0U, "Mock reset cleared locked state for following tests");
}

#define TEST_APM_REGION_DYNAMIC_1           1U
#define TEST_APM_REGION_DYNAMIC_5           5U
#define TEST_APM_REGION_DYNAMIC_10          10U
#define TEST_APM_REGION_DYNAMIC_15          15U
#define TEST_APM_INVALID_REGION_16          16U
#define TEST_APM_INVALID_MASTER_4           4U

#define TEST_APM_DYNAMIC_START_1            0x40820000U
#define TEST_APM_DYNAMIC_END_1              0x40840000U
#define TEST_APM_UPDATED_START_1            0x40828000U
#define TEST_APM_UPDATED_END_1              0x40838000U
#define TEST_APM_INVERTED_START             0x40850000U
#define TEST_APM_INVERTED_END               0x40820000U

/*
 * Test 18: HP_APM Dynamic Filter Reconfigurations & Exception Management
 * Tests dynamic APM updates across regions 1..15, reserved Region 0 handling,
 * inverted boundary rejection, master enable/disable, and query of exception registers.
 */
static void test_apm_dynamic_reconfiguration(void)
{
    printf("  [TEST] hp_apm dynamic filter reconfigurations & exception status...\n");

    TEST_ASSERT(apm_init() == APM_OK, "apm_init succeeds");

    /* --- Part 1: Reserved Region 0 Enforcement --- */
    TEST_ASSERT(apm_disable_region(0U) == APM_ERR_RESERVED_REGION, "apm_disable_region rejects Region 0");
    apm_region_cfg_t r0_dis = {
        .region_idx = 0U,
        .start_addr = 0U,
        .end_addr = 0xFFFFFFFFU,
        .filter_enable = 0U
    };
    TEST_ASSERT(apm_set_region(&r0_dis) == APM_ERR_RESERVED_REGION, "apm_set_region rejects disabling Region 0");

    /* --- Part 2: Inverted Boundary Rejection --- */
    apm_region_cfg_t inv_cfg = {
        .region_idx = TEST_APM_REGION_DYNAMIC_1,
        .start_addr = TEST_APM_INVERTED_START,
        .end_addr = TEST_APM_INVERTED_END,
        .read_allow = 1U,
        .write_allow = 1U,
        .execute_allow = 0U,
        .filter_enable = 1U
    };
    TEST_ASSERT(apm_set_region(&inv_cfg) == APM_ERR_INVALID_ADDR, "Inverted boundaries on Region 1 rejected");

    inv_cfg.region_idx = TEST_APM_REGION_DYNAMIC_15;
    inv_cfg.start_addr = 0x50001000U;
    inv_cfg.end_addr = 0x50000000U;
    TEST_ASSERT(apm_set_region(&inv_cfg) == APM_ERR_INVALID_ADDR, "Inverted boundaries on Region 15 rejected");

    /* --- Part 3: Dynamic Region Updates Across Regions 1..15 --- */
    /* Initial configuration of Region 1: Read/Write */
    apm_region_cfg_t apm_cfg1 = {
        .region_idx = TEST_APM_REGION_DYNAMIC_1,
        .start_addr = TEST_APM_DYNAMIC_START_1,
        .end_addr = TEST_APM_DYNAMIC_END_1,
        .read_allow = 1U,
        .write_allow = 1U,
        .execute_allow = 0U,
        .filter_enable = 1U
    };
    TEST_ASSERT(apm_set_region(&apm_cfg1) == APM_OK, "Set Region 1 [0x40820000, 0x40840000] RW succeeds");

    apm_region_cfg_t rb1;
    TEST_ASSERT(apm_get_region(TEST_APM_REGION_DYNAMIC_1, &rb1) == APM_OK, "Get Region 1 succeeds");
    TEST_ASSERT(rb1.start_addr == TEST_APM_DYNAMIC_START_1, "Region 1 start address matches");
    TEST_ASSERT(rb1.end_addr == TEST_APM_DYNAMIC_END_1, "Region 1 end address matches");
    TEST_ASSERT(rb1.read_allow == 1U && rb1.write_allow == 1U && rb1.execute_allow == 0U, "Region 1 permissions are RW");
    TEST_ASSERT(rb1.filter_enable == 1U, "Region 1 filter is enabled");

    /* Dynamic reconfiguration of Region 1: update boundaries and restrict to Read-Only */
    apm_cfg1.start_addr = TEST_APM_UPDATED_START_1;
    apm_cfg1.end_addr = TEST_APM_UPDATED_END_1;
    apm_cfg1.write_allow = 0U; /* Read-Only */
    TEST_ASSERT(apm_set_region(&apm_cfg1) == APM_OK, "Dynamic reconfiguration of Region 1 to Read-Only succeeds");

    TEST_ASSERT(apm_get_region(TEST_APM_REGION_DYNAMIC_1, &rb1) == APM_OK, "Get reconfigured Region 1 succeeds");
    TEST_ASSERT(rb1.start_addr == TEST_APM_UPDATED_START_1, "Updated Region 1 start address matches");
    TEST_ASSERT(rb1.end_addr == TEST_APM_UPDATED_END_1, "Updated Region 1 end address matches");
    TEST_ASSERT(rb1.read_allow == 1U && rb1.write_allow == 0U && rb1.execute_allow == 0U, "Updated permissions are RO");
    TEST_ASSERT(rb1.filter_enable == 1U, "Updated Region 1 filter is enabled");

    /* Configure Region 5 (LP SRAM: 0x50000000 - 0x50003FFF) */
    apm_region_cfg_t apm_cfg5 = {
        .region_idx = TEST_APM_REGION_DYNAMIC_5,
        .start_addr = 0x50000000U,
        .end_addr = 0x50003FFFU,
        .read_allow = 1U,
        .write_allow = 1U,
        .execute_allow = 0U,
        .filter_enable = 1U
    };
    TEST_ASSERT(apm_set_region(&apm_cfg5) == APM_OK, "Set Region 5 LP SRAM succeeds");
    apm_region_cfg_t rb5;
    TEST_ASSERT(apm_get_region(TEST_APM_REGION_DYNAMIC_5, &rb5) == APM_OK, "Get Region 5 succeeds");
    TEST_ASSERT(rb5.start_addr == 0x50000000U && rb5.end_addr == 0x50003FFFU, "Region 5 boundaries match");
    TEST_ASSERT(rb5.filter_enable == 1U, "Region 5 filter is enabled");

    /* Configure Region 10 (UART0 MMIO: 0x60000000 - 0x60000FFF) */
    apm_region_cfg_t apm_cfg10 = {
        .region_idx = TEST_APM_REGION_DYNAMIC_10,
        .start_addr = 0x60000000U,
        .end_addr = 0x60000FFFU,
        .read_allow = 1U,
        .write_allow = 1U,
        .execute_allow = 0U,
        .filter_enable = 1U
    };
    TEST_ASSERT(apm_set_region(&apm_cfg10) == APM_OK, "Set Region 10 UART0 succeeds");
    apm_region_cfg_t rb10;
    TEST_ASSERT(apm_get_region(TEST_APM_REGION_DYNAMIC_10, &rb10) == APM_OK, "Get Region 10 succeeds");
    TEST_ASSERT(rb10.start_addr == 0x60000000U && rb10.end_addr == 0x60000FFFU, "Region 10 boundaries match");
    TEST_ASSERT(rb10.filter_enable == 1U, "Region 10 filter is enabled");

    /* Configure Region 15 (Flash XIP: 0x42000000 - 0x427FFFFF) */
    apm_region_cfg_t apm_cfg15 = {
        .region_idx = TEST_APM_REGION_DYNAMIC_15,
        .start_addr = 0x42000000U,
        .end_addr = 0x427FFFFFU,
        .read_allow = 1U,
        .write_allow = 0U,
        .execute_allow = 1U,
        .filter_enable = 1U
    };
    TEST_ASSERT(apm_set_region(&apm_cfg15) == APM_OK, "Set Region 15 Flash XIP succeeds");
    apm_region_cfg_t rb15;
    TEST_ASSERT(apm_get_region(TEST_APM_REGION_DYNAMIC_15, &rb15) == APM_OK, "Get Region 15 succeeds");
    TEST_ASSERT(rb15.start_addr == 0x42000000U && rb15.end_addr == 0x427FFFFFU, "Region 15 boundaries match");
    TEST_ASSERT(rb15.filter_enable == 1U, "Region 15 filter is enabled");

    /* Invalid region bounds and NULL pointer rejection */
    apm_cfg15.region_idx = TEST_APM_INVALID_REGION_16;
    TEST_ASSERT(apm_set_region(&apm_cfg15) == APM_ERR_INVALID_REGION, "apm_set_region rejects region_idx >= 16");
    TEST_ASSERT(apm_get_region(TEST_APM_INVALID_REGION_16, &rb15) == APM_ERR_INVALID_REGION, "apm_get_region rejects region_idx >= 16");
    TEST_ASSERT(apm_disable_region(TEST_APM_INVALID_REGION_16) == APM_ERR_INVALID_REGION, "apm_disable_region rejects region_idx >= 16");
    TEST_ASSERT(apm_set_region(NULL) == APM_ERR_NULL_PTR, "apm_set_region rejects NULL config");
    TEST_ASSERT(apm_get_region(TEST_APM_REGION_DYNAMIC_1, NULL) == APM_ERR_NULL_PTR, "apm_get_region rejects NULL config");

    /* Disable dynamic regions */
    TEST_ASSERT(apm_disable_region(TEST_APM_REGION_DYNAMIC_1) == APM_OK, "Disable Region 1 succeeds");
    TEST_ASSERT(apm_disable_region(TEST_APM_REGION_DYNAMIC_5) == APM_OK, "Disable Region 5 succeeds");
    TEST_ASSERT(apm_disable_region(TEST_APM_REGION_DYNAMIC_10) == APM_OK, "Disable Region 10 succeeds");
    TEST_ASSERT(apm_disable_region(TEST_APM_REGION_DYNAMIC_15) == APM_OK, "Disable Region 15 succeeds");

    /* --- Part 4: Master Filter Enable / Disable & Exception Query / Clear --- */
    for (uint32_t m = 0U; m < APM_MAX_MASTERS; m++)
    {
        TEST_ASSERT(apm_enable_master(m, 1) == APM_OK, "apm_enable_master enable succeeds");
        TEST_ASSERT(apm_enable_master(m, 0) == APM_OK, "apm_enable_master disable succeeds");
    }

    /* Invalid master rejection */
    TEST_ASSERT(apm_enable_master(TEST_APM_INVALID_MASTER_4, 1) == APM_ERR_INVALID_MASTER, "Enable master >= 4 rejected");
    TEST_ASSERT(apm_enable_master(99U, 0) == APM_ERR_INVALID_MASTER, "Disable master 99 rejected");
    apm_exception_info_t exc_info;
    TEST_ASSERT(apm_get_exception_info(TEST_APM_INVALID_MASTER_4, &exc_info) == APM_ERR_INVALID_MASTER, "Query master >= 4 rejected");
    TEST_ASSERT(apm_get_exception_info(0U, NULL) == APM_ERR_NULL_PTR, "Query exception info with NULL pointer rejected");
    TEST_ASSERT(apm_clear_exception(TEST_APM_INVALID_MASTER_4) == APM_ERR_INVALID_MASTER, "Clear master >= 4 rejected");

    /* Query baseline exception status for Master 0 and clear it */
    TEST_ASSERT(apm_get_exception_info(0U, &exc_info) == APM_OK, "apm_get_exception_info for Master 0 succeeds");
    TEST_ASSERT(exc_info.exception_status == 0U, "Initial exception status is 0");
    TEST_ASSERT(apm_clear_exception(0U) == APM_OK, "apm_clear_exception for Master 0 succeeds");
    TEST_ASSERT(apm_clear_exception(1U) == APM_OK, "apm_clear_exception for Master 1 succeeds");
    TEST_ASSERT(apm_clear_exception(2U) == APM_OK, "apm_clear_exception for Master 2 succeeds");
    TEST_ASSERT(apm_clear_exception(3U) == APM_OK, "apm_clear_exception for Master 3 succeeds");
}

#ifndef ASCII_BS
#define ASCII_BS               0x08
#endif
#ifndef ASCII_DEL
#define ASCII_DEL              0x7F
#endif
#ifndef ASCII_PRINTABLE_MIN
#define ASCII_PRINTABLE_MIN    ' '
#endif
#ifndef ASCII_PRINTABLE_MAX
#define ASCII_PRINTABLE_MAX    '~'
#endif

#define TEST_CONSOLE_MAX_LINE_LEN           128U
#define TEST_CONSOLE_SMALL_BUF_SIZE         8U
#define TEST_CONSOLE_OVERFLOW_FEED_COUNT    150U

typedef struct {
    const char *data;
    size_t len;
    size_t pos;
} test_console_stream_t;

static int mock_console_stream_getc(test_console_stream_t *stream, char *out_c)
{
    if (!stream || !out_c || stream->pos >= stream->len)
    {
        return 0;
    }
    *out_c = stream->data[stream->pos++];
    return 1;
}

typedef struct {
    char line_buf[TEST_CONSOLE_MAX_LINE_LEN];
    size_t line_idx;
    uint8_t prev_was_cr;
} test_console_reader_t;

static void test_console_reader_init(test_console_reader_t *r)
{
    if (!r) return;
    r->line_idx = 0U;
    r->prev_was_cr = 0U;
    r->line_buf[0] = '\0';
}

/*
 * Line reader state machine matching src/console.c:225-290
 */
static int test_console_read_line(test_console_reader_t *r,
                                  test_console_stream_t *stream,
                                  char *out_buffer,
                                  size_t max_len)
{
    if (!r || !stream || !out_buffer || max_len == 0U) return 0;

    char c = '\0';
    while (mock_console_stream_getc(stream, &c))
    {
        /* Drop isolated '\n' immediately following '\r' to prevent duplicate blank prompts on CRLF */
        if (r->prev_was_cr && c == '\n')
        {
            r->prev_was_cr = 0U;
            continue;
        }
        r->prev_was_cr = 0U;

        if (c == '\r' || c == '\n')
        {
            if (c == '\r')
            {
                r->prev_was_cr = 1U;
            }

            r->line_buf[r->line_idx] = '\0';

            size_t copy_len = (r->line_idx < max_len - 1U) ? r->line_idx : (max_len - 1U);
            for (size_t i = 0; i < copy_len; i++)
            {
                out_buffer[i] = r->line_buf[i];
            }
            out_buffer[copy_len] = '\0';

            r->line_idx = 0U;
            return 1;
        }
        else if (c == ASCII_BS || c == ASCII_DEL)
        {
            if (r->line_idx > 0U)
            {
                r->line_idx--;
            }
        }
        else if (c >= ASCII_PRINTABLE_MIN && c <= ASCII_PRINTABLE_MAX)
        {
            if (r->line_idx < TEST_CONSOLE_MAX_LINE_LEN - 1U)
            {
                r->line_buf[r->line_idx++] = c;
            }
        }
    }

    return 0;
}

/*
 * Test 19: Console Line Reader Edge Cases
 * Tests non-blocking line reading with CRLF (\r\n), standalone \n, standalone \r,
 * backspace (\b) and delete (\x7f) character erasure, and buffer overflow limit truncation.
 */
static void test_console_line_reader_edge_cases(void)
{
    printf("  [TEST] console line reader edge cases (crlf, backspace, overflow)...\n");

    test_console_reader_t reader;
    char out[TEST_CONSOLE_MAX_LINE_LEN];

    /* --- Part 1: Standalone \n --- */
    test_console_reader_init(&reader);
    const char input_nl[] = "status\n";
    test_console_stream_t stream_nl = { .data = input_nl, .len = s_strlen(input_nl), .pos = 0U };
    int res = test_console_read_line(&reader, &stream_nl, out, sizeof(out));
    TEST_ASSERT(res == 1, "Standalone newline completes line reading");
    TEST_ASSERT(s_strcmp(out, "status") == 0, "Line content is 'status'");

    /* --- Part 2: CRLF (\r\n) Line Terminator & Consecutive Command Parsing --- */
    test_console_reader_init(&reader);
    const char input_crlf[] = "uptime\r\nhelp\r\n";
    test_console_stream_t stream_crlf = { .data = input_crlf, .len = s_strlen(input_crlf), .pos = 0U };

    /* First command: uptime\r\n */
    res = test_console_read_line(&reader, &stream_crlf, out, sizeof(out));
    TEST_ASSERT(res == 1, "First CRLF command line read succeeds");
    TEST_ASSERT(s_strcmp(out, "uptime") == 0, "First line content is 'uptime'");

    /* Second command: help\r\n (verify \n was dropped and did not produce phantom empty line) */
    res = test_console_read_line(&reader, &stream_crlf, out, sizeof(out));
    TEST_ASSERT(res == 1, "Second CRLF command line read succeeds without phantom blank line");
    TEST_ASSERT(s_strcmp(out, "help") == 0, "Second line content is 'help'");

    /* Subsequent read with depleted stream returns 0 */
    res = test_console_read_line(&reader, &stream_crlf, out, sizeof(out));
    TEST_ASSERT(res == 0, "Depleted stream returns 0");

    /* --- Part 3: Standalone \r Terminator --- */
    test_console_reader_init(&reader);
    const char input_cr[] = "tasks\r";
    test_console_stream_t stream_cr = { .data = input_cr, .len = s_strlen(input_cr), .pos = 0U };
    res = test_console_read_line(&reader, &stream_cr, out, sizeof(out));
    TEST_ASSERT(res == 1, "Standalone carriage return completes line reading");
    TEST_ASSERT(s_strcmp(out, "tasks") == 0, "Line content is 'tasks'");

    /* --- Part 4: Backspace (\b = 0x08) Character Erasure --- */
    test_console_reader_init(&reader);
    const char input_bs[] = "helpp\b\bo\n";
    test_console_stream_t stream_bs = { .data = input_bs, .len = s_strlen(input_bs), .pos = 0U };
    res = test_console_read_line(&reader, &stream_bs, out, sizeof(out));
    TEST_ASSERT(res == 1, "Line with backspaces completed");
    TEST_ASSERT(s_strcmp(out, "helo") == 0, "Backspace erased two characters ('helpp' -> 'helo')");

    /* Backspace underflow guard (more backspaces than characters) */
    test_console_reader_init(&reader);
    const char input_bs_under[] = "\b\b\b\babc\n";
    test_console_stream_t stream_bs_under = { .data = input_bs_under, .len = s_strlen(input_bs_under), .pos = 0U };
    res = test_console_read_line(&reader, &stream_bs_under, out, sizeof(out));
    TEST_ASSERT(res == 1, "Line with excess leading backspaces completed");
    TEST_ASSERT(s_strcmp(out, "abc") == 0, "Excess backspaces safely clamped to 0");

    /* --- Part 5: Delete (\x7f = 0x7F) Character Erasure --- */
    test_console_reader_init(&reader);
    const char input_del[] = "iron\x7f\x7f_v\n";
    test_console_stream_t stream_del = { .data = input_del, .len = s_strlen(input_del), .pos = 0U };
    res = test_console_read_line(&reader, &stream_del, out, sizeof(out));
    TEST_ASSERT(res == 1, "Line with DEL characters completed");
    TEST_ASSERT(s_strcmp(out, "ir_v") == 0, "DEL erased characters ('iron' -> 'ir' + '_v' -> 'ir_v')");

    /* Combined backspace and delete */
    test_console_reader_init(&reader);
    const char input_mixed[] = "abcde\b\x7f" "f\n";
    test_console_stream_t stream_mixed = { .data = input_mixed, .len = s_strlen(input_mixed), .pos = 0U };
    res = test_console_read_line(&reader, &stream_mixed, out, sizeof(out));
    TEST_ASSERT(res == 1, "Line with mixed BS and DEL completed");
    TEST_ASSERT(s_strcmp(out, "abcf") == 0, "Mixed BS and DEL erased 2 characters ('abcde' -> 'abc' + 'f' -> 'abcf')");

    /* --- Part 6: Buffer Overflow Limit Truncation (128 bytes max) --- */
    test_console_reader_init(&reader);
    char overflow_input[TEST_CONSOLE_OVERFLOW_FEED_COUNT + 2U];
    for (size_t i = 0U; i < TEST_CONSOLE_OVERFLOW_FEED_COUNT; i++)
    {
        overflow_input[i] = 'X';
    }
    overflow_input[TEST_CONSOLE_OVERFLOW_FEED_COUNT] = '\n';
    overflow_input[TEST_CONSOLE_OVERFLOW_FEED_COUNT + 1U] = '\0';

    test_console_stream_t stream_overflow = {
        .data = overflow_input,
        .len = TEST_CONSOLE_OVERFLOW_FEED_COUNT + 1U,
        .pos = 0U
    };
    res = test_console_read_line(&reader, &stream_overflow, out, sizeof(out));
    TEST_ASSERT(res == 1, "Overflow line read completes on newline");
    TEST_ASSERT(s_strlen(out) == (TEST_CONSOLE_MAX_LINE_LEN - 1U), "Line length capped at CONSOLE_MAX_LINE_LEN - 1 (127 bytes)");
    TEST_ASSERT(out[0] == 'X' && out[126] == 'X' && out[127] == '\0', "Buffer content verified and null-terminated");

    /* --- Part 7: Destination Buffer Truncation (small max_len) --- */
    test_console_reader_init(&reader);
    const char input_dest_trunc[] = "command123456\n";
    test_console_stream_t stream_dest = { .data = input_dest_trunc, .len = s_strlen(input_dest_trunc), .pos = 0U };
    char small_out[TEST_CONSOLE_SMALL_BUF_SIZE]; /* 8 bytes max */
    res = test_console_read_line(&reader, &stream_dest, small_out, sizeof(small_out));
    TEST_ASSERT(res == 1, "Destination truncation read completes");
    TEST_ASSERT(s_strlen(small_out) == (TEST_CONSOLE_SMALL_BUF_SIZE - 1U), "Output truncated to max_len - 1 (7 bytes)");
    TEST_ASSERT(s_strncmp(small_out, "command", 7) == 0, "Output contains first 7 characters ('command')");

    /* --- Part 8: Null and Boundary Input Validation --- */
    TEST_ASSERT(test_console_read_line(NULL, &stream_dest, out, sizeof(out)) == 0, "NULL reader returns 0");
    TEST_ASSERT(test_console_read_line(&reader, NULL, out, sizeof(out)) == 0, "NULL stream returns 0");
    TEST_ASSERT(test_console_read_line(&reader, &stream_dest, NULL, sizeof(out)) == 0, "NULL out_buffer returns 0");
    TEST_ASSERT(test_console_read_line(&reader, &stream_dest, out, 0U) == 0, "max_len == 0 returns 0");

    /* Empty line "\n" returns 1 with empty string */
    test_console_reader_init(&reader);
    const char input_empty[] = "\n";
    test_console_stream_t stream_empty = { .data = input_empty, .len = 1U, .pos = 0U };
    res = test_console_read_line(&reader, &stream_empty, out, sizeof(out));
    TEST_ASSERT(res == 1, "Empty line completes");
    TEST_ASSERT(out[0] == '\0', "Empty line output is empty string");
}

int main(void)
{
    printf("======================================================================\n");
    printf("        IRON V FREESTANDING RUNTIME UNIT TEST SUITE (HOST GCC)       \n");
    printf("======================================================================\n");

    test_s_strlen();
    test_s_strcmp();
    test_s_strncmp();
    test_s_htoi();
    test_s_itoa();
    test_s_hextoa();
    test_memory_utils();
    test_char_helpers();
    test_dpc_queue();
    test_console_multiplexer();
    test_arena_allocator();
    test_systimer_timebase();
    test_task_structures();
    test_pmp_apm_isolation();
    test_lp_core_driver();
    test_power_mailbox_subsystem();
    test_gpio_subsystem();
    test_gdma_subsystem();
    test_modem_subsystem();
    test_ble_gatt_subsystem();
    test_wifi_mac_subsystem();
    test_ieee802154_subsystem();
    test_tcpip_subsystem();

    /* Hardened cross-module integration and edge case tests */
    test_coroutine_systimer_dpc_integration();
    test_arena_concurrency_exhaustion();
    test_pmp_chained_tor_and_locked_regions();
    test_apm_dynamic_reconfiguration();
    test_console_line_reader_edge_cases();

    printf("======================================================================\n");
    if (g_assert_failures == 0)
    {
        printf("  Result: ALL FREESTANDING C UNIT TESTS PASSED (0 FAILURES)\n");
        printf("======================================================================\n");
        return 0;
    }
    else
    {
        printf("  Result: FAILED WITH %d ASSERTION VIOLATIONS\n", g_assert_failures);
        printf("======================================================================\n");
        return 1;
    }
}

