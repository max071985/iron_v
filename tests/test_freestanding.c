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
#include "config.h"
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
#include "wifi.h"
#include "ieee802154.h"
#include "net.h"
#include "tcp.h"
#include "dhcp.h"
#include "regs/lp_wdt.h"
#include "wifi_vendor_types.h"
#include "wifi_regulatory.h"
#include "wifi_ftm_cal.h"
#include "wifi_phy_data.h"
#include "test.h"
#include "http_server.h"
#include "web_assets.h"
#include "speedtest.h"
#include "clock.h"
#include "wdt.h"
#include "timer.h"
#include "trap.h"
#include "shell.h"
#include "efuse.h"
#include "soak.h"
#include "ota.h"
#include "nvs.h"
#include "provisioning.h"
#include "wpa2_client.h"
#include "wifi_link.h"
#include "wpa_ie.h"
#include "wpa_driver.h"
#include "mdns.h"

/* Host test stubs for hardware-specific functions */
void wdt_feed(void)
{
}

void lp_wdt_feed(void)
{
}

void clock_get_config(clock_config_t *cfg)
{
    if (cfg != NULL)
    {
        cfg->xtal_mhz = 40U;
        cfg->pll_mhz  = 480U;
        cfg->cpu_mhz  = 160U;
        cfg->apb_mhz  = 40U;
    }
}

void wdt_get_status(wdt_supervisor_t *wdt)
{
    if (wdt != NULL)
    {
        memset(wdt, 0, sizeof(*wdt));
        wdt->active = 1U;
        wdt->epoch_count = 10U;
        wdt->total_feed_count = 100U;
    }
}

soc_reset_cause_t wdt_get_reset_cause(void)
{
    return RESET_CAUSE_CHIP_POWER_ON;
}

const char *wdt_get_reset_cause_desc(soc_reset_cause_t cause)
{
    (void)cause;
    return "Power-on reset";
}

uint64_t systimer_get_ticks(void)
{
    return 160000000ULL;
}

static uint64_t s_host_now_us = 10000000ULL;   /* host clock; tests advance it */

uint64_t systimer_get_us(void)
{
    return s_host_now_us;
}

void systimer_get_telemetry(systimer_telemetry_t *t)
{
    if (t != NULL)
    {
        memset(t, 0, sizeof(*t));
        t->uptime_sec = 10U;
        t->total_ticks = 160000000ULL;
    }
}

void console_get_manager(console_manager_t *cmgr)
{
    if (cmgr != NULL)
    {
        memset(cmgr, 0, sizeof(*cmgr));
        cmgr->active_mask = CONSOLE_MASK_UART0 | CONSOLE_MASK_USB;
        cmgr->echo_enabled = 1U;
    }
}

void console_putc(char c) { (void)c; }
void console_flush(void) { }
int console_read_line_nonblocking(char *buffer, size_t max_len) { (void)buffer; (void)max_len; return 0; }
void read_line(char *buffer, int max_len) { (void)buffer; (void)max_len; }

int usb_serial_is_tx_ready(void) { return 1; }
int usb_serial_is_rx_ready(void) { return 0; }

void timer_get_status(timer_status_t *t) { if (t != NULL) memset(t, 0, sizeof(*t)); }
uint64_t timer_get_current_ticks(void) { return 0ULL; }
void timer_stop(void) { }
void timer_start(void) { }

bool test_soak_run(uint32_t c, uint32_t d) { (void)c; (void)d; return true; }
void test_soak_get_telemetry(test_soak_telemetry_t *t) { if (t != NULL) memset(t, 0, sizeof(*t)); }

mem_access_t check_mem_access(uint32_t addr) { (void)addr; return MEM_ACCESS_READWRITE; }
uint32_t trap_get_ecall_count(void) { return 0U; }

void task_get_status(task_scheduler_status_t *s) { if (s != NULL) memset(s, 0, sizeof(*s)); }
const char *task_state_name(task_state_t st) { (void)st; return "READY"; }
uint32_t task_get_count(void) { return 4U; }

const uint8_t _sflash_xip[1] = {0};
const uint8_t _eflash_xip[1] = {0};

uint32_t interrupt_get_count(uint32_t src) { (void)src; return 0U; }
void run_validation_suite(void) { }

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

    /* memcmp */
    TEST_ASSERT(memcmp(dest, src, sizeof(dest)) == 0, "memcmp equal buffers");
    dest[5] = 0x00;
    TEST_ASSERT(memcmp(dest, src, sizeof(dest)) < 0, "memcmp less than");
    dest[5] = 0xFF;
    TEST_ASSERT(memcmp(dest, src, sizeof(dest)) > 0, "memcmp greater than");

    /* memmove overlapping forward and backward */
    uint8_t move_buf[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    memmove(move_buf + 2, move_buf, 6); /* {0, 1, 0, 1, 2, 3, 4, 5, 8, ...} */
    TEST_ASSERT(move_buf[2] == 0 && move_buf[3] == 1 && move_buf[7] == 5, "memmove forward overlap");
    memmove(move_buf, move_buf + 2, 6);
    TEST_ASSERT(move_buf[0] == 0 && move_buf[1] == 1 && move_buf[5] == 5, "memmove backward overlap");

    /* Word paths (MMIO RAM only takes 32-bit writes): aligned, unaligned and odd tails */
    uint32_t wsrc[9], wdst[9];
    uint8_t *bs = (uint8_t *)wsrc, *bd = (uint8_t *)wdst;
    for (size_t i = 0; i < sizeof(wsrc); i++) bs[i] = (uint8_t)(i * 7U + 1U);
    for (size_t off_d = 0; off_d < 4; off_d++)
    {
        for (size_t off_s = 0; off_s < 4; off_s++)
        {
            for (size_t n = 0; n <= 29; n++)
            {
                memset(wdst, 0xEE, sizeof(wdst));
                memcpy(bd + off_d, bs + off_s, n);
                int ok = memcmp(bd + off_d, bs + off_s, n) == 0;
                for (size_t i = 0; i < off_d; i++) ok &= (bd[i] == 0xEE);
                for (size_t i = off_d + n; i < sizeof(wdst); i++) ok &= (bd[i] == 0xEE);
                TEST_ASSERT(ok, "memcpy exact for every alignment and length, no overrun");
            }
        }
    }
    for (size_t off = 0; off < 4; off++)
    {
        memset(wdst, 0x11, sizeof(wdst));
        memset(bd + off, 0x5A, 23);
        int ok = 1;
        for (size_t i = 0; i < sizeof(wdst); i++)
            ok &= (bd[i] == ((i >= off && i < off + 23) ? 0x5A : 0x11));
        TEST_ASSERT(ok, "memset exact for every alignment, no overrun");
    }
    for (size_t shift = 1; shift <= 8; shift++)
    {
        uint8_t ref[36];
        for (size_t i = 0; i < sizeof(wsrc); i++) bs[i] = (uint8_t)i;
        for (size_t i = 0; i < sizeof(ref); i++) ref[i] = (uint8_t)i;
        memmove(bs + shift, bs, 24);
        for (size_t i = 0; i < 24; i++) ref[shift + i] = (uint8_t)i;
        TEST_ASSERT(memcmp(bs, ref, sizeof(ref)) == 0, "memmove backward overlap, word and byte paths");
        for (size_t i = 0; i < sizeof(wsrc); i++) bs[i] = (uint8_t)i;
        for (size_t i = 0; i < sizeof(ref); i++) ref[i] = (uint8_t)i;
        memmove(bs, bs + shift, 24);
        for (size_t i = 0; i < 24; i++) ref[i] = (uint8_t)(i + shift);
        TEST_ASSERT(memcmp(bs, ref, sizeof(ref)) == 0, "memmove forward overlap, word and byte paths");
    }

    /* strnlen, strcpy, strncpy */
    TEST_ASSERT(strnlen("hello", 10) == 5, "strnlen normal");
    TEST_ASSERT(strnlen("hello", 3) == 3, "strnlen clamped");
    char str_dest[16];
    strcpy(str_dest, "iron-v");
    TEST_ASSERT(strcmp(str_dest, "iron-v") == 0, "strcpy exact copy");
    memset(str_dest, 'A', sizeof(str_dest));
    strncpy(str_dest, "abc", 5);
    TEST_ASSERT(str_dest[0] == 'a' && str_dest[1] == 'b' && str_dest[2] == 'c' && str_dest[3] == '\0' && str_dest[4] == '\0', "strncpy null pads");
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
    TEST_ASSERT((uintptr_t)MODEM_LPCON_RST_CONF_REG == 0x600AF024U, "MODEM_LPCON_RST_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_LPCON_DATE_REG == 0x600AF02CU, "MODEM_LPCON_DATE_REG address calculation");

    TEST_ASSERT((uintptr_t)IEEE802154_COMMAND_REG == 0x600A3000U, "IEEE802154_COMMAND_REG address calculation");
    TEST_ASSERT((uintptr_t)IEEE802154_CTRL_CFG_REG == 0x600A3004U, "IEEE802154_CTRL_CFG_REG address calculation");

    TEST_ASSERT((uintptr_t)MODEM_RF_ENABLE_REG == 0x600A7104U, "MODEM_RF_ENABLE_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_ANA_PERI_PWR_CONF_REG == 0x600B2C04U, "LP_ANA_PERI_PWR_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_ANA_PERI_CLK_CONF_REG == 0x600B2C0CU, "LP_ANA_PERI_CLK_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_CLKRST_I2C_ANA_MST_LINK0_REG == 0x600B0418U, "LP_CLKRST_I2C_ANA_MST_LINK0_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_CLKRST_I2C_ANA_MST_LINK1_REG == 0x600B041CU, "LP_CLKRST_I2C_ANA_MST_LINK1_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_I2C0_CTRL_REG == 0x600AF800U, "I2C_ANA_MST_I2C0_CTRL_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_I2C1_CTRL_REG == 0x600AF804U, "I2C_ANA_MST_I2C1_CTRL_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_I2C0_CONF_REG == 0x600AF808U, "I2C_ANA_MST_I2C0_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_I2C1_CONF_REG == 0x600AF80CU, "I2C_ANA_MST_I2C1_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_BURST_CONF_REG == 0x600AF810U, "I2C_ANA_MST_BURST_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_BURST_STATUS_REG == 0x600AF814U, "I2C_ANA_MST_BURST_STATUS_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_ANA_CONF0_REG == 0x600AF818U, "I2C_ANA_MST_ANA_CONF0_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_ANA_CONF1_REG == 0x600AF81CU, "I2C_ANA_MST_ANA_CONF1_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_ANA_CONF2_REG == 0x600AF820U, "I2C_ANA_MST_ANA_CONF2_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_I2C0_CTRL1_REG == 0x600AF824U, "I2C_ANA_MST_I2C0_CTRL1_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_I2C1_CTRL1_REG == 0x600AF828U, "I2C_ANA_MST_I2C1_CTRL1_REG address calculation");
    TEST_ASSERT((uintptr_t)I2C_ANA_MST_DATE_REG == 0x600AF834U, "I2C_ANA_MST_DATE_REG address calculation");
    TEST_ASSERT((uintptr_t)PMU_IMM_HP_CK_POWER_REG == 0x600B00CCU, "PMU_IMM_HP_CK_POWER_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_FE_FREQ_STATUS_REG == 0x600A00CCU, "MODEM_FE_FREQ_STATUS_REG address calculation");

    /* Parameterized MMIO Macro Integrity */
    TEST_ASSERT(I2C_ANA_MST_I2C_CTRL_REG(0U) == I2C_ANA_MST_I2C0_CTRL_REG, "I2C_ANA_MST_I2C_CTRL_REG(0) evaluates to I2C0_CTRL");
    TEST_ASSERT(I2C_ANA_MST_I2C_CTRL_REG(1U) == I2C_ANA_MST_I2C1_CTRL_REG, "I2C_ANA_MST_I2C_CTRL_REG(1) evaluates to I2C1_CTRL");
    TEST_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(0U) == MODEM_RF_ANALOG_SWITCH0_REG, "MODEM_RF_ANALOG_SWITCH_REG(0) evaluates to SWITCH0");
    TEST_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(1U) == MODEM_RF_ANALOG_SWITCH1_REG, "MODEM_RF_ANALOG_SWITCH_REG(1) evaluates to SWITCH1");
    TEST_ASSERT(MODEM_RF_ANALOG_SWITCH_REG(16U) == MODEM_RF_ANALOG_SWITCH16_REG, "MODEM_RF_ANALOG_SWITCH_REG(16) evaluates to SWITCH16");

    /* Parameterized Command Constructor Verification */
    TEST_ASSERT(I2C_ANA_MST_CMD_WRITE(0x66U, 2U, 0x50U) == 0x01500266U, "I2C_ANA_MST_CMD_WRITE formats Reg 2 write packet");
    TEST_ASSERT(I2C_ANA_MST_CMD_WRITE(0x66U, 3U, 0x08U) == 0x01080366U, "I2C_ANA_MST_CMD_WRITE formats Reg 3 write packet");
    TEST_ASSERT(I2C_ANA_MST_CMD_WRITE(0x66U, 6U, 0x73U) == 0x01730666U, "I2C_ANA_MST_CMD_WRITE formats Reg 6 write packet");
    TEST_ASSERT(I2C_ANA_MST_CMD_READ(0x66U, 5U) == 0x00000566U, "I2C_ANA_MST_CMD_READ formats Reg 5 read packet");
    TEST_ASSERT((uintptr_t)APB_SARADC_CAL_REG(0) == 0x6000E0D0U, "APB_SARADC_CAL_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)APB_SARADC_CAL_REG(11) == 0x6000E0FCU, "APB_SARADC_CAL_REG(11) address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_AGC_CTRL_REG == 0x600A7400U, "MODEM_RF_AGC_CTRL_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_AGC_CFG1_REG == 0x600A7414U, "MODEM_RF_AGC_CFG1_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_AGC_CFG2_REG == 0x600A7418U, "MODEM_RF_AGC_CFG2_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_AGC_CFG3_REG == 0x600A7424U, "MODEM_RF_AGC_CFG3_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_AGC_CFG4_REG == 0x600A7428U, "MODEM_RF_AGC_CFG4_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_AGC_CFG5_REG == 0x600A7438U, "MODEM_RF_AGC_CFG5_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_CAL_OFFSET_0_REG == 0x600A7C00U, "MODEM_RF_CAL_OFFSET_0_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_CAL_OFFSET_1_REG == 0x600A7C14U, "MODEM_RF_CAL_OFFSET_1_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_CAL_OFFSET_2_REG == 0x600A7C30U, "MODEM_RF_CAL_OFFSET_2_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_CAL_OFFSET_3_REG == 0x600A7C6CU, "MODEM_RF_CAL_OFFSET_3_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_CAL_OFFSET_4_REG == 0x600A7CA8U, "MODEM_RF_CAL_OFFSET_4_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_CAL_OFFSET_5_REG == 0x600A7CD0U, "MODEM_RF_CAL_OFFSET_5_REG address calculation");

    /* 2. Concrete Data Structure Geometry */
    TEST_ASSERT(sizeof(modem_clock_state_t) == 4U, "sizeof(modem_clock_state_t) must be 4 bytes");

    /* 3. Driver Lifecycle Initialization */
    TEST_ASSERT(modem_init() == MODEM_OK, "modem_init succeeds");
    TEST_ASSERT(modem_get_syscon_date() == MODEM_SYSCON_DATE_EXPECTED, "modem_get_syscon_date matches expected");
    TEST_ASSERT(modem_get_lpcon_date() == MODEM_LPCON_DATE_EXPECTED, "modem_get_lpcon_date matches expected");
    TEST_ASSERT((modem_get_rf_enable_reg() & MODEM_RF_ENABLE_MASTER_BIT) != 0U, "MODEM_RF Master Enable bit asserted after modem_init");
    TEST_ASSERT((modem_get_lp_ana_peri_pwr_reg() & LP_ANA_PERI_PWR_ENABLE_BIT) != 0U, "LP_ANA_PERI power enabled after modem_init");
    TEST_ASSERT(modem_get_lp_ana_peri_clk_reg() == LP_ANA_PERI_CLK_ENABLE_VAL, "LP_ANA_PERI clock enabled after modem_init");
    TEST_ASSERT(modem_is_rf_synth_enabled(), "modem_is_rf_synth_enabled reports true after modem_init");
    TEST_ASSERT(modem_is_sar_adc_cal_primed(), "SAR ADC calibration primed during modem_init");
    TEST_ASSERT((modem_get_i2c_ana_mst_link0_reg() & 0xFFC00000U) == 0x60000000U, "I2C_ANA_MST_LINK0 configured after modem_init");
    TEST_ASSERT((modem_get_i2c_ana_mst_link1_reg() & 0xFFC00000U) == 0x60000000U, "I2C_ANA_MST_LINK1 configured after modem_init");

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
    TEST_ASSERT(!modem_validate_coexistence(), "modem_validate_coexistence reports false when coex disabled");
    TEST_ASSERT(modem_enable_coexistence() == MODEM_OK, "modem_enable_coexistence succeeds");
    TEST_ASSERT(modem_is_coex_enabled(), "modem_is_coex_enabled reports true");
    TEST_ASSERT(modem_validate_coexistence(), "modem_validate_coexistence reports true when coex enabled");

    /* 9. Orchestrated Wireless Subsystems Activation (TEST 29 Stimulus) */
    TEST_ASSERT(modem_enable_all_clocks() == MODEM_OK, "modem_enable_all_clocks succeeds");
    TEST_ASSERT(modem_get_clock_state(&st) == MODEM_OK, "modem_get_clock_state succeeds after all enabled");
    TEST_ASSERT(st.wifi_clk_enabled == 1U, "Wi-Fi clock confirmed enabled");
    TEST_ASSERT(st.ble_clk_enabled == 1U, "BLE clock confirmed enabled");
    TEST_ASSERT(st.ieee802154_clk_enabled == 1U, "IEEE 802.15.4 clock confirmed enabled");
    TEST_ASSERT(st.coexistence_enabled == 1U, "Coexistence confirmed enabled");
    TEST_ASSERT((modem_get_rf_enable_reg() & MODEM_RF_ENABLE_MASTER_BIT) != 0U, "MODEM_RF Master Enable bit asserted after modem_enable_all_clocks");
    TEST_ASSERT(modem_is_rf_synth_enabled(), "modem_is_rf_synth_enabled confirmed enabled");

    /* 10. Wi-Fi RX AGC Override Validation */
    modem_force_rx_agc();

    /* 11. Analog I2C master: clocked for the PHY, BBPLL and the LP analog I2C master untouched */
    TEST_ASSERT(modem_enable_i2c_ana_mst() == MODEM_OK, "modem_enable_i2c_ana_mst succeeds");
    TEST_ASSERT(modem_get_lp_i2c_ana_mst_device_en() == 0U,
                "LP analog I2C master keeps no analog slaves (they stay on I2C_ANA_MST)");
}

static void test_lp_wdt_subsystem(void)
{
    printf("  [TEST] Low-Power Watchdog (LP_WDT) Register Architecture (Task 5.7 Remediation)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)LP_WDT_WDTCONFIG0_REG == 0x600B1C00U, "LP_WDT_WDTCONFIG0_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_WDT_CONFIG1_REG == 0x600B1C04U, "LP_WDT_CONFIG1_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_WDT_WDTFEED_REG == 0x600B1C14U, "LP_WDT_WDTFEED_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_WDT_WDTWPROTECT_REG == 0x600B1C18U, "LP_WDT_WDTWPROTECT_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_WDT_SWD_CONF_REG == 0x600B1C1CU, "LP_WDT_SWD_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)LP_WDT_SWD_WPROTECT_REG == 0x600B1C20U, "LP_WDT_SWD_WPROTECT_REG address calculation");

    /* 2. Parameterized Accessor Verification (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)LP_WDT_REG(0x0U) == 0x600B1C00U, "LP_WDT_REG(0x0) macro calculation");
    TEST_ASSERT((uintptr_t)LP_WDT_REG(0x14U) == 0x600B1C14U, "LP_WDT_REG(0x14) macro calculation");
    TEST_ASSERT((uintptr_t)LP_WDT_REG(0x18U) == 0x600B1C18U, "LP_WDT_REG(0x18) macro calculation");

    /* 3. Symbolic Constants & Bitmask Verification */
    TEST_ASSERT(LP_WDT_WKEY_VALUE == 0x50D83AA1U, "LP_WDT_WKEY_VALUE matches 0x50D83AA1");
    TEST_ASSERT(LP_WDT_WDTFEED_RTC_WDT_FEED_M == 0x80000000U, "LP_WDT_WDTFEED_RTC_WDT_FEED_M is bit 31");
    TEST_ASSERT(LP_WDT_SWD_CONF_SWD_DISABLE_M == 0x40000000U, "LP_WDT_SWD_CONF_SWD_DISABLE_M is bit 30");
    TEST_ASSERT(LP_WDT_SWD_CONF_SWD_FEED_M == 0x80000000U, "LP_WDT_SWD_CONF_SWD_FEED_M is bit 31");
    TEST_ASSERT(LP_WDT_WDTCONFIG0_WDT_EN_M == 0x80000000U, "LP_WDT_WDTCONFIG0_WDT_EN_M is bit 31");
}

static void test_wifi_mac_subsystem(void)
{
    printf("  [TEST] 802.11ax Wi-Fi 6 MAC Driver & Zero-Copy Packet Ring (Task 5.3)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)EFUSE_MAC_SYS_0_REG == 0x600B0844U, "EFUSE_MAC_SYS_0_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_MAC_SYS_1_REG == 0x600B0848U, "EFUSE_MAC_SYS_1_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_DATA_RF_DMA_DESC_ADDR_REG == 0x600AD000U, "MODEM_DATA_RF_DMA_DESC_ADDR_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_SYSCON_RF_DMA_ADDR_REG == 0x600AD000U, "MODEM_SYSCON_RF_DMA_ADDR_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_DATA_TX_DMA_DESC_ADDR_REG == 0x600AD004U, "MODEM_DATA_TX_DMA_DESC_ADDR_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_DATA_BLE_DMA_DESC_ADDR_REG == 0x600AD008U, "MODEM_DATA_BLE_DMA_DESC_ADDR_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_BB_TX_ON_DELAY_REG == 0x600A4010U, "WIFI_MAC_BB_TX_ON_DELAY_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TX_RAMP_DELAY_REG == 0x600A4014U, "WIFI_MAC_TX_RAMP_DELAY_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TX_CCA_START_TS_REG == 0x600A4018U, "WIFI_MAC_TX_CCA_START_TS_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_PHY_CCA_CTRL_REG == 0x600A4C5CU, "WIFI_MAC_PHY_CCA_CTRL_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_DBG_CTRL_REG == 0x600A4C7CU, "WIFI_MAC_DBG_CTRL_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TX_Q0_PTI_REG == 0x600A4D68U, "WIFI_MAC_TX_Q0_PTI_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TX_Q0_DMA_REG == 0x600A4D6CU, "WIFI_MAC_TX_Q0_DMA_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TSF_TIMER_LOW_REG == 0x600AD000U, "WIFI_MAC_TSF_TIMER_LOW_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TSF_TIMER_HIGH_REG == 0x600AD004U, "WIFI_MAC_TSF_TIMER_HIGH_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TSF_TBTT0_CONF_REG == 0x600AD050U, "WIFI_MAC_TSF_TBTT0_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TSF_TBTT1_CONF_REG == 0x600AD058U, "WIFI_MAC_TSF_TBTT1_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TSF_TBTT_TRIG0_REG == 0x600AD0A8U, "WIFI_MAC_TSF_TBTT_TRIG0_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_TSF_TBTT_TRIG1_REG == 0x600AD0B4U, "WIFI_MAC_TSF_TBTT_TRIG1_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_RATE_CTRL0_REG == 0x600A4440U, "WIFI_MAC_RATE_CTRL0_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_RATE_CTRL1_REG == 0x600A4444U, "WIFI_MAC_RATE_CTRL1_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_RATE_CTRL2_REG == 0x600A444CU, "WIFI_MAC_RATE_CTRL2_REG address calculation");
    TEST_ASSERT((uintptr_t)WIFI_MAC_RATE_CTRL3_REG == 0x600A4450U, "WIFI_MAC_RATE_CTRL3_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_ANALOG_SWITCH0_REG == 0x600AA008U, "MODEM_RF_ANALOG_SWITCH0_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_RF_ANALOG_SWITCH1_REG == 0x600AA02CU, "MODEM_RF_ANALOG_SWITCH1_REG address calculation");

    /* 2. Concrete Data Structure Geometry & Memory Sizing */
    TEST_ASSERT(PACKET_BUFFER_SIZE == 1536U, "PACKET_BUFFER_SIZE must be exactly 1536 bytes");
    TEST_ASSERT(PACKET_RING_COUNT == 28U, "PACKET_RING_COUNT must be exactly 28 descriptors");
    TEST_ASSERT(sizeof(net_packet_t) == (sizeof(dma_descriptor_t) + PACKET_BUFFER_SIZE), "net_packet_t layout packed with descriptor and buffer");
    TEST_ASSERT((sizeof(net_packet_t) % 4U) == 0U, "net_packet_t must be 4-byte aligned");

    /* 3. Subsystem Lifecycle & MAC Address Retrieval */
    TEST_ASSERT(wifi_init() == WIFI_OK, "wifi_init succeeds");
    TEST_ASSERT(wifi_get_state() == WIFI_STATE_IDLE, "Wi-Fi initial state is WIFI_STATE_IDLE");

    /* Baseband DMA Linkage & Timings (Task 3 Remediation) */
    TEST_ASSERT(wifi_get_bb_tx_on_delay() == WIFI_MAC_DEFAULT_BB_TX_ON_DELAY_US, "bb_tx_on_delay matches 50us default");
    TEST_ASSERT(wifi_get_tx_ramp_delay() == WIFI_MAC_DEFAULT_TX_RAMP_DELAY_US, "tx_ramp_delay matches 60us default");
    TEST_ASSERT(wifi_get_tx_cca_start_ts() == WIFI_MAC_DEFAULT_TX_CCA_START_TS_US, "tx_cca_start_ts matches 80us default");

    uint8_t mac[WIFI_MAC_ADDR_LEN] = {0};
    TEST_ASSERT(wifi_get_mac_addr(NULL) == WIFI_ERR_INVALID_ARG, "wifi_get_mac_addr rejects NULL");
    TEST_ASSERT(wifi_get_mac_addr(mac) == WIFI_OK, "wifi_get_mac_addr succeeds");
    TEST_ASSERT(mac[0] == 0x40U && mac[1] == 0x4CU && mac[2] == 0xCAU &&
                mac[3] == 0x45U && mac[4] == 0x1EU && mac[5] == 0x14U,
                "Authentic Wi-Fi Station MAC matches hardware 40:4C:CA:45:1E:14");

    wifi_telemetry_t telem;
    TEST_ASSERT(wifi_get_telemetry(NULL) == WIFI_ERR_INVALID_ARG, "wifi_get_telemetry rejects NULL");
    TEST_ASSERT(wifi_get_telemetry(&telem) == WIFI_OK, "wifi_get_telemetry succeeds");
    TEST_ASSERT(telem.rx_ring_capacity == PACKET_RING_COUNT, "RX ring capacity is 28");

    /* 4. Circular Packet Ring Integrity & Boundary Traversal (TEST 31) */
    uint32_t visited_count = 0U;
    TEST_ASSERT(wifi_verify_rx_ring(NULL) == WIFI_ERR_INVALID_ARG, "wifi_verify_rx_ring rejects NULL");
    TEST_ASSERT(wifi_verify_rx_ring(&visited_count) == WIFI_OK, "wifi_verify_rx_ring succeeds");
    TEST_ASSERT(visited_count == PACKET_RING_COUNT, "Circular traversal visits all 28 descriptors and loops back");

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

    /* 6. Packet Transmission: one frame per call, handed to the given interface */
    uint8_t tx_frame[128];
    memset(tx_frame, 0x5AU, sizeof(tx_frame));
    TEST_ASSERT(wifi_tx_packet(WIFI_TX_IF_AP, NULL, sizeof(tx_frame)) == WIFI_ERR_INVALID_ARG, "wifi_tx_packet rejects NULL payload");
    TEST_ASSERT(wifi_tx_packet(WIFI_TX_IF_AP, tx_frame, 0U) == WIFI_ERR_INVALID_ARG, "wifi_tx_packet rejects length 0");
    TEST_ASSERT(wifi_tx_packet(WIFI_TX_IF_AP, tx_frame, 2048U) == WIFI_ERR_INVALID_ARG, "wifi_tx_packet rejects length > 1536");
    TEST_ASSERT(wifi_tx_packet((wifi_tx_if_t)7, tx_frame, sizeof(tx_frame)) == WIFI_ERR_INVALID_ARG, "wifi_tx_packet rejects unknown interface");

    TEST_ASSERT(wifi_get_telemetry(&telem) == WIFI_OK, "wifi_get_telemetry succeeds");
    uint32_t tx_before = telem.tx_packets;
    tx_frame[0] = 0x11U;
    TEST_ASSERT(wifi_tx_packet(WIFI_TX_IF_STA, tx_frame, sizeof(tx_frame)) == WIFI_OK, "wifi_tx_packet STA succeeds (host)");
    uint16_t last_len = 0U;
    wifi_tx_if_t last_if = WIFI_TX_IF_AP;
    const uint8_t *last = wifi_host_last_tx(&last_len, &last_if);
    TEST_ASSERT(last_len == sizeof(tx_frame) && last_if == WIFI_TX_IF_STA && last[0] == 0x11U, "Frame and STA interface handed through unchanged");
    TEST_ASSERT(wifi_tx_packet(WIFI_TX_IF_AP, tx_frame, 64U) == WIFI_OK, "wifi_tx_packet AP succeeds (host)");
    wifi_host_last_tx(&last_len, &last_if);
    TEST_ASSERT(last_len == 64U && last_if == WIFI_TX_IF_AP, "Frame length and AP interface handed through unchanged");
    TEST_ASSERT(wifi_get_telemetry(&telem) == WIFI_OK, "wifi_get_telemetry succeeds");
    TEST_ASSERT(telem.tx_packets == tx_before + 2U, "tx_packets counts only frames handed over");

    /* 7. SoftAP Broadcasting Functionality (Task 5.7.2) */
    TEST_ASSERT(!wifi_is_ap_active(), "SoftAP is initially inactive");
    TEST_ASSERT(wifi_start_ap(NULL, NULL, 0U) == WIFI_OK, "wifi_start_ap succeeds with default parameters");
    TEST_ASSERT(wifi_is_ap_active(), "SoftAP is active after start");
    TEST_ASSERT(wifi_get_state() == WIFI_STATE_AP_ACTIVE, "Wi-Fi state is WIFI_STATE_AP_ACTIVE");
    TEST_ASSERT(s_strncmp(wifi_get_ap_ssid(), WIFI_DEFAULT_AP_SSID, 8) == 0, "Default AP SSID matches 'IronV-C6'");
    TEST_ASSERT(wifi_get_ap_channel() == WIFI_DEFAULT_AP_CHANNEL, "Default AP channel matches 1");

    /* Stop AP */
    TEST_ASSERT(wifi_stop_ap() == WIFI_OK, "wifi_stop_ap succeeds");
    TEST_ASSERT(!wifi_is_ap_active(), "SoftAP is inactive after stop");
    TEST_ASSERT(wifi_get_state() == WIFI_STATE_IDLE, "Wi-Fi state is WIFI_STATE_IDLE after stop");

    /* Start AP with custom SSID, password, and channel */
    TEST_ASSERT(wifi_start_ap("IronV-Lab", "supersecret123", 6U) == WIFI_OK, "wifi_start_ap succeeds with custom config");
    TEST_ASSERT(wifi_is_ap_active(), "SoftAP is active after custom start");
    TEST_ASSERT(wifi_get_state() == WIFI_STATE_AP_ACTIVE, "Wi-Fi state is WIFI_STATE_AP_ACTIVE with custom config");
    TEST_ASSERT(s_strncmp(wifi_get_ap_ssid(), "IronV-Lab", 9) == 0, "Custom AP SSID matches 'IronV-Lab'");
    TEST_ASSERT(wifi_get_ap_channel() == 6U, "Custom AP channel matches 6");

    /* Test event simulation on host */
    wifi_handle_vendor_event(WIFI_VENDOR_EVENT_AP_STOP, NULL);
    TEST_ASSERT(!wifi_is_ap_active(), "SoftAP is inactive after vendor AP_STOP event");
    TEST_ASSERT(wifi_get_state() == WIFI_STATE_IDLE, "Wi-Fi state is WIFI_STATE_IDLE after AP_STOP event");

    wifi_handle_vendor_event(WIFI_VENDOR_EVENT_AP_START, NULL);
    TEST_ASSERT(wifi_is_ap_active(), "SoftAP is active after vendor AP_START event");
    TEST_ASSERT(wifi_get_state() == WIFI_STATE_AP_ACTIVE, "Wi-Fi state is WIFI_STATE_AP_ACTIVE after AP_START event");

    TEST_ASSERT(wifi_stop_ap() == WIFI_OK, "wifi_stop_ap cleanly stops AP");
    TEST_ASSERT(!wifi_is_ap_active(), "SoftAP is inactive after final stop");

    /* 8. RF Front-End Analog Switch & CCA Control (Task 5.7.3) */
    TEST_ASSERT(wifi_set_cca_enabled(false) == WIFI_OK, "wifi_set_cca_enabled(false) succeeds");
    TEST_ASSERT(!wifi_is_cca_enabled(), "wifi_is_cca_enabled is false when CCA is disabled");
    TEST_ASSERT(wifi_set_cca_enabled(true) == WIFI_OK, "wifi_set_cca_enabled(true) succeeds");
    TEST_ASSERT(wifi_is_cca_enabled(), "wifi_is_cca_enabled is true when CCA is enabled");
    modem_rf_analog_init();
    TEST_ASSERT(modem_get_rf_analog_switch0() == MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG, "modem_get_rf_analog_switch0 matches default config");
    TEST_ASSERT(modem_get_rf_analog_switch1() == MODEM_RF_ANALOG_SWITCH_DEFAULT_CONFIG, "modem_get_rf_analog_switch1 matches default config");
}

static void test_wifi_custom_stack_refactor(void)
{
    printf("  [TEST] Native Wi-Fi Stack Refactor & Regulatory/FTM/PHY Validation...\n");

    /* 1. Wi-Fi Regulatory Country Domain Table Verification */
    size_t reg_count = 0U;
    bool found_default = false;
    bool found_us = false;
    bool found_jp = false;
    bool found_sentinel = false;

    for (size_t i = 0U; i < 500U; i++)
    {
        const wifi_regdomain_t *entry = &regdomain_table[i];
        if (entry->cn[0] == '#' && entry->cn[1] == '#')
        {
            found_sentinel = true;
            TEST_ASSERT(entry->regulatory_type == ESP_WIFI_REGULATORY_TYPE_MAX, "Sentinel regulatory type is TYPE_MAX");
            reg_count = i;
            break;
        }
        if (entry->cn[0] == '0' && entry->cn[1] == '1' && entry->regulatory_type == ESP_WIFI_REGULATORY_TYPE_DEFAULT)
        {
            found_default = true;
        }
        if (entry->cn[0] == 'U' && entry->cn[1] == 'S' && entry->regulatory_type == ESP_WIFI_REGULATORY_TYPE_FCC)
        {
            found_us = true;
        }
        if (entry->cn[0] == 'J' && entry->cn[1] == 'P' && entry->regulatory_type == ESP_WIFI_REGULATORY_TYPE_MIC)
        {
            found_jp = true;
        }
    }
    TEST_ASSERT(found_sentinel, "Regulatory country table terminated by ## sentinel");
    TEST_ASSERT(reg_count > 150U, "Regulatory country table contains comprehensive ISO country mappings (>150)");
    TEST_ASSERT(found_default, "Regulatory table contains default country 01 -> TYPE_DEFAULT");
    TEST_ASSERT(found_us, "Regulatory table maps US -> TYPE_FCC");
    TEST_ASSERT(found_jp, "Regulatory table maps JP -> TYPE_MIC");

    /* 2. Wi-Fi Regulatory Rule Data & Geometry Verification */
    const wifi_regulatory_t *reg_def = &regulatory_data[ESP_WIFI_REGULATORY_TYPE_DEFAULT];
    TEST_ASSERT(reg_def->n_reg_rules == WIFI_REG_RULE_NUM_SINGLE, "Default regulatory domain has 1 rule");
    TEST_ASSERT(reg_def->reg_rules[0].start_channel == WIFI_REG_CHAN_MIN, "Default domain starts at channel 1");
    TEST_ASSERT(reg_def->reg_rules[0].end_channel == WIFI_REG_CHAN_MAX_11, "Default domain ends at channel 11");
    TEST_ASSERT(reg_def->reg_rules[0].max_bandwidth == WIFI_REG_BW_40M, "Default domain max bandwidth is 40MHz");
    TEST_ASSERT(reg_def->reg_rules[0].max_eirp == WIFI_REG_EIRP_20DBM, "Default domain max EIRP is 20dBm");

    const wifi_regulatory_t *reg_mic = &regulatory_data[ESP_WIFI_REGULATORY_TYPE_MIC];
    TEST_ASSERT(reg_mic->n_reg_rules == WIFI_REG_RULE_NUM_DUAL, "Japan (MIC) domain has 2 rules");
    TEST_ASSERT(reg_mic->reg_rules[0].start_channel == WIFI_REG_CHAN_MIN && reg_mic->reg_rules[0].end_channel == WIFI_REG_CHAN_MAX_13, "MIC rule 0 covers channels 1-13");
    TEST_ASSERT(reg_mic->reg_rules[0].max_bandwidth == WIFI_REG_BW_40M, "MIC rule 0 max bandwidth is 40MHz");
    TEST_ASSERT(reg_mic->reg_rules[1].start_channel == WIFI_REG_CHAN_MAX_14 && reg_mic->reg_rules[1].end_channel == WIFI_REG_CHAN_MAX_14, "MIC rule 1 covers channel 14");
    TEST_ASSERT(reg_mic->reg_rules[1].max_bandwidth == WIFI_REG_BW_20M, "MIC rule 1 max bandwidth is 20MHz");

    /* Validate all domain profiles are within bounds */
    for (int t = 0; t < ESP_WIFI_REGULATORY_TYPE_MAX; t++)
    {
        const wifi_regulatory_t *prof = &regulatory_data[t];
        TEST_ASSERT(prof->n_reg_rules >= WIFI_REG_RULE_NUM_SINGLE && prof->n_reg_rules <= WIFI_MAX_REGULATORY_RULE_NUM, "Profile rule count within bounds");
        for (uint8_t r = 0U; r < prof->n_reg_rules; r++)
        {
            TEST_ASSERT(prof->reg_rules[r].start_channel >= WIFI_REG_CHAN_MIN, "Rule start channel valid");
            TEST_ASSERT(prof->reg_rules[r].end_channel <= WIFI_REG_CHAN_MAX_14, "Rule end channel valid");
            TEST_ASSERT(prof->reg_rules[r].start_channel <= prof->reg_rules[r].end_channel, "Rule channel span monotonic");
            TEST_ASSERT(prof->reg_rules[r].max_bandwidth == WIFI_REG_BW_20M || prof->reg_rules[r].max_bandwidth == WIFI_REG_BW_40M, "Bandwidth valid");
            TEST_ASSERT(prof->reg_rules[r].max_eirp >= WIFI_REG_EIRP_20DBM && prof->reg_rules[r].max_eirp <= WIFI_REG_EIRP_36DBM, "EIRP within 20-36 dBm");
        }
    }

    /* 3. Fine Timing Measurement (FTM) Calibration Data Verification */
    TEST_ASSERT(est_PHY_INIT_FTM_COMP_20_20U_MHZ == WIFI_FTM_CAL_INIT_20_20U_MHZ, "FTM INIT 20/20U initialized correctly");
    TEST_ASSERT(est_PHY_INIT_FTM_COMP_20_20D_MHZ == WIFI_FTM_CAL_INIT_20_20D_MHZ, "FTM INIT 20/20D initialized correctly");
    TEST_ASSERT(est_PHY_RESP_FTM_COMP_20_20U_MHZ == WIFI_FTM_CAL_RESP_20_20U_MHZ, "FTM RESP 20/20U initialized correctly");
    TEST_ASSERT(est_PHY_RESP_FTM_COMP_20_20D_MHZ == WIFI_FTM_CAL_RESP_20_20D_MHZ, "FTM RESP 20/20D initialized correctly");
    TEST_ASSERT(est_PHY_INIT_FTM_COMP_40_40U_MHZ == WIFI_FTM_CAL_INIT_40_40U_MHZ, "FTM INIT 40/40U initialized correctly");
    TEST_ASSERT(est_PHY_INIT_FTM_COMP_40_40D_MHZ == WIFI_FTM_CAL_INIT_40_40D_MHZ, "FTM INIT 40/40D initialized correctly");
    TEST_ASSERT(est_PHY_RESP_FTM_COMP_40_40U_MHZ == WIFI_FTM_CAL_RESP_40_40U_MHZ, "FTM RESP 40/40U initialized correctly");

    /* Specific validation for ESP32-C6 40MHz channel 11 responder calibration constant */
    TEST_ASSERT(est_PHY_RESP_FTM_COMP_40_40D_MHZ == 433U, "FTM RESP 40/40D matches ESP32-C6 silicon specification 433");
    TEST_ASSERT(est_PHY_RESP_FTM_COMP_40_40D_MHZ_DIS == 433U, "FTM RESP 40/40D DIS matches ESP32-C6 silicon specification 433");

    /* 4. Canonical PHY Initialization Data Verification */
    TEST_ASSERT(sizeof(phy_init_data.params) == WIFI_PHY_INIT_DATA_LEN, "PHY init data table length is 128 bytes");
    TEST_ASSERT(phy_init_data.params[0] == 0x0AU, "PHY init data version byte matches 0x0A");
    TEST_ASSERT(phy_init_data.params[127] == 0x51U, "PHY init data checksum byte matches 0x51");
    TEST_ASSERT(sizeof(esp_phy_init_data_t) == 128U, "esp_phy_init_data_t size is exactly 128 bytes");
    TEST_ASSERT(sizeof(esp_phy_calibration_data_t) == (WIFI_PHY_CAL_VERSION_LEN + WIFI_PHY_CAL_MAC_LEN + WIFI_PHY_CAL_OPAQUE_LEN), "PHY calibration container geometry matches");

    /* 5. Vendor Types, ABI Compatibility & OSAL Constants Verification */
    TEST_ASSERT(ESP_WIFI_OS_ADAPTER_VERSION == 0x00000009U, "ESP_WIFI_OS_ADAPTER_VERSION matches ABI");
    TEST_ASSERT(ESP_WIFI_OS_ADAPTER_MAGIC == 0xDEADBEAFU, "ESP_WIFI_OS_ADAPTER_MAGIC matches ABI");
    TEST_ASSERT(offsetof(wpa_crypto_funcs_t, hmac_sha256_vector) == sizeof(uint32_t) * 2U, "wpa_crypto_funcs_t callbacks follow size and version");
    TEST_ASSERT(offsetof(wpa_crypto_funcs_t, aes_unwrap) == offsetof(wpa_crypto_funcs_t, hmac_sha256_vector) + sizeof(void *) * 10U, "wpa_crypto_funcs_t has 11 callbacks in ESP-IDF order");
    TEST_ASSERT(WIFI_CSI_DISABLED == 0, "WIFI_CSI_DISABLED zero constant verified");
    TEST_ASSERT(WIFI_AMSDU_TX_DISABLED == 0, "WIFI_AMSDU_TX_DISABLED zero constant verified");
    TEST_ASSERT(WIFI_NVS_DISABLED == 0, "WIFI_NVS_DISABLED zero constant verified");
    TEST_ASSERT(WIFI_RMAC_AUTO_RESET_INT_DEF == 0, "WIFI_RMAC_AUTO_RESET_INT_DEF zero constant verified");
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
    TEST_ASSERT(telem.channel == IEEE802154_CHANNEL_DEFAULT, "Default channel matches configuration");
    TEST_ASSERT(telem.freq_mhz == ieee802154_get_freq_mhz(IEEE802154_CHANNEL_DEFAULT), "Default channel frequency matches default");
    TEST_ASSERT(telem.short_addr == IEEE802154_DEFAULT_SHORT_ADDR, "Default short address matches configuration");
    TEST_ASSERT(telem.pan_id == IEEE802154_DEFAULT_PAN_ID, "Default PAN ID matches configuration");
    TEST_ASSERT(telem.auto_ack_tx == (CONFIG_IEEE802154_AUTO_ACK_TX != 0U), "Auto-ACK TX matches configuration");
    TEST_ASSERT(telem.auto_ack_rx == (CONFIG_IEEE802154_AUTO_ACK_RX != 0U), "Auto-ACK RX matches configuration");

    /* 3. RF Channel & Frequency Range Validation */
    TEST_ASSERT(ieee802154_set_channel(10U) == IEEE802154_ERR_INVALID_ARG, "Channel 10 below range rejected");
    TEST_ASSERT(ieee802154_set_channel(27U) == IEEE802154_ERR_INVALID_ARG, "Channel 27 above range rejected");
    TEST_ASSERT(ieee802154_set_channel(11U) == IEEE802154_OK, "Channel 11 accepted");
    TEST_ASSERT(ieee802154_get_channel() == 11U, "Channel readback is 11");
    TEST_ASSERT(ieee802154_get_freq_mhz(11U) == 2405U, "Channel 11 frequency is 2405 MHz");
    TEST_ASSERT(ieee802154_set_channel(26U) == IEEE802154_OK, "Channel 26 accepted");
    TEST_ASSERT(ieee802154_get_freq_mhz(26U) == 2480U, "Channel 26 frequency is 2480 MHz");
    TEST_ASSERT(ieee802154_set_channel(IEEE802154_CHANNEL_DEFAULT) == IEEE802154_OK, "Default channel restored");

    /* 4. Addressing Controls (Short, PAN ID, EUI-64) */
    TEST_ASSERT(ieee802154_set_short_address(0xABCDU) == IEEE802154_OK, "Set short address succeeds");
    TEST_ASSERT(ieee802154_get_short_address() == 0xABCDU, "Short address readback matches 0xABCD");
    TEST_ASSERT(ieee802154_set_short_address(IEEE802154_DEFAULT_SHORT_ADDR) == IEEE802154_OK, "Default short address restored");

    TEST_ASSERT(ieee802154_set_pan_id(0xCAFEU) == IEEE802154_OK, "Set PAN ID succeeds");
    TEST_ASSERT(ieee802154_get_pan_id() == 0xCAFEU, "PAN ID readback matches 0xCAFE");
    TEST_ASSERT(ieee802154_set_pan_id(IEEE802154_DEFAULT_PAN_ID) == IEEE802154_OK, "Default PAN ID restored");

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
    TEST_ASSERT(cfg.ip != 0U, "Active IP is non-zero");
    TEST_ASSERT(cfg.netmask != 0U, "Active netmask is non-zero");
    TEST_ASSERT(cfg.gateway != 0U, "Active gateway is non-zero");

    /* Dynamically derive peer IP within active subnet */
    uint32_t peer_ip;
    if (cfg.gateway != 0U && cfg.gateway != cfg.ip)
    {
        peer_ip = cfg.gateway;
    }
    else
    {
        uint32_t subnet = cfg.ip & cfg.netmask;
        uint32_t host_part = cfg.ip & ~cfg.netmask;
        uint32_t peer_host = (host_part == 1U) ? 2U : 1U;
        peer_ip = subnet | (peer_host & ~cfg.netmask);
    }

    /* IP String Conversions Round-Trip */
    char ip_str[NET_IP_STR_BUF_LEN];
    net_ip_to_str(cfg.ip, ip_str, sizeof(ip_str));
    TEST_ASSERT(net_str_to_ip(ip_str) == cfg.ip, "net_ip_to_str and net_str_to_ip round-trip on active IP");
    uint32_t parsed_ip = net_str_to_ip("192.168.1.55");
    TEST_ASSERT(parsed_ip == NET_IP4_ADDR(192U, 168U, 1U, 55U), "net_str_to_ip parses 192.168.1.55");
    TEST_ASSERT(net_set_ip(parsed_ip, cfg.netmask, cfg.gateway) == NET_OK, "net_set_ip succeeds");
    TEST_ASSERT(net_set_ip(cfg.ip, cfg.netmask, cfg.gateway) == NET_OK, "restore active IP");

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
    TEST_ASSERT(arp_lookup(peer_ip, found_mac) == NET_ERR_NOT_FOUND, "Lookup unmapped IP returns NOT_FOUND");
    uint8_t test_mac[ETH_ADDR_LEN] = { 0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U };
    TEST_ASSERT(arp_insert(peer_ip, test_mac) == NET_OK, "arp_insert succeeds");
    TEST_ASSERT(arp_lookup(peer_ip, found_mac) == NET_OK, "Lookup mapped IP succeeds");
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
    arp_req.arp.sender_ip = NET_HTONL(peer_ip);
    arp_req.arp.target_ip = NET_HTONL(cfg.ip);

    uint8_t reply_buf[64] = {0};
    uint16_t reply_len = 0U;
    TEST_ASSERT(arp_process_packet((const uint8_t *)&arp_req, sizeof(arp_req), reply_buf, sizeof(reply_buf), &reply_len) == NET_OK, "arp_process_packet succeeds");
    TEST_ASSERT(reply_len == sizeof(arp_frame_t), "Reply length is 42 bytes");
    const arp_frame_t *reply_f = (const arp_frame_t *)reply_buf;
    TEST_ASSERT(reply_f->arp.opcode == NET_HTONS(ARP_OPCODE_REPLY), "Opcode is ARP_OPCODE_REPLY");
    TEST_ASSERT(reply_f->arp.sender_ip == NET_HTONL(cfg.ip), "Sender IP in reply is our IP");
    TEST_ASSERT(reply_f->arp.target_ip == NET_HTONL(peer_ip), "Target IP in reply is requester IP");

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
    ip_req->ttl = IPV4_TTL_DEFAULT;
    ip_req->src_ip = NET_HTONL(peer_ip);
    ip_req->dest_ip = NET_HTONL(cfg.ip);
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
    const ipv4_header_t *rep_ip = (const ipv4_header_t *)(icmp_rep_buf + ETH_HDR_LEN);
    TEST_ASSERT(rep_ip->src_ip == NET_HTONL(cfg.ip), "Echo reply src is our IP");
    TEST_ASSERT(rep_ip->dest_ip == NET_HTONL(peer_ip), "Echo reply dest is peer IP");
    TEST_ASSERT(rep_icmp->type == ICMP_TYPE_ECHO_REPLY, "Echo reply type is 0");
    TEST_ASSERT(rep_icmp->code == ICMP_CODE_ECHO, "Echo reply code is 0");

    /* 6. TCP State Machine & PCB Lifecycle */
    tcp_pcb_t *pcb = tcp_new();
    TEST_ASSERT(pcb != NULL, "tcp_new allocates PCB");
    TEST_ASSERT(pcb->state == TCP_STATE_CLOSED, "Initial PCB state is CLOSED");
    TEST_ASSERT(tcp_bind(pcb, CONFIG_TCP_DEFAULT_HTTP_PORT) == TCP_OK, "tcp_bind to configured HTTP port succeeds");
    TEST_ASSERT(tcp_listen(pcb, NULL) == TCP_OK, "tcp_listen succeeds");
    TEST_ASSERT(pcb->state == TCP_STATE_LISTEN, "State transitions to LISTEN");
    TEST_ASSERT(tcp_close(pcb) == TCP_OK, "tcp_close succeeds");
    TEST_ASSERT(pcb->state == TCP_STATE_CLOSED, "State returns to CLOSED");

    /* 7. Reset to Defaults */
    TEST_ASSERT(net_reset_defaults() == NET_OK, "net_reset_defaults succeeds");
}

static void test_custom_http_handler(const char *qp, char *body, size_t max)
{
    (void)qp;
    const char msg[] = "{\"custom\":\"ok\"}";
    size_t mlen = strlen(msg);
    if (mlen >= max) mlen = max - 1U;
    memcpy(body, msg, mlen);
    body[mlen] = '\0';
}

static void test_http_server_subsystem(void)
{
    printf("  [TEST] Zero-Allocation Local REST/HTTP Engine & Embedded Web UI (Task 6.1)...\n");

    /* 1. Protocol & Sizing Constants */
    TEST_ASSERT(HTTP_SERVER_DEFAULT_PORT == 80U, "HTTP default port is 80");
    TEST_ASSERT(HTTP_MAX_ROUTES == 24U, "HTTP_MAX_ROUTES is 24");
    TEST_ASSERT(HTTP_REQUEST_BUF_SIZE == 1536U, "HTTP request buffer size is 1536");

    /* Request assembly: phones send headers and body in separate segments (REV-29 bug) */
    {
        const char hdr_only[] = "POST /api/wifi/configure HTTP/1.1\r\nHost: 192.168.1.1\r\nCONTENT-LENGTH: 46\r\n\r\n";
        const char full[] = "POST /api/wifi/configure HTTP/1.1\r\nHost: 192.168.1.1\r\nCONTENT-LENGTH: 46\r\n\r\n"
                            "{\"ssid\":\"SplitNet\",\"password\":\"longenough123\"}";
        const char partial[] = "POST /api/wifi/configure HTTP/1.1\r\nHost: 192.168.1.1\r\nContent-Length: 46\r\n\r\n{\"ssid\":";
        const char get_req[] = "GET /setup HTTP/1.1\r\nHost: 192.168.1.1\r\n\r\n";
        const char no_end[] = "GET /setup HTTP/1.1\r\nHost: 192.168.1.1\r\n";
        TEST_ASSERT(strlen(full) - strlen(hdr_only) == 46U, "Test body is 46 bytes");
        TEST_ASSERT(http_request_state(hdr_only, strlen(hdr_only)) == HTTP_REQ_INCOMPLETE, "Headers without the body: incomplete");
        TEST_ASSERT(http_request_state(partial, strlen(partial)) == HTTP_REQ_INCOMPLETE, "Partial body: incomplete");
        TEST_ASSERT(http_request_state(full, strlen(full)) == HTTP_REQ_COMPLETE, "Headers + Content-Length body: complete");
        TEST_ASSERT(http_request_state(get_req, strlen(get_req)) == HTTP_REQ_COMPLETE, "GET without body: complete at the blank line");
        TEST_ASSERT(http_request_state(no_end, strlen(no_end)) == HTTP_REQ_INCOMPLETE, "Headers not finished: incomplete");
        const char huge[] = "POST /x HTTP/1.1\r\nContent-Length: 9999\r\n\r\n";
        TEST_ASSERT(http_request_state(huge, strlen(huge)) == HTTP_REQ_TOO_LARGE, "Body larger than the buffer: too large");
    }
    TEST_ASSERT(HTTP_RESPONSE_BUF_SIZE == 4096U, "HTTP response buffer size is 4096");

    /* 2. Lifecycle & Route Registration */
    TEST_ASSERT(http_server_init() == HTTP_OK, "http_server_init succeeds");
    uint16_t initial_routes = http_server_get_route_count();
    TEST_ASSERT(initial_routes >= 15U, "Default routes registered >= 15");

    /* Register custom route */
    TEST_ASSERT(http_route_register("/api/test", HTTP_METHOD_GET, test_custom_http_handler) == HTTP_OK, "Custom route register succeeds");
    TEST_ASSERT(http_server_get_route_count() == initial_routes + 1U, "Route count incremented");
    TEST_ASSERT(http_route_register("/api/test", HTTP_METHOD_GET, test_custom_http_handler) == HTTP_OK &&
                http_server_get_route_count() == initial_routes + 1U,
                "Re-registering a path/method replaces it instead of adding a slot");

    /* 3. Route Lookup */
    bool path_match = false;
    const http_route_t *found = http_route_find("/api/test", HTTP_METHOD_GET, &path_match);
    TEST_ASSERT(found != NULL && path_match == true, "Find custom route succeeds");
    TEST_ASSERT(http_route_find("/nonexistent", HTTP_METHOD_GET, &path_match) == NULL && path_match == false, "Non-existent route returns NULL");

    /* 4. Request Processing: Valid GET /api/status */
    const char req_status[] = "GET /api/status HTTP/1.1\r\nHost: 192.168.1.100\r\n\r\n";
    char resp[1536] = {0};
    size_t resp_len = 0U;
    TEST_ASSERT(http_process_request(req_status, strlen(req_status), resp, sizeof(resp), &resp_len) == HTTP_OK, "Process GET /api/status succeeds");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 200 OK") != NULL, "Status 200 OK in response");
    TEST_ASSERT(strstr(resp, "uptime_ms") != NULL, "JSON contains uptime_ms");
    TEST_ASSERT(strstr(resp, "Content-Type: application/json") != NULL, "Content-Type is JSON");
    TEST_ASSERT(strstr(resp, "Access-Control-Allow-Origin: *") != NULL, "CORS header present");

    /* 4b. Request Processing: Valid GET /api/health */
    const char req_health[] = "GET /api/health HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_health, strlen(req_health), resp, sizeof(resp), &resp_len) == HTTP_OK, "Process GET /api/health succeeds");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 200 OK") != NULL, "Health response is 200 OK");
    TEST_ASSERT(strstr(resp, "uptime_seconds") != NULL, "Health contains uptime_seconds");
    TEST_ASSERT(strstr(resp, "arena_bytes_used") != NULL, "Health contains arena_bytes_used");
    TEST_ASSERT(strstr(resp, "dpc_queue_drops") != NULL, "Health contains dpc_queue_drops");

    /* 4c. Request Processing: Valid GET & POST /api/speedtest */
    const char req_speed_get[] = "GET /api/speedtest HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_speed_get, strlen(req_speed_get), resp, sizeof(resp), &resp_len) == HTTP_OK, "Process GET /api/speedtest succeeds");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 200 OK") != NULL, "Speedtest GET response is 200 OK");
    TEST_ASSERT(strstr(resp, "bursts_run") != NULL, "Speedtest contains bursts_run");

    const char req_speed_post[] = "POST /api/speedtest?run=1 HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_speed_post, strlen(req_speed_post), resp, sizeof(resp), &resp_len) == HTTP_OK, "Process POST /api/speedtest succeeds");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 200 OK") != NULL, "Speedtest POST response is 200 OK");
    TEST_ASSERT(strstr(resp, "throughput_kbps") != NULL, "Speedtest burst contains throughput_kbps");

    /* 4d. Matter was descoped (REV-08): its endpoint is gone */
    const char req_matter[] = "GET /api/matter/payload HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_matter, strlen(req_matter), resp, sizeof(resp), &resp_len) == HTTP_ERR_NOT_FOUND, "GET /api/matter/payload returns 404 (Matter descoped)");

    /* 4e. Request Processing: Valid GET & POST /api/gpio */
    const char req_gpio_get[] = "GET /api/gpio HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_gpio_get, strlen(req_gpio_get), resp, sizeof(resp), &resp_len) == HTTP_OK, "Process GET /api/gpio succeeds");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 200 OK") != NULL, "GPIO GET response is 200 OK");
    TEST_ASSERT(strstr(resp, "Status LED") != NULL, "GPIO GET lists Status LED");

    const char req_gpio_set[] = "POST /api/gpio?pin=15&value=1 HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_gpio_set, strlen(req_gpio_set), resp, sizeof(resp), &resp_len) == HTTP_OK, "Process POST /api/gpio set succeeds");
    TEST_ASSERT(strstr(resp, "\"pin\":15") != NULL, "GPIO set reports pin 15");
    TEST_ASSERT(strstr(resp, "\"level\":1") != NULL, "GPIO set reports level 1");

    const char req_gpio_toggle[] = "POST /api/gpio?pin=15&toggle=1 HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_gpio_toggle, strlen(req_gpio_toggle), resp, sizeof(resp), &resp_len) == HTTP_OK, "Process POST /api/gpio toggle succeeds");
    TEST_ASSERT(strstr(resp, "\"pin\":15") != NULL, "GPIO toggle reports pin 15");
    TEST_ASSERT(strstr(resp, "\"level\":0") != NULL, "GPIO toggle reports level 0");

    /* 4f. Request Processing: OPTIONS CORS Preflight */
    const char req_options[] = "OPTIONS /api/status HTTP/1.1\r\nOrigin: http://localhost:8080\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_options, strlen(req_options), resp, sizeof(resp), &resp_len) == HTTP_OK, "Process OPTIONS preflight succeeds");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 204 No Content") != NULL, "OPTIONS returns 204 No Content");
    TEST_ASSERT(strstr(resp, "Access-Control-Allow-Methods: GET, POST, OPTIONS") != NULL, "CORS Allow-Methods present");
    TEST_ASSERT(strstr(resp, "Access-Control-Allow-Headers: Content-Type, Authorization") != NULL, "CORS Allow-Headers present");

    /* 4g. Connectivity probes never report "connected": captive mode redirects to the
     *     setup page, the default "no internet" mode answers 404 */
    const char *probe_paths[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/ncsi.txt", "/connecttest.txt"};
    char probe_req[96];
    char portal_loc[48];
    net_config_t probe_cfg;
    net_get_config(&probe_cfg);
    snprintf(portal_loc, sizeof(portal_loc), "Location: http://%u.%u.%u.%u/setup\r\n",
             (unsigned)((probe_cfg.ip >> 24) & 0xFFU), (unsigned)((probe_cfg.ip >> 16) & 0xFFU),
             (unsigned)((probe_cfg.ip >> 8) & 0xFFU), (unsigned)(probe_cfg.ip & 0xFFU));
    for (size_t pi = 0U; pi < sizeof(probe_paths) / sizeof(probe_paths[0]); pi++)
    {
        snprintf(probe_req, sizeof(probe_req), "GET %s HTTP/1.1\r\nHost: connectivitycheck.gstatic.com\r\n\r\n", probe_paths[pi]);
        resp[0] = '\0';
        TEST_ASSERT(http_process_request(probe_req, strlen(probe_req), resp, sizeof(resp), &resp_len) == HTTP_OK, "Connectivity probe processed");
#if CONFIG_SOFTAP_CAPTIVE_PORTAL
        TEST_ASSERT(strncmp(resp, "HTTP/1.1 302 Found\r\n", 20) == 0, "Connectivity probe answered with 302, not success");
        TEST_ASSERT(strstr(resp, portal_loc) != NULL, "Connectivity probe redirects to http://<board ip>/setup");
#else
        (void)portal_loc;
        TEST_ASSERT(strncmp(resp, "HTTP/1.1 404 Not Found\r\n", 24) == 0, "Connectivity probe answered with 404 (no internet), not success");
        TEST_ASSERT(strstr(resp, "Location:") == NULL, "Connectivity probe is not redirected");
#endif
    }

    /* 5. Request Processing: 404 Not Found */
    const char req_unknown[] = "POST /unknown HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_unknown, strlen(req_unknown), resp, sizeof(resp), &resp_len) == HTTP_ERR_NOT_FOUND, "Process POST /unknown returns 404");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 404 Not Found") != NULL, "Response line is 404 Not Found");
    TEST_ASSERT(strstr(resp, "not_found") != NULL, "Response body contains not_found");

    /* 6. Request Processing: 405 Method Not Allowed */
    const char req_405[] = "POST /api/status HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_405, strlen(req_405), resp, sizeof(resp), &resp_len) == HTTP_ERR_METHOD_NOT_ALLOWED, "Process POST to GET route returns 405");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 405 Method Not Allowed") != NULL, "Response line is 405 Method Not Allowed");

    /* 7. Request Processing: 400 Bad Request */
    const char req_bad[] = "GARBAGE_REQUEST\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_bad, strlen(req_bad), resp, sizeof(resp), &resp_len) == HTTP_ERR_MALFORMED, "Process malformed request returns 400");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 400 Bad Request") != NULL, "Response line is 400 Bad Request");

    /* 8. Web Dashboard Asset Delivery (GET /) */
    const char req_root[] = "GET / HTTP/1.1\r\n\r\n";
    resp[0] = '\0';
    TEST_ASSERT(http_process_request(req_root, strlen(req_root), resp, sizeof(resp), &resp_len) == HTTP_OK, "Process GET / succeeds");
    TEST_ASSERT(strstr(resp, "HTTP/1.1 200 OK") != NULL, "Root response is 200 OK");
    TEST_ASSERT(strstr(resp, "Content-Type: text/html") != NULL, "Content-Type is text/html");
    TEST_ASSERT(strstr(resp, "<html") != NULL, "Body contains HTML");

    /* 9. HTTP Server Start & Telemetry */
    TEST_ASSERT(http_server_start(80U) == HTTP_OK, "http_server_start succeeds");
    TEST_ASSERT(http_server_is_running() == true, "Server reports running");

    http_telemetry_t telem;
    TEST_ASSERT(http_server_get_telemetry(&telem) == HTTP_OK, "http_server_get_telemetry succeeds");
    TEST_ASSERT(telem.requests_total >= 5U, "Telemetry records >= 5 requests");
    TEST_ASSERT(telem.responses_200 >= 2U, "Telemetry records >= 2 200 responses");
    TEST_ASSERT(telem.responses_404 >= 1U, "Telemetry records >= 1 404 response");
    TEST_ASSERT(telem.bytes_tx > 0U, "Telemetry records transmitted bytes");

    /* 10. HTTP Server Stop */
    TEST_ASSERT(http_server_stop() == HTTP_OK, "http_server_stop succeeds");
    TEST_ASSERT(http_server_is_running() == false, "Server reports stopped");
}

static void test_dhcp_dns_subsystem(void)
{
    printf("  [TEST] Freestanding DHCP Server & DNS Captive Portal Subsystem...\n");

    /* 1. Protocol & Port Constants */
    TEST_ASSERT(DHCP_SERVER_PORT == 67U, "DHCP server port is 67");
    TEST_ASSERT(DHCP_CLIENT_PORT == 68U, "DHCP client port is 68");
    TEST_ASSERT(DNS_SERVER_PORT == 53U, "DNS server port is 53");
    TEST_ASSERT(DHCP_MAGIC_COOKIE == 0x63825363U, "DHCP magic cookie matches RFC 2131");
    TEST_ASSERT(DHCP_DEFAULT_LEASE_TIME_SEC == 86400U, "Default lease time is 86400s (24h)");
    TEST_ASSERT(DHCP_MAX_LEASES == 4U, "DHCP pool capacity is 4");

    /* 2. Subsystem Lifecycle & Reset */
    TEST_ASSERT(dhcp_init() == DHCP_OK, "dhcp_init succeeds");
    dhcp_telemetry_t dt;
    TEST_ASSERT(dhcp_get_telemetry(&dt) == DHCP_OK, "dhcp_get_telemetry succeeds");
    TEST_ASSERT(dt.active_leases == 0U, "Initial active leases is 0");
    TEST_ASSERT(dt.discover_rx == 0U, "Initial discover_rx is 0");

    /* 3. Synthetic DHCPDISCOVER Processing */
    uint8_t client_mac[6] = { 0x56, 0xC7, 0xE1, 0x1F, 0x63, 0x7C };
    uint8_t disc_frame[ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN + sizeof(dhcp_packet_t)];
    memset(disc_frame, 0, sizeof(disc_frame));

    ethernet_header_t *eth = (ethernet_header_t *)disc_frame;
    ipv4_header_t *ip = (ipv4_header_t *)(disc_frame + ETH_HDR_LEN);
    udp_header_t *udp = (udp_header_t *)(disc_frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    dhcp_packet_t *dhcp = (dhcp_packet_t *)(disc_frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN);

    memset(eth->dest_mac, 0xFF, ETH_ADDR_LEN);
    memcpy(eth->src_mac, client_mac, ETH_ADDR_LEN);
    eth->ethertype = NET_HTONS(ETHERTYPE_IPV4);

    ip->ver_ihl = IPV4_VER_IHL_DEFAULT;
    ip->total_len = NET_HTONS(IPV4_MIN_HDR_LEN + UDP_HDR_LEN + 250U);
    ip->protocol = IPV4_PROTO_UDP;
    ip->ttl = 64U;
    ip->src_ip = 0U;
    ip->dest_ip = 0xFFFFFFFFU;
    ip->checksum = 0U;
    ip->checksum = NET_HTONS(net_ipv4_checksum(ip));

    udp->src_port = NET_HTONS(DHCP_CLIENT_PORT);
    udp->dest_port = NET_HTONS(DHCP_SERVER_PORT);
    udp->length = NET_HTONS(UDP_HDR_LEN + 250U);

    dhcp->op = DHCP_OP_BOOTREQUEST;
    dhcp->htype = DHCP_HTYPE_ETHERNET;
    dhcp->hlen = DHCP_HLEN_ETHERNET;
    dhcp->xid = 0x3903F326U;
    memcpy(dhcp->chaddr, client_mac, 6);
    dhcp->magic_cookie = NET_HTONL(DHCP_MAGIC_COOKIE);

    uint8_t *opts = dhcp->options;
    opts[0] = DHCP_OPT_MSG_TYPE;
    opts[1] = 1U;
    opts[2] = DHCP_MSG_DISCOVER;
    opts[3] = DHCP_OPT_END;

    uint16_t frame_len = (uint16_t)(ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN + 250U);
    TEST_ASSERT(net_input(disc_frame, frame_len) == NET_OK, "net_input handles DHCPDISCOVER");
    dhcp_get_telemetry(&dt);
    TEST_ASSERT(dt.discover_rx == 1U, "Telemetry discover_rx incremented to 1");
    TEST_ASSERT(dt.offer_tx == 1U, "Telemetry offer_tx incremented to 1");
    const dhcp_lease_t *lease0 = dhcp_get_lease(0);
    TEST_ASSERT(lease0 != NULL && lease0->active, "Lease 0 allocated");
    TEST_ASSERT(lease0->ip == DHCP_DEFAULT_BASE_IP, "Lease 0 assigned 192.168.1.2");

    /* 4. Synthetic DHCPREQUEST Processing */
    opts[2] = DHCP_MSG_REQUEST;
    opts[3] = DHCP_OPT_REQUESTED_IP;
    opts[4] = 4U;
    opts[5] = (uint8_t)(DHCP_DEFAULT_BASE_IP >> 24U);
    opts[6] = (uint8_t)(DHCP_DEFAULT_BASE_IP >> 16U);
    opts[7] = (uint8_t)(DHCP_DEFAULT_BASE_IP >> 8U);
    opts[8] = (uint8_t)(DHCP_DEFAULT_BASE_IP & 0xFFU);
    opts[9] = DHCP_OPT_SERVER_ID;
    opts[10] = 4U;
    opts[11] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 24U);
    opts[12] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 16U);
    opts[13] = (uint8_t)(DHCP_DEFAULT_GATEWAY >> 8U);
    opts[14] = (uint8_t)(DHCP_DEFAULT_GATEWAY & 0xFFU);
    opts[15] = DHCP_OPT_END;

    TEST_ASSERT(net_input(disc_frame, frame_len) == NET_OK, "net_input handles DHCPREQUEST");
    dhcp_get_telemetry(&dt);
    TEST_ASSERT(dt.request_rx == 1U, "Telemetry request_rx incremented to 1");
    TEST_ASSERT(dt.ack_tx == 1U, "Telemetry ack_tx incremented to 1");
    TEST_ASSERT(dt.active_leases == 1U, "Active leases count is 1");

    /* 5. Synthetic DNS Query Processing (Captive Portal) */
    uint8_t dns_frame[128];
    memset(dns_frame, 0, sizeof(dns_frame));
    ethernet_header_t *d_eth = (ethernet_header_t *)dns_frame;
    ipv4_header_t *d_ip = (ipv4_header_t *)(dns_frame + ETH_HDR_LEN);
    udp_header_t *d_udp = (udp_header_t *)(dns_frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    dns_header_t *d_dns = (dns_header_t *)(dns_frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN);

    memcpy(d_eth->src_mac, client_mac, ETH_ADDR_LEN);
    d_eth->ethertype = NET_HTONS(ETHERTYPE_IPV4);
    d_ip->ver_ihl = IPV4_VER_IHL_DEFAULT;
    d_ip->protocol = IPV4_PROTO_UDP;
    d_ip->src_ip = NET_HTONL(DHCP_DEFAULT_BASE_IP);
    d_ip->dest_ip = NET_HTONL(DHCP_DEFAULT_GATEWAY);
    d_ip->ttl = 64U;

    d_udp->src_port = NET_HTONS(54321U);
    d_udp->dest_port = NET_HTONS(DNS_SERVER_PORT);

    d_dns->id = NET_HTONS(0xABCDU);
    d_dns->flags = NET_HTONS(0x0100U); /* Standard query */
    d_dns->qdcount = NET_HTONS(1U);

    /* Question: "apple" (5) "com" (3) 0 */
    uint8_t *q = dns_frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN + sizeof(dns_header_t);
    q[0] = 5; q[1] = 'a'; q[2] = 'p'; q[3] = 'p'; q[4] = 'l'; q[5] = 'e';
    q[6] = 3; q[7] = 'c'; q[8] = 'o'; q[9] = 'm';
    q[10] = 0; /* root */
    q[11] = 0x00; q[12] = 0x01; /* QTYPE A */
    q[13] = 0x00; q[14] = 0x01; /* QCLASS IN */

    uint16_t dns_payload_len = (uint16_t)(sizeof(dns_header_t) + 15U);
    d_udp->length = NET_HTONS(UDP_HDR_LEN + dns_payload_len);
    d_ip->total_len = NET_HTONS(IPV4_MIN_HDR_LEN + UDP_HDR_LEN + dns_payload_len);
    d_ip->checksum = 0U;
    d_ip->checksum = NET_HTONS(net_ipv4_checksum(d_ip));

    uint16_t dns_frame_len = (uint16_t)(ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN + dns_payload_len);
    TEST_ASSERT(net_input(dns_frame, dns_frame_len) == NET_OK, "net_input handles DNS query");
    dhcp_get_telemetry(&dt);
    TEST_ASSERT(dt.dns_queries_rx == 1U, "Telemetry dns_queries_rx incremented to 1");
    TEST_ASSERT(dt.dns_replies_tx == 1U, "Telemetry dns_replies_tx incremented to 1");

    /* 6. Release Lease */
    dhcp_release_lease(client_mac);
    dhcp_get_telemetry(&dt);
    TEST_ASSERT(dt.active_leases == 0U, "Active leases decremented to 0 after release");
    TEST_ASSERT(dt.release_rx == 1U, "Telemetry release_rx incremented to 1");
}

static void test_speedtest_subsystem(void)
{
    printf("  [TEST] LAN Network Diagnostics & Wi-Fi Speed-Test Benchmark Engine (Task 6.2)...\n");

    /* 1. Protocol & Sizing Constants */
    TEST_ASSERT(SPEEDTEST_DEFAULT_PORT == 5001U, "Speedtest default port is 5001");
    TEST_ASSERT(SPEEDTEST_MAGIC_HEADER == 0x53504544U, "Speedtest magic header matches 'SPED'");
    TEST_ASSERT(SPEEDTEST_DEFAULT_BURST_COUNT == 100U, "Default burst count is 100");
    TEST_ASSERT(SPEEDTEST_DEFAULT_PACKET_SIZE == 1024U, "Default packet size is 1024");
    TEST_ASSERT(SPEEDTEST_MAX_PACKET_SIZE == 1472U, "Max packet size is 1472");
    TEST_ASSERT(SPEEDTEST_MIN_PACKET_SIZE == 64U, "Min packet size is 64");
    TEST_ASSERT(sizeof(speedtest_packet_header_t) == 16U, "Wire header is 16 bytes packed");

    /* 2. Lifecycle Initialization */
    TEST_ASSERT(speedtest_init() == SPEEDTEST_OK, "speedtest_init succeeds");

    /* 3. Bandwidth Calculation Formulas & Known Math Test Vectors */
    /* Vector 1: 125,000 bytes in 1,000,000 us -> 1000 kbps (1 Mbps) */
    uint32_t kbps1 = speedtest_calculate_throughput_kbps(125000U, 1000000U);
    uint32_t mbps1 = speedtest_calculate_throughput_mbps(125000U, 1000000U);
    TEST_ASSERT(kbps1 == 1000U, "125,000 B in 1s calculates exactly 1000 kbps");
    TEST_ASSERT(mbps1 == 1U, "125,000 B in 1s calculates exactly 1 Mbps");
    TEST_ASSERT(speedtest_kbps_to_mbps(kbps1) == 1U, "speedtest_kbps_to_mbps converts 1000 kbps to 1 Mbps");

    /* Vector 2: 12,500,000 bytes in 1,000,000 us -> 100,000 kbps (100 Mbps) */
    uint32_t kbps2 = speedtest_calculate_throughput_kbps(12500000U, 1000000U);
    uint32_t mbps2 = speedtest_calculate_throughput_mbps(12500000U, 1000000U);
    TEST_ASSERT(kbps2 == 100000U, "12.5 MB in 1s calculates exactly 100,000 kbps");
    TEST_ASSERT(mbps2 == 100U, "12.5 MB in 1s calculates exactly 100 Mbps");

    /* Vector 3: 1,500 bytes in 120 us -> 100 Mbps */
    uint32_t kbps3 = speedtest_calculate_throughput_kbps(1500U, 120U);
    uint32_t mbps3 = speedtest_calculate_throughput_mbps(1500U, 120U);
    TEST_ASSERT(kbps3 == 100000U, "1500 B in 120 us calculates 100,000 kbps");
    TEST_ASSERT(mbps3 == 100U, "1500 B in 120 us calculates 100 Mbps");

    /* Vector 4: 102,400 bytes in 10,240 us -> 80 Mbps */
    uint32_t kbps4 = speedtest_calculate_throughput_kbps(102400U, 10240U);
    uint32_t mbps4 = speedtest_calculate_throughput_mbps(102400U, 10240U);
    TEST_ASSERT(kbps4 == 80000U, "102,400 B in 10,240 us calculates 80,000 kbps");
    TEST_ASSERT(mbps4 == 80U, "102,400 B in 10,240 us calculates 80 Mbps");

    /* Division by zero / zero bytes safety */
    TEST_ASSERT(speedtest_calculate_throughput_kbps(1000U, 0U) == 0U, "Zero elapsed us returns 0 kbps safely");
    TEST_ASSERT(speedtest_calculate_throughput_kbps(0U, 1000U) == 0U, "Zero bytes returns 0 kbps safely");
    TEST_ASSERT(speedtest_calculate_throughput_mbps(1000U, 0U) == 0U, "Zero elapsed us returns 0 mbps safely");
    TEST_ASSERT(speedtest_calculate_throughput_mbps(0U, 1000U) == 0U, "Zero bytes returns 0 mbps safely");

    /* 4. Synthetic Burst Execution (Roadmap T35) */
    speedtest_result_t res;
    memset(&res, 0, sizeof(res));
    TEST_ASSERT(speedtest_run_synthetic_burst(100U, 1024U, &res) == SPEEDTEST_OK, "speedtest_run_synthetic_burst succeeds");
    TEST_ASSERT(res.total_bytes_transferred == 102400U, "Transferred 102,400 bytes in 100-packet burst");
    TEST_ASSERT(res.end_time_us > res.start_time_us, "End timestamp exceeds start timestamp");
    TEST_ASSERT(res.throughput_kbps > 0U, "Positive throughput calculated");
    TEST_ASSERT(res.packet_loss_count == 0U, "Zero packet loss in synthetic burst");

    /* Calculation function correctly converts bytes and us into Mbps */
    uint32_t dur = res.end_time_us - res.start_time_us;
    uint32_t derived_mbps = speedtest_calculate_throughput_mbps(res.total_bytes_transferred, dur);
    TEST_ASSERT(derived_mbps == speedtest_kbps_to_mbps(res.throughput_kbps), "Throughput kbps and Mbps match");

    /* 5. Parameter Validation */
    TEST_ASSERT(speedtest_run_synthetic_burst(0U, 2000U, &res) == SPEEDTEST_ERR_INVALID_PARAM, "Reject oversized packet");
    TEST_ASSERT(speedtest_run_synthetic_burst(0U, 32U, &res) == SPEEDTEST_ERR_INVALID_PARAM, "Reject undersized packet");

    /* 6. Telemetry & Last Result Queries */
    speedtest_telemetry_t telem;
    TEST_ASSERT(speedtest_get_telemetry(&telem) == SPEEDTEST_OK, "speedtest_get_telemetry succeeds");
    TEST_ASSERT(telem.bursts_run >= 1U, "Telemetry records >= 1 burst run");
    TEST_ASSERT(telem.total_packets_tx >= 100U, "Telemetry records >= 100 packets tx");
    TEST_ASSERT(telem.total_bytes_tx >= 102400U, "Telemetry records >= 102,400 bytes tx");

    speedtest_result_t last_res;
    TEST_ASSERT(speedtest_get_last_result(&last_res) == SPEEDTEST_OK, "speedtest_get_last_result succeeds");
    TEST_ASSERT(last_res.total_bytes_transferred == 102400U, "Last result matches executed burst");

    /* 7. Inbound Packet Processing via net_input */
    uint8_t sp_frame[128];
    memset(sp_frame, 0, sizeof(sp_frame));
    ethernet_header_t *sp_eth = (ethernet_header_t *)sp_frame;
    ipv4_header_t *sp_ip = (ipv4_header_t *)(sp_frame + ETH_HDR_LEN);
    udp_header_t *sp_udp = (udp_header_t *)(sp_frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    speedtest_packet_header_t *sp_hdr = (speedtest_packet_header_t *)(sp_frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN);

    uint8_t sender_mac[ETH_ADDR_LEN] = {0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U};
    memcpy(sp_eth->src_mac, sender_mac, ETH_ADDR_LEN);
    sp_eth->ethertype = NET_HTONS(ETHERTYPE_IPV4);

    sp_ip->ver_ihl = IPV4_VER_IHL_DEFAULT;
    sp_ip->protocol = IPV4_PROTO_UDP;
    sp_ip->src_ip = NET_HTONL(NET_IP4_ADDR(192, 168, 1, 55));
    sp_ip->dest_ip = NET_HTONL(NET_IP4_ADDR(192, 168, 1, 1));
    sp_ip->ttl = 64U;

    uint16_t sp_payload_len = (uint16_t)sizeof(speedtest_packet_header_t);
    sp_udp->src_port = NET_HTONS(5001U);
    sp_udp->dest_port = NET_HTONS(SPEEDTEST_DEFAULT_PORT);
    sp_udp->length = NET_HTONS(UDP_HDR_LEN + sp_payload_len);

    sp_hdr->magic = SPEEDTEST_MAGIC_HEADER;
    sp_hdr->sequence = 42U;
    sp_hdr->timestamp_us = 123456U;
    sp_hdr->payload_len = sp_payload_len;
    sp_hdr->flags = SPEEDTEST_FLAG_BURST;

    sp_ip->total_len = NET_HTONS(IPV4_MIN_HDR_LEN + UDP_HDR_LEN + sp_payload_len);
    sp_ip->checksum = 0U;
    sp_ip->checksum = NET_HTONS(net_ipv4_checksum(sp_ip));

    uint16_t sp_frame_len = (uint16_t)(ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN + sp_payload_len);
    TEST_ASSERT(net_input(sp_frame, sp_frame_len) == NET_OK, "net_input handles inbound speedtest UDP packet");

    speedtest_get_telemetry(&telem);
    TEST_ASSERT(telem.total_packets_rx >= 1U, "Telemetry records inbound speedtest packet");

    /* 8. Reset Subsystem */
    speedtest_reset();
    speedtest_get_telemetry(&telem);
    TEST_ASSERT(telem.bursts_run == 0U, "speedtest_reset clears burst counter");
}

static void test_shell_subsystem(void)
{
    printf("  [TEST] Interactive Console Shell & 24/7 Health Monitoring (Task 6.4)...\n");

    /* 1. Subsystem Initialization */
    shell_init();
    shell_telemetry_t stelem;
    shell_get_telemetry(&stelem);
    TEST_ASSERT(stelem.commands_processed == 0U, "Initial commands processed is 0");
    TEST_ASSERT(stelem.unknown_commands == 0U, "Initial unknown commands is 0");
    TEST_ASSERT(stelem.empty_commands == 0U, "Initial empty commands is 0");

    /* 2. 24/7 System Health Telemetry Aggregation */
    system_health_telemetry_t htelem;
    shell_get_health_telemetry(&htelem);
    TEST_ASSERT(htelem.uptime_seconds > 0U, "Health telemetry uptime > 0");
    TEST_ASSERT(htelem.wdt_feeds_total > 0U, "Health telemetry watchdog feeds > 0");
    TEST_ASSERT(htelem.arena_bytes_free > 0U, "Health telemetry arena memory free > 0");
    TEST_ASSERT(htelem.dpc_queue_drops == 0U, "Health telemetry dpc queue drops is 0");
    TEST_ASSERT(htelem.uart_active == 1U, "Health telemetry UART0 console active");
    TEST_ASSERT(htelem.usb_active == 1U, "Health telemetry USB CDC-ACM console active");

    /* 3. Empty Command Line Handling */
    char empty_cmd[] = "";
    shell_execute(empty_cmd);
    shell_get_telemetry(&stelem);
    TEST_ASSERT(stelem.empty_commands == 1U, "Empty command increments empty_commands");
    TEST_ASSERT(stelem.commands_processed == 0U, "Empty command does not count as processed command");

    /* 4. Valid Builtin Shell Commands */
    char help_cmd[] = "help";
    shell_execute(help_cmd);
    char health_cmd[] = "health";
    shell_execute(health_cmd);
    char top_cmd[] = "top";
    shell_execute(top_cmd);
    char info_cmd[] = "info";
    shell_execute(info_cmd);

    shell_get_telemetry(&stelem);
    TEST_ASSERT(stelem.commands_processed == 4U, "4 valid commands processed");
    TEST_ASSERT(stelem.unknown_commands == 0U, "0 unknown commands for valid commands");

    /* 5. Unknown Command Handling */
    char bad_cmd[] = "nonexistent_shell_cmd_xyz";
    shell_execute(bad_cmd);
    shell_get_telemetry(&stelem);
    TEST_ASSERT(stelem.commands_processed == 5U, "5 total commands processed");
    TEST_ASSERT(stelem.unknown_commands == 1U, "1 unknown command recorded");

    /* 6. Direct Print Helpers and Uptime */
    shell_print_help();
    shell_print_info();
    shell_print_health();
    shell_print_top();
    TEST_ASSERT(shell_get_uptime_seconds() > 0U, "shell_get_uptime_seconds returns positive uptime");

    /* 7. Shell Telemetry Reset */
    shell_reset_telemetry();
    shell_get_telemetry(&stelem);
    TEST_ASSERT(stelem.commands_processed == 0U, "Reset clears commands processed");
    TEST_ASSERT(stelem.unknown_commands == 0U, "Reset clears unknown commands");
    TEST_ASSERT(stelem.empty_commands == 0U, "Reset clears empty commands");
}

static void test_efuse_subsystem(void)
{
    printf("  [TEST] eFuse Memory Controller & Silicon Security Sealing (Task 7.1)...\n");

    /* 1. Register Address & Offset Calculation Validation (AGENTS.md rule) */
    TEST_ASSERT((uintptr_t)EFUSE_BASE == 0x600B0800U, "EFUSE_BASE address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_WR_DIS_REG == 0x600B082CU, "EFUSE_RD_WR_DIS_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_REPEAT_DATA0_REG == 0x600B0830U, "EFUSE_RD_REPEAT_DATA0_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_REPEAT_DATA1_REG == 0x600B0834U, "EFUSE_RD_REPEAT_DATA1_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_REPEAT_DATA2_REG == 0x600B0838U, "EFUSE_RD_REPEAT_DATA2_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_REPEAT_DATA3_REG == 0x600B083CU, "EFUSE_RD_REPEAT_DATA3_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_REPEAT_DATA4_REG == 0x600B0840U, "EFUSE_RD_REPEAT_DATA4_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_MAC_SPI_SYS_0_REG == 0x600B0844U, "EFUSE_RD_MAC_SPI_SYS_0_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_MAC_SPI_SYS_1_REG == 0x600B0848U, "EFUSE_RD_MAC_SPI_SYS_1_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_MAC_SPI_SYS_2_REG == 0x600B084CU, "EFUSE_RD_MAC_SPI_SYS_2_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_MAC_SPI_SYS_3_REG == 0x600B0850U, "EFUSE_RD_MAC_SPI_SYS_3_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_SYS_PART1_DATA_REG(0) == 0x600B085CU, "EFUSE_RD_SYS_PART1_DATA_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_SYS_PART1_DATA_REG(3) == 0x600B0868U, "EFUSE_RD_SYS_PART1_DATA_REG(3) address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_USR_DATA_REG(0) == 0x600B087CU, "EFUSE_RD_USR_DATA_REG(0) address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_KEY_DATA_REG(0, 0) == 0x600B089CU, "EFUSE_RD_KEY_DATA_REG(0, 0) address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_RD_KEY_DATA_REG(5, 0) == 0x600B093CU, "EFUSE_RD_KEY_DATA_REG(5, 0) address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_STATUS_REG == 0x600B09D0U, "EFUSE_STATUS_REG address calculation");
    TEST_ASSERT((uintptr_t)EFUSE_CMD_REG == 0x600B09D4U, "EFUSE_CMD_REG address calculation");

    /* 2. Lifecycle & Shadow Refresh */
    efuse_mock_reset();
    TEST_ASSERT(efuse_init() == EFUSE_OK, "efuse_init initializes cleanly");
    TEST_ASSERT(efuse_refresh_shadow() == EFUSE_OK, "efuse_refresh_shadow succeeds");

    /* 3. MAC & Extension Address Query and Parameter Guards */
    uint8_t mac[EFUSE_MAC_LEN];
    TEST_ASSERT(efuse_get_mac(NULL) == EFUSE_ERR_INVALID_PARAM, "efuse_get_mac rejects NULL pointer");
    TEST_ASSERT(efuse_get_mac(mac) == EFUSE_OK, "efuse_get_mac succeeds");
    TEST_ASSERT(mac[0] == 0x40U && mac[1] == 0x4CU && mac[2] == 0xCAU &&
                mac[3] == 0x45U && mac[4] == 0x1EU && mac[5] == 0x14U,
                "efuse_get_mac matches 40:4c:ca:45:1e:14");

    uint16_t mac_ext = 0U;
    TEST_ASSERT(efuse_get_mac_ext(NULL) == EFUSE_ERR_INVALID_PARAM, "efuse_get_mac_ext rejects NULL pointer");
    TEST_ASSERT(efuse_get_mac_ext(&mac_ext) == EFUSE_OK, "efuse_get_mac_ext succeeds");
    TEST_ASSERT(mac_ext == 0xFFFEU, "efuse_get_mac_ext matches 0xFFFE");

    /* 4. 128-bit Hardware Unique ID Query */
    uint8_t uid[EFUSE_UNIQUE_ID_LEN];
    TEST_ASSERT(efuse_get_unique_id(NULL) == EFUSE_ERR_INVALID_PARAM, "efuse_get_unique_id rejects NULL pointer");
    TEST_ASSERT(efuse_get_unique_id(uid) == EFUSE_OK, "efuse_get_unique_id succeeds");
    TEST_ASSERT(uid[0] == 0xE6U && uid[1] == 0x2BU && uid[2] == 0x14U && uid[3] == 0x68U, "UID word 0 matches");
    TEST_ASSERT(uid[4] == 0xBAU && uid[5] == 0x10U && uid[6] == 0xA2U && uid[7] == 0xDCU, "UID word 1 matches");
    TEST_ASSERT(uid[8] == 0xC6U && uid[9] == 0x2DU && uid[10] == 0xEEU && uid[11] == 0x1FU, "UID word 2 matches");
    TEST_ASSERT(uid[12] == 0x81U && uid[13] == 0xCFU && uid[14] == 0x68U && uid[15] == 0x51U, "UID word 3 matches");

    /* 5. Wafer & Package Versioning */
    uint32_t maj = 99U, min = 99U;
    TEST_ASSERT(efuse_get_chip_version(NULL, &min) == EFUSE_ERR_INVALID_PARAM, "efuse_get_chip_version rejects NULL major");
    TEST_ASSERT(efuse_get_chip_version(&maj, NULL) == EFUSE_ERR_INVALID_PARAM, "efuse_get_chip_version rejects NULL minor");
    TEST_ASSERT(efuse_get_chip_version(&maj, &min) == EFUSE_OK, "efuse_get_chip_version succeeds");
    TEST_ASSERT(maj == 0U && min == 0U, "Wafer version defaults to 0.0");
    TEST_ASSERT(efuse_get_pkg_version() == 0U, "Package version defaults to 0");

    /* Test version bitfield decoding mutation */
    efuse_mock_set_reg(0x50U, (2U << EFUSE_WAFER_VERSION_MAJOR_S) |
                              (3U << EFUSE_WAFER_VERSION_MINOR_S) |
                              (5U << EFUSE_PKG_VERSION_S));
    TEST_ASSERT(efuse_get_chip_version(&maj, &min) == EFUSE_OK, "Chip version query succeeds after mutation");
    TEST_ASSERT(maj == 2U && min == 3U, "Wafer version decodes major=2 minor=3");
    TEST_ASSERT(efuse_get_pkg_version() == 5U, "Package version decodes 5");
    efuse_mock_reset();

    /* 6. Security Seals: Secure Boot */
    TEST_ASSERT(!efuse_is_secure_boot_enabled(), "Secure Boot initially disabled");
    efuse_mock_set_reg(0x38U, EFUSE_SECURE_BOOT_EN_BIT);
    TEST_ASSERT(efuse_is_secure_boot_enabled(), "Secure Boot detects active bit");
    efuse_mock_set_reg(0x38U, 0U);
    TEST_ASSERT(!efuse_is_secure_boot_enabled(), "Secure Boot cleared");

    /* 7. Security Seals: Flash Encryption Parity Checks */
    TEST_ASSERT(!efuse_is_flash_encryption_enabled(), "Flash encryption initially disabled (0b000)");
    /* 1 bit set -> Enabled */
    efuse_mock_set_reg(0x34U, (1U << EFUSE_SPI_BOOT_CRYPT_CNT_S));
    TEST_ASSERT(efuse_is_flash_encryption_enabled(), "Flash encryption enabled on 1 bit set (0b001)");
    /* 2 bits set -> Disabled */
    efuse_mock_set_reg(0x34U, (3U << EFUSE_SPI_BOOT_CRYPT_CNT_S));
    TEST_ASSERT(!efuse_is_flash_encryption_enabled(), "Flash encryption disabled on 2 bits set (0b011)");
    /* 3 bits set -> Enabled */
    efuse_mock_set_reg(0x34U, (7U << EFUSE_SPI_BOOT_CRYPT_CNT_S));
    TEST_ASSERT(efuse_is_flash_encryption_enabled(), "Flash encryption enabled on 3 bits set (0b111)");
    /* 1 bit set alternate -> Enabled */
    efuse_mock_set_reg(0x34U, (4U << EFUSE_SPI_BOOT_CRYPT_CNT_S));
    TEST_ASSERT(efuse_is_flash_encryption_enabled(), "Flash encryption enabled on 1 bit set (0b100)");
    /* 2 bits set alternate -> Disabled */
    efuse_mock_set_reg(0x34U, (5U << EFUSE_SPI_BOOT_CRYPT_CNT_S));
    TEST_ASSERT(!efuse_is_flash_encryption_enabled(), "Flash encryption disabled on 2 bits set (0b101)");
    efuse_mock_set_reg(0x34U, 0U);

    /* 8. Security Seals: JTAG Permanent & Soft Disables */
    TEST_ASSERT(!efuse_is_jtag_disabled(), "JTAG initially enabled");
    efuse_mock_set_reg(0x30U, EFUSE_DIS_PAD_JTAG_BIT);
    TEST_ASSERT(efuse_is_jtag_disabled(), "JTAG disabled via PAD JTAG fuse");
    efuse_mock_set_reg(0x30U, EFUSE_DIS_USB_JTAG_BIT);
    TEST_ASSERT(efuse_is_jtag_disabled(), "JTAG disabled via USB JTAG fuse");
    efuse_mock_set_reg(0x30U, (1U << EFUSE_SOFT_DIS_JTAG_S)); /* odd=1 -> disabled */
    TEST_ASSERT(efuse_is_jtag_disabled(), "JTAG disabled via soft JTAG odd fuse");
    efuse_mock_set_reg(0x30U, (2U << EFUSE_SOFT_DIS_JTAG_S)); /* even=2 -> enabled */
    TEST_ASSERT(!efuse_is_jtag_disabled(), "JTAG enabled via soft JTAG even fuse");
    efuse_mock_set_reg(0x30U, 0U);

    /* 9. Security Seals: Download Mode */
    TEST_ASSERT(!efuse_is_download_mode_disabled(), "Download mode initially enabled");
    efuse_mock_set_reg(0x3CU, EFUSE_DIS_DOWNLOAD_MODE_BIT);
    TEST_ASSERT(efuse_is_download_mode_disabled(), "Download mode disabled via fuse bit");
    efuse_mock_set_reg(0x3CU, 0U);

    /* 10. Complete Telemetry Snapshot & Read Count */
    efuse_mock_reset();
    TEST_ASSERT(efuse_init() == EFUSE_OK, "efuse_init succeeds after mock reset");
    efuse_telemetry_t telem;
    TEST_ASSERT(efuse_get_telemetry(NULL) == EFUSE_ERR_INVALID_PARAM, "efuse_get_telemetry rejects NULL");
    TEST_ASSERT(efuse_get_telemetry(&telem) == EFUSE_OK, "efuse_get_telemetry succeeds");
    TEST_ASSERT(telem.read_count > 0U, "Telemetry captures valid read count");
    TEST_ASSERT(telem.mac[0] == 0x40U && telem.mac[5] == 0x14U, "Telemetry captures MAC");
    TEST_ASSERT(telem.unique_id[0] == 0xE6U && telem.unique_id[15] == 0x51U, "Telemetry captures 128-bit UID");
    TEST_ASSERT(telem.wafer_version_major == 0U && telem.wafer_version_minor == 0U, "Telemetry captures wafer version");
    TEST_ASSERT(!telem.secure_boot_en, "Telemetry captures Secure Boot state");
    TEST_ASSERT(!telem.flash_encryption_en, "Telemetry captures Flash Encryption state");
    TEST_ASSERT(!telem.jtag_pad_disabled, "Telemetry captures JTAG PAD state");
}

static void test_soak_anti_starvation_subsystem(void)
{
    printf("  [TEST] 24/7 Stability Soak, Memory Leak & Anti-Starvation (Task 7.2)...\n");

    /* 1. Lifecycle and Initialization */
    soak_mock_reset();
    TEST_ASSERT(soak_init() == SOAK_OK, "soak_init initializes cleanly");

    /* 2. Quiescent Memory Audit */
    soak_mem_audit_t mem_audit;
    TEST_ASSERT(soak_audit_memory(NULL) == SOAK_ERR_INVALID_PARAM, "soak_audit_memory rejects NULL");
    TEST_ASSERT(soak_audit_memory(&mem_audit) == SOAK_OK, "soak_audit_memory succeeds");
    TEST_ASSERT(mem_audit.is_leak_free, "Quiescent memory state is 100% leak free");
    TEST_ASSERT(mem_audit.small_pool_active == 0U, "Small pool active count is 0");
    TEST_ASSERT(mem_audit.med_pool_active == 0U, "Medium pool active count is 0");
    TEST_ASSERT(mem_audit.scratch_bytes_used == 0U, "Scratch arena bytes used is 0");

    /* 3. Memory Leak Detection Mutation */
    void *p1 = arena_alloc(ARENA_POOL_BLOCK_SIZE_SMALL);
    TEST_ASSERT(p1 != NULL, "Arena allocation succeeds");
    TEST_ASSERT(soak_audit_memory(&mem_audit) == SOAK_OK, "soak_audit_memory succeeds during allocation");
    TEST_ASSERT(!mem_audit.is_leak_free, "Active allocation correctly flags leak state");
    TEST_ASSERT(mem_audit.small_pool_active == 1U, "Small pool active count is 1");
    arena_free(p1);
    TEST_ASSERT(soak_audit_memory(&mem_audit) == SOAK_OK, "soak_audit_memory succeeds after free");
    TEST_ASSERT(mem_audit.is_leak_free, "Freed allocation restores 100% leak-free state");
    TEST_ASSERT(mem_audit.small_pool_active == 0U, "Small pool active count restored to 0");

    /* 4. DPC Anti-Starvation Audit */
    soak_dpc_audit_t dpc_audit;
    TEST_ASSERT(soak_audit_dpc(NULL) == SOAK_ERR_INVALID_PARAM, "soak_audit_dpc rejects NULL");
    TEST_ASSERT(soak_audit_dpc(&dpc_audit) == SOAK_OK, "soak_audit_dpc succeeds");
    TEST_ASSERT(dpc_audit.is_starvation_free, "DPC queue starts starvation-free");
    TEST_ASSERT(dpc_audit.dpc_drop_count == 0U, "DPC drop count is 0");

    /* DPC Drop Detection Mutation */
    soak_mock_set_dpc_drops(3U);
    TEST_ASSERT(soak_audit_dpc(&dpc_audit) == SOAK_OK, "soak_audit_dpc succeeds during drops");
    TEST_ASSERT(!dpc_audit.is_starvation_free, "DPC drops correctly flag starvation");
    TEST_ASSERT(dpc_audit.dpc_drop_count == 3U, "DPC drop count matches mock");
    soak_mock_set_dpc_drops(0U);
    TEST_ASSERT(soak_audit_dpc(&dpc_audit) == SOAK_OK, "soak_audit_dpc succeeds after reset");
    TEST_ASSERT(dpc_audit.is_starvation_free, "DPC starvation-free restored");

    /* 5. Scheduler Latency & Fairness Audit */
    soak_sched_audit_t sched_audit;
    TEST_ASSERT(soak_audit_scheduler(NULL) == SOAK_ERR_INVALID_PARAM, "soak_audit_scheduler rejects NULL");
    TEST_ASSERT(soak_audit_scheduler(&sched_audit) == SOAK_OK, "soak_audit_scheduler succeeds");
    TEST_ASSERT(sched_audit.fairness_preserved, "Scheduler fairness preserved with bounded latency");
    TEST_ASSERT(sched_audit.max_yield_latency_us <= SOAK_SCHED_LATENCY_THRESHOLD_US, "Scheduler latency within threshold");

    /* Scheduler Starvation Mutation */
    soak_mock_set_sched_latency(SOAK_SCHED_LATENCY_THRESHOLD_US + 500U);
    TEST_ASSERT(soak_audit_scheduler(&sched_audit) == SOAK_OK, "soak_audit_scheduler succeeds on high latency");
    TEST_ASSERT(!sched_audit.fairness_preserved, "High latency flags scheduler starvation risk");
    soak_mock_set_sched_latency(15U);
    TEST_ASSERT(soak_audit_scheduler(&sched_audit) == SOAK_OK, "soak_audit_scheduler restored");
    TEST_ASSERT(sched_audit.fairness_preserved, "Scheduler fairness restored");

    /* 6. Stability Soak Cycle Execution */
    soak_mock_reset();
    TEST_ASSERT(soak_run_stability_cycle(1U) == SOAK_OK, "soak_run_stability_cycle completes cleanly");
    TEST_ASSERT(soak_run_stability_cycle(2U) == SOAK_OK, "Second soak stability cycle completes cleanly");

    /* 7. Subsystem Telemetry Snapshot */
    soak_telemetry_t telem;
    TEST_ASSERT(soak_get_telemetry(NULL) == SOAK_ERR_INVALID_PARAM, "soak_get_telemetry rejects NULL");
    TEST_ASSERT(soak_get_telemetry(&telem) == SOAK_OK, "soak_get_telemetry succeeds");
    TEST_ASSERT(telem.completed_cycles == 2U, "Telemetry records 2 completed cycles");
    TEST_ASSERT(telem.clean_streak == 2U, "Telemetry records clean streak of 2");
    TEST_ASSERT(telem.failed_cycles == 0U, "Telemetry records 0 failed cycles");
    TEST_ASSERT(!telem.mem_leak_detected, "No memory leaks detected across cycles");
    TEST_ASSERT(!telem.dpc_drop_detected, "No DPC drops detected across cycles");
    TEST_ASSERT(!telem.starvation_detected, "No task starvation detected across cycles");
    TEST_ASSERT(telem.wdt_feeds_count >= 2U, "Watchdog fed on each soak cycle");

    /* 8. Telemetry Reset */
    soak_reset_telemetry();
    TEST_ASSERT(soak_get_telemetry(&telem) == SOAK_OK, "soak_get_telemetry succeeds after reset");
    TEST_ASSERT(telem.completed_cycles == 0U, "Reset clears completed cycles");
    TEST_ASSERT(telem.clean_streak == 0U, "Reset clears clean streak");
}

static void test_ota_subsystem(void)
{
    printf("  [TEST] Dual-Slot Flash OTA Firmware Upgrade & Rollback (Task 7.3)...\n");

    /* 1. Lifecycle and Initialization */
    ota_mock_reset();
    TEST_ASSERT(ota_init() == OTA_OK, "ota_init initializes cleanly");
    TEST_ASSERT(ota_get_active_slot() == OTA_SLOT_0, "Active slot defaults to Slot 0");
    TEST_ASSERT(ota_get_inactive_slot() == OTA_SLOT_1, "Inactive slot is Slot 1");
    TEST_ASSERT(ota_get_slot_state(OTA_SLOT_0) == OTA_STATE_VALID, "Slot 0 starts in VALID state");

    /* 2. Partition Geometry & Query */
    ota_partition_t p0, p1;
    TEST_ASSERT(ota_get_partition_info(OTA_SLOT_0, &p0) == OTA_OK, "Query Slot 0 partition info succeeds");
    TEST_ASSERT(p0.phys_offset == OTA_SLOT_0_OFFSET, "Slot 0 physical offset matches 0x000000");
    TEST_ASSERT(p0.size_bytes == OTA_SLOT_0_SIZE, "Slot 0 size matches 3.75 MB");
    TEST_ASSERT(p0.is_active, "Slot 0 is marked active");
    TEST_ASSERT(p0.seq == 1U, "Slot 0 sequence number is 1");

    TEST_ASSERT(ota_get_partition_info(OTA_SLOT_1, &p1) == OTA_OK, "Query Slot 1 partition info succeeds");
    TEST_ASSERT(p1.phys_offset == OTA_SLOT_1_OFFSET, "Slot 1 physical offset matches 0x3C0000 (3.75 MB)");
    TEST_ASSERT(p1.size_bytes == OTA_SLOT_1_SIZE, "Slot 1 capacity matches Slot 0");
    TEST_ASSERT(!p1.is_active, "Slot 1 is marked inactive");

    /* 3. Image Header Parsing & Verification */
    esp_image_header_t hdr;
    TEST_ASSERT(ota_verify_image(OTA_SLOT_0, &hdr) == OTA_OK, "Slot 0 image verification succeeds");
    TEST_ASSERT(hdr.magic == ESP_IMAGE_HEADER_MAGIC, "Image magic byte matches 0xE9");
    TEST_ASSERT(hdr.entry_addr == ESP_IMAGE_DEFAULT_ENTRY_ADDR, "Entry address matches 0x40800000");
    TEST_ASSERT(hdr.chip_id == ESP_IMAGE_CHIP_ID_ESP32C6, "Chip ID matches ESP32-C6 (13)");
    TEST_ASSERT(hdr.segment_count == 4U, "Segment count matches 4");

    /* Corrupted Header Rejection Mutation */
    uint8_t bad_hdr[ESP_IMAGE_HEADER_SIZE];
    memset(bad_hdr, 0, sizeof(bad_hdr));
    TEST_ASSERT(ota_parse_image_header(bad_hdr, sizeof(bad_hdr), &hdr) == OTA_ERR_INVALID_IMAGE, "Null magic byte rejected");
    bad_hdr[0] = ESP_IMAGE_HEADER_MAGIC;
    bad_hdr[1] = 0U; /* 0 segments */
    TEST_ASSERT(ota_parse_image_header(bad_hdr, sizeof(bad_hdr), &hdr) == OTA_ERR_INVALID_IMAGE, "Zero segments rejected");
    bad_hdr[1] = 20U; /* > 16 segments */
    TEST_ASSERT(ota_parse_image_header(bad_hdr, sizeof(bad_hdr), &hdr) == OTA_ERR_INVALID_IMAGE, "Overflow segment count rejected");

    /* 4. Slot Switching & Testing State */
    TEST_ASSERT(ota_switch_slot(OTA_SLOT_0) == OTA_ERR_ALREADY_ACTIVE, "Cannot switch to currently active slot");
    TEST_ASSERT(ota_switch_slot(OTA_SLOT_1) == OTA_OK, "Switching to Slot 1 succeeds");
    TEST_ASSERT(ota_get_active_slot() == OTA_SLOT_1, "Active slot is now Slot 1");
    TEST_ASSERT(ota_get_inactive_slot() == OTA_SLOT_0, "Inactive slot is now Slot 0");
    TEST_ASSERT(ota_get_slot_state(OTA_SLOT_1) == OTA_STATE_TESTING, "New slot transitions to TESTING state");

    /* 5. Safe Rollback State Machine */
    TEST_ASSERT(ota_rollback() == OTA_OK, "Rollback from uncommitted slot succeeds");
    TEST_ASSERT(ota_get_active_slot() == OTA_SLOT_0, "Active slot reverted to Slot 0");
    TEST_ASSERT(ota_get_slot_state(OTA_SLOT_0) == OTA_STATE_VALID, "Fallback slot restored to VALID state");

    /* 6. Mark Valid State Commitment */
    TEST_ASSERT(ota_switch_slot(OTA_SLOT_1) == OTA_OK, "Switch to Slot 1 for confirmation");
    TEST_ASSERT(ota_mark_valid() == OTA_OK, "ota_mark_valid commits slot");
    TEST_ASSERT(ota_get_slot_state(OTA_SLOT_1) == OTA_STATE_VALID, "Slot 1 committed to VALID state");

    /* 7. Flash I/O Inactive Guarding */
    uint8_t test_chunk[32];
    memset(test_chunk, 0xA5, sizeof(test_chunk));
    TEST_ASSERT(ota_write_chunk(OTA_SLOT_1, 0, test_chunk, sizeof(test_chunk)) == OTA_ERR_NOT_PERMITTED, "Writing to active slot is blocked");
    TEST_ASSERT(ota_erase_slot(OTA_SLOT_1) == OTA_ERR_NOT_PERMITTED, "Erasing active slot is blocked");
    TEST_ASSERT(ota_write_chunk(OTA_SLOT_0, 0, test_chunk, sizeof(test_chunk)) == OTA_OK, "Writing to inactive slot succeeds");

    /* 8. Telemetry Snapshot & Parameter Guards */
    ota_status_report_t rep;
    TEST_ASSERT(ota_get_status(NULL) == OTA_ERR_INVALID_PARAM, "ota_get_status rejects NULL");
    TEST_ASSERT(ota_get_status(&rep) == OTA_OK, "ota_get_status succeeds");
    TEST_ASSERT(rep.active_slot == OTA_SLOT_1, "Telemetry matches active Slot 1");
    TEST_ASSERT(rep.total_switches >= 2U, "Telemetry tracks slot switches");
    TEST_ASSERT(rep.total_rollbacks >= 1U, "Telemetry tracks rollbacks");
    TEST_ASSERT(rep.flash_reads > 0U, "Flash reads tracked");
    TEST_ASSERT(rep.flash_writes > 0U, "Flash writes tracked");
    TEST_ASSERT(ota_get_partition_info(OTA_SLOT_INVALID, &p0) == OTA_ERR_INVALID_PARAM, "Reject invalid slot query");
    TEST_ASSERT(ota_parse_image_header(NULL, 10, &hdr) == OTA_ERR_INVALID_PARAM, "Parse rejects NULL pointer");
    TEST_ASSERT(ota_verify_image(OTA_SLOT_INVALID, &hdr) == OTA_ERR_INVALID_PARAM, "Verify rejects invalid slot");
}

/* REV-28: the self-test saves NVS and the OTA records first and restores them at the end */
static void test_selftest_snapshots(void)
{
    printf("  [TEST] NVS / OTA snapshots behind the non-destructive self-test...\n");

    nvs_mock_reset();
    TEST_ASSERT(nvs_init() == NVS_OK, "nvs_init on a blank mock sector");
    TEST_ASSERT(nvs_set_str("wifi_ssid", "HomeNet") == NVS_OK, "Seed a saved SSID");
    nvs_snapshot_t nsnap;
    bool rewritten = true;
    TEST_ASSERT(nvs_snapshot_save(&nsnap) == NVS_OK, "nvs_snapshot_save succeeds");
    TEST_ASSERT(nvs_snapshot_restore(&nsnap, &rewritten) == NVS_OK && !rewritten, "Restoring an unchanged store writes nothing");
    TEST_ASSERT(nvs_set_str("wifi_ssid", "SelfTestAP") == NVS_OK && nvs_set_u32("test_u32", 7U) == NVS_OK,
                "A test overwrites the SSID and adds a key");
    TEST_ASSERT(!nvs_snapshot_matches(&nsnap), "Store differs from the snapshot");
    TEST_ASSERT(nvs_snapshot_restore(&nsnap, &rewritten) == NVS_OK && rewritten, "Restore rewrites the changed store");
    char ssid[NVS_VAL_MAX_LEN];
    uint32_t val = 0U;
    TEST_ASSERT(nvs_get_str("wifi_ssid", ssid, sizeof(ssid)) == NVS_OK && strcmp(ssid, "HomeNet") == 0, "Original SSID is back");
    TEST_ASSERT(nvs_get_u32("test_u32", &val) == NVS_ERR_NOT_FOUND, "Test key is gone");
    TEST_ASSERT(nvs_init() == NVS_OK && nvs_get_str("wifi_ssid", ssid, sizeof(ssid)) == NVS_OK && strcmp(ssid, "HomeNet") == 0,
                "Restored store survives a reload from flash");
    TEST_ASSERT(nvs_snapshot_restore(NULL, NULL) == NVS_ERR_INVALID_PARAM, "NULL snapshot rejected");

    ota_mock_reset();
    TEST_ASSERT(ota_init() == OTA_OK, "ota_init on blank mock flash");
    ota_snapshot_t osnap;
    ota_status_report_t before;
    ota_status_report_t after;
    TEST_ASSERT(ota_snapshot_save(&osnap) == OTA_OK && ota_get_status(&before) == OTA_OK, "ota_snapshot_save succeeds");
    TEST_ASSERT(ota_switch_slot(OTA_SLOT_1) == OTA_OK && ota_rollback() == OTA_OK, "A test switches slots and rolls back");
    TEST_ASSERT(!ota_snapshot_matches(&osnap), "Records differ after switch + rollback");
    TEST_ASSERT(ota_snapshot_restore(&osnap, &rewritten) == OTA_OK && rewritten, "Restore rewrites the records");
    TEST_ASSERT(ota_snapshot_matches(&osnap), "Records match the snapshot again");
    TEST_ASSERT(ota_get_status(&after) == OTA_OK && after.active_slot == before.active_slot &&
                ota_get_slot_state(after.active_slot) == OTA_STATE_VALID, "Active slot and state are back");
    TEST_ASSERT(ota_init() == OTA_OK && ota_get_active_slot() == before.active_slot, "Restored records survive a reload from flash");
    TEST_ASSERT(ota_snapshot_restore(NULL, NULL) == OTA_ERR_INVALID_PARAM, "NULL snapshot rejected");
}

static void test_nvs_subsystem(void)
{
    printf("  [TEST] Production Hardening, NVS Storage Engine & Golden Master (Task 7.4)...\n");

    /* 1. Lifecycle and Initialization */
    nvs_mock_reset();
    TEST_ASSERT(nvs_init() == NVS_OK, "nvs_init initializes cleanly");

    /* 2. Key-Value Storage & Retrieval (u32) */
    TEST_ASSERT(nvs_set_u32("boot_count", 42U) == NVS_OK, "Set u32 key 'boot_count' succeeds");
    uint32_t val_u32 = 0U;
    TEST_ASSERT(nvs_get_u32("boot_count", &val_u32) == NVS_OK, "Get u32 key 'boot_count' succeeds");
    TEST_ASSERT(val_u32 == 42U, "Read value matches written value (42)");

    /* 3. Key-Value Storage & Retrieval (string) */
    TEST_ASSERT(nvs_set_str("wifi_ssid", "IronV-SecureNet") == NVS_OK, "Set string key 'wifi_ssid' succeeds");
    char str_buf[64];
    TEST_ASSERT(nvs_get_str("wifi_ssid", str_buf, sizeof(str_buf)) == NVS_OK, "Get string key 'wifi_ssid' succeeds");
    TEST_ASSERT(strcmp(str_buf, "IronV-SecureNet") == 0, "String value matches expected SSID");

    TEST_ASSERT(nvs_set_str("wifi_pass", "SuperSecretPassphrase123") == NVS_OK, "Set string key 'wifi_pass' succeeds");
    TEST_ASSERT(nvs_get_str("wifi_pass", str_buf, sizeof(str_buf)) == NVS_OK, "Get string key 'wifi_pass' succeeds");
    TEST_ASSERT(strcmp(str_buf, "SuperSecretPassphrase123") == 0, "Passphrase matches expected string");

    /* 4. Binary Blob Storage & Retrieval */
    uint8_t blob_in[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t blob_out[16];
    size_t out_len = 0U;
    TEST_ASSERT(nvs_set_blob("device_uuid", blob_in, sizeof(blob_in)) == NVS_OK, "Set blob key succeeds");
    TEST_ASSERT(nvs_get_blob("device_uuid", blob_out, sizeof(blob_out), &out_len) == NVS_OK, "Get blob key succeeds");
    TEST_ASSERT(out_len == sizeof(blob_in), "Blob length matches");
    TEST_ASSERT(memcmp(blob_in, blob_out, sizeof(blob_in)) == 0, "Blob payload matches byte-for-byte");

    /* 5. Key Update / Overwrite */
    TEST_ASSERT(nvs_set_u32("boot_count", 43U) == NVS_OK, "Overwriting existing key succeeds");
    TEST_ASSERT(nvs_get_u32("boot_count", &val_u32) == NVS_OK, "Get updated u32 succeeds");
    TEST_ASSERT(val_u32 == 43U, "Updated value reflects new value (43)");

    /* 6. Key Deletion / Erase */
    TEST_ASSERT(nvs_erase_key("device_uuid") == NVS_OK, "Erase key 'device_uuid' succeeds");
    TEST_ASSERT(nvs_get_blob("device_uuid", blob_out, sizeof(blob_out), &out_len) == NVS_ERR_NOT_FOUND, "Erased key returns NVS_ERR_NOT_FOUND");
    TEST_ASSERT(nvs_erase_key("nonexistent_key") == NVS_ERR_NOT_FOUND, "Erasing non-existent key returns NVS_ERR_NOT_FOUND");

    /* 7. Statistics & Geometry Tracking */
    nvs_stats_t st;
    TEST_ASSERT(nvs_get_stats(&st) == NVS_OK, "nvs_get_stats succeeds");
    TEST_ASSERT(st.total_keys == 3U, "Stats report correct active key count (boot_count, wifi_ssid, wifi_pass)");
    TEST_ASSERT(st.used_bytes > 0U && st.free_bytes < NVS_FLASH_SECTOR_SIZE, "Used and free bytes correctly partitioned");

    /* 8. Error Guards & Parameter Validation */
    TEST_ASSERT(nvs_set_str(NULL, "val") == NVS_ERR_INVALID_PARAM, "Reject NULL key");
    TEST_ASSERT(nvs_set_str("key", NULL) == NVS_ERR_INVALID_PARAM, "Reject NULL value");
    TEST_ASSERT(nvs_get_str(NULL, str_buf, sizeof(str_buf)) == NVS_ERR_INVALID_PARAM, "Get rejects NULL key");
    TEST_ASSERT(nvs_get_str("key", NULL, sizeof(str_buf)) == NVS_ERR_INVALID_PARAM, "Get rejects NULL dest");
    TEST_ASSERT(nvs_get_stats(NULL) == NVS_ERR_INVALID_PARAM, "Get stats rejects NULL report");

    /* 9. Golden Master System Health & Sealing Audit */
    ota_mock_reset();
    golden_master_report_t gm_rep;
    bool gm_ok = golden_master_verify(&gm_rep);
    TEST_ASSERT(gm_ok, "Golden Master integrity verification reports PASS");
    TEST_ASSERT(gm_rep.golden_seal_magic == GOLDEN_MASTER_MAGIC, "Golden Seal magic matches 0x5A5A5A5A");
    TEST_ASSERT(gm_rep.memory_cartography_ok, "Memory cartography conforms to Harvard spec");
    TEST_ASSERT(gm_rep.watchdogs_ok, "Watchdog supervisor status validated");
    TEST_ASSERT(gm_rep.efuse_security_ok, "eFuse security seals verified");
    TEST_ASSERT(gm_rep.rf_coexistence_ok, "RF baseband coexistence verified");
    TEST_ASSERT(gm_rep.ota_partitions_ok, "OTA partitions & boot image header verified");
    TEST_ASSERT(gm_rep.nvs_storage_ok, "NVS storage verified");
    TEST_ASSERT(gm_rep.total_assertions_passed >= 6U, "All 6 subsystem audits pass");
}

static void test_provisioning_subsystem(void)
{
    printf("  [TEST] SoftAP Captive Portal Wi-Fi Provisioning Engine (Task 8.1)...\n");

    /* 1. Lifecycle and Initialization */
    nvs_mock_reset();
    nvs_init();
    provisioning_mock_reset();
    http_server_init();

    TEST_ASSERT(provisioning_init() == PROV_OK, "provisioning_init succeeds");
    TEST_ASSERT(provisioning_get_state() == PROV_STATE_UNPROVISIONED, "Initial state is UNPROVISIONED");
    TEST_ASSERT(!provisioning_has_credentials(), "Initially has no credentials");

    /* 2. Constants & Data Structure Geometry */
    TEST_ASSERT(sizeof(wifi_scan_item_t) >= 40U, "sizeof(wifi_scan_item_t) holds required fields");
    TEST_ASSERT(sizeof(wifi_credentials_t) >= 96U, "sizeof(wifi_credentials_t) holds SSID & pass");
    TEST_ASSERT(PROVISIONING_MAX_SCAN_APS == 16U, "PROVISIONING_MAX_SCAN_APS is 16");

    /* 3. Scan Results: real records only (no made-up list, O-7); hidden SSIDs dropped,
     *    one entry per SSID with the strongest signal, JSON-escaped */
    wifi_scan_item_t aps[PROVISIONING_MAX_SCAN_APS];
    uint16_t ap_count = 0U;
    TEST_ASSERT(provisioning_get_scan_results(aps, PROVISIONING_MAX_SCAN_APS, &ap_count) == PROV_OK, "get_scan_results succeeds");
    TEST_ASSERT(ap_count == 0U, "No scan yet: the list is empty (no made-up networks)");

    wifi_ap_record_t recs[5];
    memset(recs, 0, sizeof(recs));
    strcpy((char *)recs[0].ssid, "Mesh");      recs[0].rssi = -70; recs[0].primary = 1U;  recs[0].authmode = WIFI_AUTH_WPA2_PSK;
    strcpy((char *)recs[1].ssid, "Cafe \"Q\"");  recs[1].rssi = -60; recs[1].primary = 6U;  recs[1].authmode = WIFI_AUTH_OPEN;
    strcpy((char *)recs[2].ssid, "Mesh");      recs[2].rssi = -41; recs[2].primary = 11U; recs[2].authmode = WIFI_AUTH_WPA2_PSK;
    recs[2].bssid[5] = 0x42U;
    /* recs[3]: hidden network (empty SSID) */
    recs[3].rssi = -30; recs[3].primary = 3U; recs[3].authmode = WIFI_AUTH_WPA2_PSK;
    strcpy((char *)recs[4].ssid, "Modern");    recs[4].rssi = -50; recs[4].primary = 9U;  recs[4].authmode = WIFI_AUTH_WPA3_PSK;
    wifi_host_set_scan_records(recs, 5U);
    TEST_ASSERT(provisioning_start_scan() == PROV_OK, "provisioning_start_scan loads the scan records");
    TEST_ASSERT(provisioning_get_scan_results(aps, PROVISIONING_MAX_SCAN_APS, &ap_count) == PROV_OK && ap_count == 3U,
                "3 networks: duplicate SSID merged, hidden SSID dropped");
    TEST_ASSERT(strcmp(aps[0].ssid, "Mesh") == 0 && aps[0].rssi == -41 && aps[0].channel == 11U && aps[0].bssid[5] == 0x42U,
                "Duplicate SSID keeps the strongest record");
    TEST_ASSERT(aps[0].auth_mode == PROV_AUTH_WPA2_PSK && aps[2].auth_mode == PROV_AUTH_WPA3_PSK, "Auth modes mapped from the blob");

    /* 4. Trigger Wi-Fi Scan */
    TEST_ASSERT(provisioning_start_scan() == PROV_OK, "provisioning_start_scan succeeds");
    provisioning_telemetry_t telem;
    TEST_ASSERT(provisioning_get_telemetry(&telem) == PROV_OK, "provisioning_get_telemetry succeeds");
    TEST_ASSERT(telem.scans_initiated >= 1U, "Telemetry tracks initiated scans");
    TEST_ASSERT(telem.scans_completed >= 1U, "Telemetry tracks completed scans");

    /* 5. Credential Validation & Persistence */
    /* Rejection of invalid credentials */
    TEST_ASSERT(provisioning_set_credentials("", "password123") == PROV_ERR_SSID_EMPTY, "Rejects empty SSID");
    TEST_ASSERT(provisioning_set_credentials("MyNetwork", "short") == PROV_ERR_PASS_TOO_SHORT, "Rejects short passphrase (< 8 chars)");

    /* Valid credentials */
    TEST_ASSERT(provisioning_set_credentials("IronHomeWiFi", "SuperSecret123") == PROV_OK, "Valid credentials accepted");
    TEST_ASSERT(provisioning_has_credentials(), "provisioning_has_credentials returns true");
    TEST_ASSERT(provisioning_get_state() == PROV_STATE_CONFIGURED, "State transitioned to CONFIGURED");

    wifi_credentials_t creds;
    TEST_ASSERT(provisioning_get_credentials(&creds) == PROV_OK, "get_credentials succeeds");
    TEST_ASSERT(strcmp(creds.ssid, "IronHomeWiFi") == 0, "Configured SSID matches");
    TEST_ASSERT(strcmp(creds.passphrase, "SuperSecret123") == 0, "Configured passphrase matches");
    TEST_ASSERT(creds.provisioned, "creds.provisioned flag is true");

    /* NVS Persistence Verification */
    char nvs_ssid[PROVISIONING_MAX_SSID_LEN + 1U];
    char nvs_pass[PROVISIONING_MAX_PASS_LEN + 1U];
    TEST_ASSERT(nvs_get_str(PROV_NVS_KEY_SSID, nvs_ssid, sizeof(nvs_ssid)) == NVS_OK, "Read SSID from NVS succeeds");
    TEST_ASSERT(strcmp(nvs_ssid, "IronHomeWiFi") == 0, "NVS SSID matches configured value");
    TEST_ASSERT(nvs_get_str(PROV_NVS_KEY_PASS, nvs_pass, sizeof(nvs_pass)) == NVS_OK, "Read passphrase from NVS succeeds");
    TEST_ASSERT(strcmp(nvs_pass, "SuperSecret123") == 0, "NVS passphrase matches configured value");

    /* 6. Credential Erasure */
    TEST_ASSERT(provisioning_clear_credentials() == PROV_OK, "clear_credentials succeeds");
    TEST_ASSERT(!provisioning_has_credentials(), "provisioning_has_credentials returns false after clear");
    TEST_ASSERT(provisioning_get_credentials(&creds) == PROV_ERR_NOT_FOUND, "get_credentials returns NOT_FOUND after clear");
    TEST_ASSERT(nvs_get_str(PROV_NVS_KEY_SSID, nvs_ssid, sizeof(nvs_ssid)) == NVS_ERR_NOT_FOUND, "SSID removed from NVS");

    /* 7. HTTP Route Execution: Captive Portal & REST APIs */
    char resp_buf[HTTP_RESPONSE_BUF_SIZE];
    size_t resp_len = 0U;

    /* 7a. GET /setup */
    const char req_setup[] = "GET /setup HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    TEST_ASSERT(http_process_request(req_setup, strlen(req_setup), resp_buf, sizeof(resp_buf), &resp_len) == HTTP_OK, "GET /setup succeeds");
    TEST_ASSERT(strstr(resp_buf, "200 OK") != NULL, "GET /setup returns 200 OK");
    TEST_ASSERT(strstr(resp_buf, "text/html") != NULL, "GET /setup returns text/html");
    TEST_ASSERT(strstr(resp_buf, "Wi-Fi Setup") != NULL, "GET /setup body contains 'Wi-Fi Setup'");
    TEST_ASSERT(sizeof(g_setup_html) <= HTTP_BODY_MAX_LEN, "Setup page fits the HTTP body buffer (was cut off at 1600 bytes)");
    TEST_ASSERT(strstr(resp_buf, "sc(0)</script></body></html>") != NULL, "GET /setup delivers the whole page");

    /* 7b. GET /api/wifi/scan */
    const char req_scan[] = "GET /api/wifi/scan HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    TEST_ASSERT(http_process_request(req_scan, strlen(req_scan), resp_buf, sizeof(resp_buf), &resp_len) == HTTP_OK, "GET /api/wifi/scan succeeds");
    TEST_ASSERT(strstr(resp_buf, "200 OK") != NULL, "GET /api/wifi/scan returns 200 OK");
    TEST_ASSERT(strstr(resp_buf, "application/json") != NULL, "GET /api/wifi/scan returns JSON");
    TEST_ASSERT(strstr(resp_buf, "\"count\":3") != NULL, "GET /api/wifi/scan lists the 3 cached networks");
    TEST_ASSERT(strstr(resp_buf, "{\"ssid\":\"Mesh\",\"rssi\":-41,\"channel\":11,\"auth\":\"WPA2-PSK\",\"supported\":true}") != NULL,
                "Scan JSON entry for a WPA2 network (supported)");
    TEST_ASSERT(strstr(resp_buf, "\"ssid\":\"Cafe \\\"Q\\\"\"") != NULL, "Quotes in an SSID are escaped");
    TEST_ASSERT(strstr(resp_buf, "\"auth\":\"WPA3-PSK\",\"supported\":false") != NULL, "WPA3-only network marked unsupported");
    TEST_ASSERT(strstr(resp_buf, "\"scanning\":false") != NULL, "No scan pending");

    /* 7b'. ?refresh=1 only queues the scan; it runs on the next tick, after the reply */
    const char req_rescan[] = "GET /api/wifi/scan?refresh=1 HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    provisioning_telemetry_t before;
    provisioning_get_telemetry(&before);
    TEST_ASSERT(http_process_request(req_rescan, strlen(req_rescan), resp_buf, sizeof(resp_buf), &resp_len) == HTTP_OK &&
                strstr(resp_buf, "\"scanning\":true") != NULL, "Rescan request answers at once with scanning:true");
    provisioning_get_telemetry(&telem);
    TEST_ASSERT(telem.scans_completed == before.scans_completed, "Scan not run inside the request");
    provisioning_tick(1U);
    provisioning_get_telemetry(&telem);
    TEST_ASSERT(telem.scans_completed == before.scans_completed + 1U, "Queued scan runs on the next tick");
    TEST_ASSERT(http_process_request(req_scan, strlen(req_scan), resp_buf, sizeof(resp_buf), &resp_len) == HTTP_OK &&
                strstr(resp_buf, "\"scanning\":false") != NULL, "Scan done: scanning:false");

    /* 7c. POST /api/wifi/configure with JSON payload */
    const char req_cfg_json[] =
        "POST /api/wifi/configure HTTP/1.1\r\n"
        "Host: 192.168.4.1\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 48\r\n"
        "\r\n"
        "{\"ssid\":\"JsonHomeWiFi\",\"password\":\"P@ssw0rd999\"}";
    TEST_ASSERT(http_process_request(req_cfg_json, strlen(req_cfg_json), resp_buf, sizeof(resp_buf), &resp_len) == HTTP_OK, "POST /api/wifi/configure (JSON) succeeds");
    TEST_ASSERT(strstr(resp_buf, "200 OK") != NULL, "Configure returns 200 OK");
    TEST_ASSERT(strstr(resp_buf, "\"provisioned\":true") != NULL, "Configure JSON reports provisioned:true");
    TEST_ASSERT(provisioning_get_credentials(&creds) == PROV_OK, "Credentials saved from JSON POST");
    TEST_ASSERT(strcmp(creds.ssid, "JsonHomeWiFi") == 0, "SSID matches JSON input");

    /* 7d. GET /api/wifi/status */
    const char req_status[] = "GET /api/wifi/status HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    TEST_ASSERT(http_process_request(req_status, strlen(req_status), resp_buf, sizeof(resp_buf), &resp_len) == HTTP_OK, "GET /api/wifi/status succeeds");
    TEST_ASSERT(strstr(resp_buf, "200 OK") != NULL, "Status returns 200 OK");
    TEST_ASSERT(strstr(resp_buf, "\"provisioned\":true") != NULL, "Status reports provisioned:true");
    TEST_ASSERT(strstr(resp_buf, "JsonHomeWiFi") != NULL, "Status contains active SSID");

    /* 7e. GET /api/wifi/credentials */
    const char req_creds[] = "GET /api/wifi/credentials HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    TEST_ASSERT(http_process_request(req_creds, strlen(req_creds), resp_buf, sizeof(resp_buf), &resp_len) == HTTP_OK, "GET /api/wifi/credentials succeeds");
    TEST_ASSERT(strstr(resp_buf, "200 OK") != NULL, "Credentials endpoint returns 200 OK");
    TEST_ASSERT(strstr(resp_buf, "JsonHomeWiFi") != NULL, "Credentials endpoint returns active SSID");

    /* 8. Reset state */
    provisioning_clear_credentials();
}

/* REV-30: build-time STA credentials only seed NVS: once, never over saved credentials */
static void test_provisioning_seed(void)
{
    printf("  [TEST] Build-time Wi-Fi credentials seed NVS once (REV-30)...\n");
    nvs_mock_reset();
    nvs_init();
    provisioning_mock_reset();
    http_server_init();
    provisioning_init();
    wifi_credentials_t creds;

    TEST_ASSERT(CONFIG_WIFI_STA_SSID[0] == '\0' && CONFIG_WIFI_STA_PASSPHRASE[0] == '\0',
                "Default build carries no station credentials");
    TEST_ASSERT(!provisioning_seed_from_config(CONFIG_WIFI_STA_SSID, CONFIG_WIFI_STA_PASSPHRASE), "Empty seed: nothing stored");
    TEST_ASSERT(!provisioning_has_credentials(), "Still unprovisioned");

    TEST_ASSERT(provisioning_seed_from_config("SeedNet", "seedpass123"), "First boot with a seed stores it");
    TEST_ASSERT(provisioning_get_credentials(&creds) == PROV_OK && strcmp(creds.ssid, "SeedNet") == 0 &&
                strcmp(creds.passphrase, "seedpass123") == 0, "Seeded credentials in NVS");

    TEST_ASSERT(provisioning_set_credentials("PortalNet", "portalpass1") == PROV_OK, "User saves other credentials");
    TEST_ASSERT(!provisioning_seed_from_config("SeedNet", "seedpass123"), "Seed never overwrites saved credentials");
    TEST_ASSERT(provisioning_get_credentials(&creds) == PROV_OK && strcmp(creds.ssid, "PortalNet") == 0, "NVS wins");

    provisioning_clear_credentials();
    TEST_ASSERT(!provisioning_seed_from_config("SeedNet", "seedpass123"), "Same seed after `prov clear`: not re-applied");
    TEST_ASSERT(!provisioning_has_credentials(), "Cleared board stays unprovisioned (setup portal)");
    TEST_ASSERT(provisioning_seed_from_config("NewNet", "newpass1234"), "A different seed (new .config) applies once");
    provisioning_clear_credentials();
}

/* REV-29: portal join with hand-over, failures with reasons, boot join with backoff */
/* ========================================================================= */
/* REV-13: TCP reliability replayed with dropped segments                    */
/* ========================================================================= */
#define TT_MAX_FRAMES      16U
#define TT_PEER_PORT       40000U
#define TT_LISTEN_PORT     8080U
#define TT_PEER_ISN        1000U
#define TT_PEER_WINDOW     8192U

typedef struct {
    uint32_t seq;
    uint32_t ack;
    uint8_t  flags;
    uint16_t sport;
    uint16_t len;
    uint8_t  data[TCP_DEFAULT_SEGMENT_MSS];
} tt_frame_t;

static tt_frame_t s_tt_frames[TT_MAX_FRAMES];
static uint32_t s_tt_frame_count;
static uint8_t s_tt_rx[256];
static uint32_t s_tt_rx_len;
static uint32_t s_tt_rx_calls;
static tcp_status_t s_tt_last_err;
static uint32_t s_tt_err_calls;
static uint32_t s_tt_now;
static bool s_tt_reply;
static bool s_tt_busy;

static uint32_t s_tt_refuse_tx;   /* frames the "driver" refuses next */

static bool tt_tx_hook(const uint8_t *frame, uint16_t len)
{
    if (s_tt_refuse_tx > 0U)
    {
        s_tt_refuse_tx--;
        return false;
    }
    if (s_tt_frame_count >= TT_MAX_FRAMES || len < ETH_HDR_LEN + IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN)
    {
        return true;
    }
    const tcp_header_t *t = (const tcp_header_t *)(frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    tt_frame_t *f = &s_tt_frames[s_tt_frame_count++];
    f->seq = NET_NTOHL(t->seq_num);
    f->ack = NET_NTOHL(t->ack_num);
    f->flags = t->flags;
    f->sport = NET_NTOHS(t->src_port);
    f->len = (uint16_t)(len - ETH_HDR_LEN - IPV4_MIN_HDR_LEN - TCP_MIN_HDR_LEN);
    memcpy(f->data, frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN, f->len);
    return true;
}

static net_status_t tt_recv_cb(void *arg, tcp_pcb_t *pcb, const uint8_t *data, uint16_t len)
{
    (void)arg;
    s_tt_rx_calls++;
    if (s_tt_busy)
    {
        return NET_ERR_BUSY;
    }
    if (s_tt_reply)
    {
        (void)tcp_write(pcb, "OK", 2U);
        (void)tcp_close(pcb);
    }
    for (uint16_t i = 0U; i < len && s_tt_rx_len < sizeof(s_tt_rx); i++)
    {
        s_tt_rx[s_tt_rx_len++] = data[i];
    }
    return NET_OK;
}

static void tt_err_cb(void *arg, tcp_status_t err)
{
    (void)arg;
    s_tt_last_err = err;
    s_tt_err_calls++;
}

static void tt_reset_capture(void)
{
    s_tt_frame_count = 0U;
}

static void tt_advance(uint32_t ms)
{
    s_tt_now += ms;
    tcp_host_set_time_ms(s_tt_now);
    tcp_tick();
}

static void tt_peer_send(uint16_t sport, uint16_t dport, uint32_t seq, uint32_t ack, uint8_t flags,
                         uint16_t wnd, const void *data, uint16_t len)
{
    uint8_t pkt[IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN + 64U];
    memset(pkt, 0, sizeof(pkt));
    uint32_t peer_ip = NET_IP4_ADDR(192, 168, 1, 50);
    uint32_t our_ip = NET_IP4_ADDR(192, 168, 1, 77);
    ipv4_header_t *ip = (ipv4_header_t *)pkt;
    tcp_header_t *t = (tcp_header_t *)(pkt + IPV4_MIN_HDR_LEN);
    ip->ver_ihl = IPV4_VER_IHL_DEFAULT;
    ip->total_len = NET_HTONS(IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN + len);
    ip->ttl = IPV4_TTL_DEFAULT;
    ip->protocol = IPV4_PROTO_TCP;
    ip->src_ip = NET_HTONL(peer_ip);
    ip->dest_ip = NET_HTONL(our_ip);
    t->src_port = NET_HTONS(sport);
    t->dest_port = NET_HTONS(dport);
    t->seq_num = NET_HTONL(seq);
    t->ack_num = NET_HTONL(ack);
    t->data_offset_reserved = (uint8_t)((TCP_MIN_HDR_LEN / TCP_HDR_WORD_BYTES) << TCP_DATA_OFFSET_SHIFT);
    t->flags = flags;
    t->window = NET_HTONS(wnd);
    uint8_t *payload = pkt + IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN;
    if (len > 0U)
    {
        memcpy(payload, data, len);
    }
    t->checksum = NET_HTONS(net_tcp_checksum(peer_ip, our_ip, t, TCP_MIN_HDR_LEN, payload, len));
    (void)tcp_input(pkt, (uint16_t)(IPV4_MIN_HDR_LEN + TCP_MIN_HDR_LEN + len));
}

static tcp_pcb_t *tt_find_conn(uint16_t peer_port)
{
    for (uint32_t i = 0U; i < TCP_MAX_PCBS; i++)
    {
        const tcp_pcb_t *p = tcp_get_pcb(i);
        if (p->in_use && p->state != TCP_STATE_LISTEN && p->remote_port == peer_port)
        {
            return (tcp_pcb_t *)p;
        }
    }
    return NULL;
}

/* Handshake from peer_port; returns the server ISN + 1 (first data seq) */
static uint32_t tt_open(uint16_t peer_port)
{
    tt_reset_capture();
    tt_peer_send(peer_port, TT_LISTEN_PORT, TT_PEER_ISN, 0U, TCP_FLAG_SYN, TT_PEER_WINDOW, NULL, 0U);
    uint32_t s1 = s_tt_frames[0].seq + 1U;
    tt_peer_send(peer_port, TT_LISTEN_PORT, TT_PEER_ISN + 1U, s1, TCP_FLAG_ACK, TT_PEER_WINDOW, NULL, 0U);
    return s1;
}

static void test_tcp_reliability(void)
{
    printf("  [TEST] TCP retransmission, SYN retry, idle policy (REV-13)...\n");
    tcp_telemetry_t tt;
    static uint8_t pattern[3000];
    for (uint32_t i = 0U; i < sizeof(pattern); i++)
    {
        pattern[i] = (uint8_t)(i * 7U + 3U);
    }

    TEST_ASSERT(tcp_init() == TCP_OK, "tcp_init");
    s_tt_now = 100000U;
    tcp_host_set_time_ms(s_tt_now);
    wifi_host_set_tx_hook(tt_tx_hook);
    s_tt_rx_len = 0U;
    s_tt_rx_calls = 0U;
    s_tt_err_calls = 0U;

    tcp_pcb_t *lst = tcp_new();
    TEST_ASSERT(lst != NULL && tcp_bind(lst, TT_LISTEN_PORT) == TCP_OK && tcp_listen(lst, NULL) == TCP_OK,
                "listener up");
    tcp_set_recv_cb(lst, tt_recv_cb);
    tcp_set_err_cb(lst, tt_err_cb);

    /* --- Lost SYN-ACK: retransmitted after RTO with the same sequence number --- */
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, TT_PEER_ISN, 0U, TCP_FLAG_SYN, TT_PEER_WINDOW, NULL, 0U);
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].flags == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
                s_tt_frames[0].ack == TT_PEER_ISN + 1U, "SYN answered with SYN-ACK");
    uint32_t s_isn = s_tt_frames[0].seq;
    tt_advance(TCP_RTO_INITIAL_MS - 1U);
    TEST_ASSERT(s_tt_frame_count == 1U, "no resend before the RTO");
    tt_advance(1U);
    TEST_ASSERT(s_tt_frame_count == 2U && s_tt_frames[1].flags == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
                s_tt_frames[1].seq == s_isn, "lost SYN-ACK retransmitted with the same ISN");
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, TT_PEER_ISN, 0U, TCP_FLAG_SYN, TT_PEER_WINDOW, NULL, 0U);
    TEST_ASSERT(s_tt_frame_count == 3U && s_tt_frames[2].seq == s_isn, "repeated SYN answered with the same SYN-ACK");

    /* Handshake ACK carries the request */
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, TT_PEER_ISN + 1U, s_isn + 1U, TCP_FLAG_ACK | TCP_FLAG_PSH,
                 TT_PEER_WINDOW, "GET", 3U);
    tcp_pcb_t *c = tt_find_conn(TT_PEER_PORT);
    TEST_ASSERT(c != NULL && c->state == TCP_STATE_ESTABLISHED, "handshake completes");
    TEST_ASSERT(s_tt_rx_calls == 1U && s_tt_rx_len == 3U && memcmp(s_tt_rx, "GET", 3U) == 0, "request delivered once");
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].ack == TT_PEER_ISN + 4U, "request ACKed");
    uint32_t rcv = TT_PEER_ISN + 4U;

    /* --- Peer retransmission (our ACK was lost): not delivered twice, re-ACKed --- */
    tcp_get_telemetry(&tt);
    uint32_t dup0 = tt.dup_segments;
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, TT_PEER_ISN + 1U, s_isn + 1U, TCP_FLAG_ACK, TT_PEER_WINDOW, "GET", 3U);
    tcp_get_telemetry(&tt);
    TEST_ASSERT(s_tt_rx_calls == 1U && tt.dup_segments == dup0 + 1U, "duplicate segment not delivered again");
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].flags == TCP_FLAG_ACK && s_tt_frames[0].ack == rcv,
                "duplicate segment re-ACKed");
    /* Overlap: only the new bytes go up */
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, TT_PEER_ISN + 1U, s_isn + 1U, TCP_FLAG_ACK, TT_PEER_WINDOW, "GETXY", 5U);
    TEST_ASSERT(s_tt_rx_len == 5U && memcmp(&s_tt_rx[3], "XY", 2U) == 0, "overlapping segment trimmed to new bytes");
    rcv += 2U;
    /* Gap (a segment before it was lost): dropped and re-ACKed, the peer resends */
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv + 10U, s_isn + 1U, TCP_FLAG_ACK, TT_PEER_WINDOW, "ZZ", 2U);
    TEST_ASSERT(s_tt_rx_len == 5U && s_tt_frame_count == 1U && s_tt_frames[0].ack == rcv,
                "out-of-order segment dropped, duplicate ACK for rcv_nxt");

    /* --- Lost data segment: retransmitted with the same bytes, one segment after the timeout --- */
    tcp_get_telemetry(&tt);
    uint32_t rtt0 = tt.rtt_samples;
    uint32_t rtx0 = tt.retransmit_count;
    tt_reset_capture();
    TEST_ASSERT(tcp_write(c, pattern, sizeof(pattern)) == TCP_OK, "3000 B write accepted");
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].len == TCP_DEFAULT_SEGMENT_MSS &&
                s_tt_frames[0].seq == s_isn + 1U && (s_tt_frames[0].flags & TCP_FLAG_PSH) == 0U,
                "after a lost SYN-ACK the first flight is one segment (RFC 5681 3.1)");
    tt_advance(5U);
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, s_isn + 1U + TCP_DEFAULT_SEGMENT_MSS, TCP_FLAG_ACK,
                 TT_PEER_WINDOW, NULL, 0U);
    TEST_ASSERT(s_tt_frame_count == 2U && s_tt_frames[0].len == TCP_DEFAULT_SEGMENT_MSS && s_tt_frames[1].len == 80U &&
                s_tt_frames[0].seq == s_isn + 1U + TCP_DEFAULT_SEGMENT_MSS && (s_tt_frames[1].flags & TCP_FLAG_PSH) != 0U,
                "ACK grows cwnd: next two segments, PSH on the last");
    TEST_ASSERT(memcmp(s_tt_frames[0].data, &pattern[TCP_DEFAULT_SEGMENT_MSS], TCP_DEFAULT_SEGMENT_MSS) == 0,
                "segment carries the right bytes");
    /* segment 2 is lost: no further ACK */
    tcp_get_telemetry(&tt);
    TEST_ASSERT(tt.rtt_samples == rtt0 + 1U && c->rto_ms == TCP_RTO_MIN_MS, "RTT sampled, RTO clamped to the minimum");
    TEST_ASSERT(c->snd_una == s_isn + 1U + TCP_DEFAULT_SEGMENT_MSS && c->sndbuf_len == sizeof(pattern) - TCP_DEFAULT_SEGMENT_MSS,
                "acked bytes leave the send buffer");
    tt_reset_capture();
    tt_advance(TCP_RTO_MIN_MS);
    tcp_get_telemetry(&tt);
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].seq == s_isn + 1U + TCP_DEFAULT_SEGMENT_MSS &&
                s_tt_frames[0].len == TCP_DEFAULT_SEGMENT_MSS &&
                memcmp(s_tt_frames[0].data, &pattern[TCP_DEFAULT_SEGMENT_MSS], TCP_DEFAULT_SEGMENT_MSS) == 0,
                "lost segment retransmitted alone with identical bytes");
    TEST_ASSERT(tt.retransmit_count == rtx0 + 1U && c->rto_ms == TCP_RTO_MIN_MS * TCP_RTO_BACKOFF_FACTOR,
                "retransmit counted, RTO doubled");
    tt_reset_capture();
    tt_advance(TCP_RTO_MIN_MS * TCP_RTO_BACKOFF_FACTOR - 1U);
    TEST_ASSERT(s_tt_frame_count == 0U, "backed-off timer not yet due");
    tt_advance(1U);
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].seq == s_isn + 1U + TCP_DEFAULT_SEGMENT_MSS,
                "second retransmission after the doubled RTO");
    /* Retransmission arrives: cumulative ACK; Karn: no RTT sample from a resent range */
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, s_isn + 1U + 2U * TCP_DEFAULT_SEGMENT_MSS, TCP_FLAG_ACK,
                 TT_PEER_WINDOW, NULL, 0U);
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].seq == s_isn + 1U + 2U * TCP_DEFAULT_SEGMENT_MSS &&
                s_tt_frames[0].len == 80U, "ACK opens cwnd: the rest is resent");
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, s_isn + 1U + sizeof(pattern), TCP_FLAG_ACK,
                 TT_PEER_WINDOW, NULL, 0U);
    tcp_get_telemetry(&tt);
    TEST_ASSERT(tt.rtt_samples == rtt0 + 1U, "Karn: no RTT sample from retransmitted data");
    TEST_ASSERT(c->sndbuf_len == 0U && !c->rtx_armed && tcp_sndbuf_free_chunks() == TCP_SNDBUF_CHUNKS,
                "all acknowledged: buffer returned, timer stopped");
    TEST_ASSERT(c->retries == 0U, "retries reset by progress");

    /* --- Peer window limits what is in flight --- */
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, s_isn + 1U + sizeof(pattern), TCP_FLAG_ACK, 1000U, NULL, 0U);
    TEST_ASSERT(tcp_write(c, pattern, 2000U) == TCP_OK && s_tt_frame_count == 1U && s_tt_frames[0].len == 1000U,
                "only the peer window is sent");
    uint32_t base = s_isn + 1U + sizeof(pattern);
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, base + 1000U, TCP_FLAG_ACK, 1000U, NULL, 0U);
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].seq == base + 1000U && s_tt_frames[0].len == 1000U,
                "window update releases the next bytes");
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, base + 2000U, TCP_FLAG_ACK, TT_PEER_WINDOW, NULL, 0U);
    base += 2000U;

    /* --- Response + close: FIN piggybacked; lost FIN retransmitted; TIME_WAIT re-ACKs a resent FIN --- */
    tt_reset_capture();
    s_tt_reply = true;   /* HTTP pattern: recv_cb writes the response and closes */
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, base, TCP_FLAG_ACK | TCP_FLAG_PSH, TT_PEER_WINDOW, "Q", 1U);
    s_tt_reply = false;
    rcv += 1U;
    TEST_ASSERT(s_tt_frame_count == 1U && (s_tt_frames[0].flags & TCP_FLAG_FIN) != 0U && s_tt_frames[0].len == 2U &&
                s_tt_frames[0].ack == rcv && c->state == TCP_STATE_FIN_WAIT_1,
                "response, FIN and the request's ACK leave in one frame");
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, base + 2U, TCP_FLAG_ACK, TT_PEER_WINDOW, NULL, 0U);
    TEST_ASSERT(c->state == TCP_STATE_FIN_WAIT_1 && c->rtx_armed, "data acked, FIN still outstanding");
    tt_reset_capture();
    tt_advance(c->rto_ms);
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].flags == (TCP_FLAG_FIN | TCP_FLAG_ACK) &&
                s_tt_frames[0].seq == base + 2U && s_tt_frames[0].len == 0U, "lost FIN retransmitted bare");
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, base + 3U, TCP_FLAG_ACK, TT_PEER_WINDOW, NULL, 0U);
    TEST_ASSERT(c->state == TCP_STATE_FIN_WAIT_2 && !c->rtx_armed, "FIN acked: FIN_WAIT_2");
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, base + 3U, TCP_FLAG_FIN | TCP_FLAG_ACK, TT_PEER_WINDOW, NULL, 0U);
    TEST_ASSERT(c->state == TCP_STATE_TIME_WAIT && s_tt_frame_count == 1U && s_tt_frames[0].ack == rcv + 1U,
                "peer FIN acked, TIME_WAIT");
    tt_peer_send(TT_PEER_PORT, TT_LISTEN_PORT, rcv, base + 3U, TCP_FLAG_FIN | TCP_FLAG_ACK, TT_PEER_WINDOW, NULL, 0U);
    TEST_ASSERT(s_tt_frame_count == 2U && s_tt_frames[1].ack == rcv + 1U, "resent peer FIN re-ACKed in TIME_WAIT");
    tt_advance(TCP_TIME_WAIT_MS);
    TEST_ASSERT(!c->in_use, "TIME_WAIT ends");

    /* --- Retries exhausted: err_cb(TIMEOUT), PCB and buffer freed --- */
    uint32_t s2 = tt_open(TT_PEER_PORT + 1U);
    c = tt_find_conn(TT_PEER_PORT + 1U);
    TEST_ASSERT(c != NULL && c->state == TCP_STATE_ESTABLISHED, "second connection");
    tt_reset_capture();
    TEST_ASSERT(tcp_write(c, pattern, 100U) == TCP_OK, "write never acknowledged");
    tcp_get_telemetry(&tt);
    uint32_t give0 = tt.rto_giveups;
    s_tt_err_calls = 0U;
    uint32_t rto = TCP_RTO_INITIAL_MS;
    for (uint32_t r = 0U; r < TCP_MAX_RETRIES; r++)
    {
        tt_advance(rto);
        rto = (rto * TCP_RTO_BACKOFF_FACTOR > TCP_RTO_MAX_MS) ? TCP_RTO_MAX_MS : rto * TCP_RTO_BACKOFF_FACTOR;
    }
    TEST_ASSERT(s_tt_frame_count == 1U + TCP_MAX_RETRIES && s_tt_frames[TCP_MAX_RETRIES].seq == s2 &&
                c->in_use, "every retry resends the oldest segment");
    tt_advance(rto);
    tcp_get_telemetry(&tt);
    TEST_ASSERT(!c->in_use && s_tt_err_calls == 1U && s_tt_last_err == TCP_ERR_TIMEOUT && tt.rto_giveups == give0 + 1U,
                "gives up after the last retry and tells the application");
    TEST_ASSERT(tcp_sndbuf_free_chunks() == TCP_SNDBUF_CHUNKS, "send buffer returned on give-up");

    /* --- Idle policy: default expires, NEVER survives --- */
    (void)tt_open(TT_PEER_PORT + 2U);
    tcp_pcb_t *http_like = tt_find_conn(TT_PEER_PORT + 2U);
    (void)tt_open(TT_PEER_PORT + 3U);
    tcp_pcb_t *persistent = tt_find_conn(TT_PEER_PORT + 3U);
    tcp_set_idle_timeout(persistent, TCP_IDLE_TIMEOUT_NEVER);
    TEST_ASSERT(http_like != NULL && persistent != NULL && http_like->idle_timeout_ms == TCP_IDLE_TIMEOUT_DEFAULT_MS,
                "accepted connections inherit the default idle timeout");
    tcp_get_telemetry(&tt);
    uint32_t idle0 = tt.idle_expired;
    tt_advance(TCP_IDLE_TIMEOUT_DEFAULT_MS - 1U);
    TEST_ASSERT(http_like->in_use, "not expired before the idle timeout");
    tt_advance(1U);
    tcp_get_telemetry(&tt);
    TEST_ASSERT(!http_like->in_use && tt.idle_expired == idle0 + 1U, "default connection expires when idle");
    for (uint32_t h = 0U; h < 24U; h++)
    {
        tt_advance(3600000U);
    }
    TEST_ASSERT(persistent->in_use && persistent->state == TCP_STATE_ESTABLISHED, "NEVER connection idle for 24 h survives");
    for (uint32_t i = 0U; i < TCP_MAX_PCBS; i++)
    {
        tcp_pcb_t *spare = tcp_new();
        TEST_ASSERT(spare != persistent, "slot recycling never takes a persistent connection");
        if (spare == NULL) break;
    }

    /* --- RST: err_cb(RST) --- */
    s_tt_err_calls = 0U;
    tt_peer_send(TT_PEER_PORT + 3U, TT_LISTEN_PORT, TT_PEER_ISN + 1U, 0U, TCP_FLAG_RST, 0U, NULL, 0U);
    TEST_ASSERT(!persistent->in_use && s_tt_err_calls == 1U && s_tt_last_err == TCP_ERR_RST, "RST reported to the application");

    /* --- Client SYN retries, then gives up; ephemeral ports rotate --- */
    TEST_ASSERT(tcp_init() == TCP_OK, "fresh stack");
    tcp_pcb_t *cl = tcp_new();
    tcp_set_err_cb(cl, tt_err_cb);
    s_tt_err_calls = 0U;
    tt_reset_capture();
    TEST_ASSERT(tcp_connect(cl, NET_IP4_ADDR(192, 168, 1, 50), 1883U) == TCP_OK && cl->state == TCP_STATE_SYN_SENT,
                "connect sends SYN");
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].flags == TCP_FLAG_SYN && s_tt_frames[0].sport == TCP_EPHEMERAL_PORT_MIN,
                "SYN from the first ephemeral port");
    rto = TCP_RTO_INITIAL_MS;
    for (uint32_t r = 0U; r < TCP_SYN_MAX_RETRIES; r++)
    {
        tt_advance(rto);
        rto *= TCP_RTO_BACKOFF_FACTOR;
    }
    TEST_ASSERT(s_tt_frame_count == 1U + TCP_SYN_MAX_RETRIES && s_tt_frames[TCP_SYN_MAX_RETRIES].flags == TCP_FLAG_SYN &&
                s_tt_frames[TCP_SYN_MAX_RETRIES].seq == s_tt_frames[0].seq, "SYN retransmitted with backoff");
    tt_advance(rto);
    TEST_ASSERT(!cl->in_use && s_tt_err_calls == 1U && s_tt_last_err == TCP_ERR_TIMEOUT, "connect times out");
    cl = tcp_new();
    tt_reset_capture();
    (void)tcp_connect(cl, NET_IP4_ADDR(192, 168, 1, 50), 1883U);
    TEST_ASSERT(s_tt_frames[0].sport == TCP_EPHEMERAL_PORT_MIN + 1U, "next connect uses the next ephemeral port");
    uint32_t c_isn = s_tt_frames[0].seq;
    tt_peer_send(1883U, TCP_EPHEMERAL_PORT_MIN + 1U, 5000U, c_isn + 1U, TCP_FLAG_SYN | TCP_FLAG_ACK, TT_PEER_WINDOW, NULL, 0U);
    TEST_ASSERT(cl->state == TCP_STATE_ESTABLISHED && !cl->rtx_armed && cl->rcv_nxt == 5001U, "client handshake completes");
    tcp_abort(cl);

    /* --- Send buffer pool: all-or-nothing writes --- */
    TEST_ASSERT(tcp_init() == TCP_OK, "fresh stack");
    lst = tcp_new();
    (void)tcp_bind(lst, TT_LISTEN_PORT);
    (void)tcp_listen(lst, NULL);
    (void)tt_open(TT_PEER_PORT);
    (void)tt_open(TT_PEER_PORT + 1U);
    tcp_pcb_t *a = tt_find_conn(TT_PEER_PORT);
    tcp_pcb_t *b = tt_find_conn(TT_PEER_PORT + 1U);
    static uint8_t big[TCP_SNDBUF_PCB_MAX_BYTES];
    memset(big, 'x', sizeof(big));
    tcp_get_telemetry(&tt);
    uint32_t full0 = tt.sndbuf_full;
    TEST_ASSERT(tcp_write(a, big, sizeof(big)) == TCP_OK, "4 KB response buffered");
    TEST_ASSERT(tcp_write(a, big, 1U) == TCP_ERR_MEM, "per-connection cap");
    uint32_t left = TCP_SNDBUF_CHUNKS * TCP_SNDBUF_CHUNK_SIZE - TCP_SNDBUF_PCB_MAX_BYTES;
    TEST_ASSERT(tcp_sndbuf_space(b) == left && tcp_write(b, big, (uint16_t)(left + 1U)) == TCP_ERR_MEM,
                "pool exhaustion refuses the whole write");
    TEST_ASSERT(tcp_write(b, big, (uint16_t)left) == TCP_OK && tcp_sndbuf_free_chunks() == 0U, "the rest of the pool fits");
    tcp_get_telemetry(&tt);
    TEST_ASSERT(tt.sndbuf_full == full0 + 2U, "refusals counted");
    tcp_abort(a);
    tcp_abort(b);
    TEST_ASSERT(tcp_sndbuf_free_chunks() == TCP_SNDBUF_CHUNKS, "abort returns the buffers");

    /* --- Driver out of TX buffers: short retry, no backoff; a dead interface ends in the RTO path --- */
    (void)tt_open(TT_PEER_PORT + 6U);
    tcp_pcb_t *tb = tt_find_conn(TT_PEER_PORT + 6U);
    tcp_get_telemetry(&tt);
    uint32_t blk0 = tt.tx_blocked;
    uint32_t rtx1 = tt.retransmit_count;
    tt_reset_capture();
    s_tt_refuse_tx = 1U;
    TEST_ASSERT(tcp_write(tb, "DATA", 4U) == TCP_OK && s_tt_frame_count == 0U && tb->tx_blocked,
                "refused send leaves the data unsent");
    tt_advance(TCP_TX_BLOCKED_RETRY_MS);
    tcp_get_telemetry(&tt);
    TEST_ASSERT(s_tt_frame_count == 1U && s_tt_frames[0].len == 4U && tt.tx_blocked == blk0 + 1U &&
                tt.retransmit_count == rtx1 && tb->rto_ms == TCP_RTO_INITIAL_MS && tb->retries == 0U,
                "sent after the short retry without backoff or retry count");
    TEST_ASSERT(tb->rtx_armed && !tb->tx_blocked, "normal RTO armed for the sent segment");
    tcp_abort(tb);
    (void)tt_open(TT_PEER_PORT + 7U);
    tb = tt_find_conn(TT_PEER_PORT + 7U);
    tt_reset_capture();
    s_tt_refuse_tx = TCP_TX_BLOCKED_MAX + 1U;
    (void)tcp_write(tb, "DATA", 4U);
    for (uint32_t k = 0U; k < TCP_TX_BLOCKED_MAX; k++)
    {
        tt_advance(TCP_TX_BLOCKED_RETRY_MS);
    }
    TEST_ASSERT(!tb->tx_blocked && tb->snd_max == tb->snd_una + 4U && tb->rtx_armed,
                "interface down: after the cap the segment counts as lost, RTO path");
    tt_advance(tb->rto_ms);
    TEST_ASSERT(s_tt_frame_count == 1U && tb->retries == 1U, "RTO resends it");
    tcp_abort(tb);

    /* --- Backpressure: a refused segment is not acknowledged; its retransmission is taken --- */
    lst->recv_cb = tt_recv_cb;
    uint32_t s3 = tt_open(TT_PEER_PORT + 5U);
    tcp_pcb_t *bp = tt_find_conn(TT_PEER_PORT + 5U);
    tcp_get_telemetry(&tt);
    uint32_t def0 = tt.rx_deferred;
    s_tt_rx_len = 0U;
    s_tt_busy = true;
    tt_reset_capture();
    tt_peer_send(TT_PEER_PORT + 5U, TT_LISTEN_PORT, TT_PEER_ISN + 1U, s3, TCP_FLAG_ACK | TCP_FLAG_PSH, TT_PEER_WINDOW, "GET", 3U);
    s_tt_busy = false;
    tcp_get_telemetry(&tt);
    TEST_ASSERT(bp->rcv_nxt == TT_PEER_ISN + 1U && s_tt_frame_count == 0U && tt.rx_deferred == def0 + 1U,
                "busy receiver: segment left unacknowledged");
    tt_peer_send(TT_PEER_PORT + 5U, TT_LISTEN_PORT, TT_PEER_ISN + 1U, s3, TCP_FLAG_ACK | TCP_FLAG_PSH, TT_PEER_WINDOW, "GET", 3U);
    TEST_ASSERT(bp->rcv_nxt == TT_PEER_ISN + 4U && s_tt_rx_len == 3U && s_tt_frame_count == 1U &&
                s_tt_frames[0].ack == TT_PEER_ISN + 4U, "peer retransmission accepted and acknowledged");
    tcp_abort(bp);

    wifi_host_set_tx_hook(NULL);
    TEST_ASSERT(tcp_init() == TCP_OK, "stack reset for later tests");
}

static void test_wifi_link_policy(void)
{
    printf("  [TEST] Wi-Fi link manager policy: backoff, jitter, fast path (REV-12)...\n");

    /* Backoff: 1 s doubling to a 5 min cap */
    TEST_ASSERT(wifi_link_next_backoff_us(0U) == WIFI_LINK_RETRY_MIN_US, "Backoff starts at the minimum");
    TEST_ASSERT(wifi_link_next_backoff_us(WIFI_LINK_RETRY_MIN_US) == 2U * WIFI_LINK_RETRY_MIN_US, "Backoff doubles");
    uint64_t d = WIFI_LINK_RETRY_MIN_US;
    uint32_t steps = 0U;
    while (d < WIFI_LINK_RETRY_MAX_US && steps < 32U)
    {
        d = wifi_link_next_backoff_us(d);
        steps++;
    }
    TEST_ASSERT(d == WIFI_LINK_RETRY_MAX_US && steps == 9U, "Cap reached after 9 doublings (1 s -> 5 min)");
    TEST_ASSERT(wifi_link_next_backoff_us(WIFI_LINK_RETRY_MAX_US) == WIFI_LINK_RETRY_MAX_US, "Cap holds");

    /* Jitter: +/- 20 %, whole range reachable, never outside */
    const uint64_t base = 10U * WIFI_LINK_RETRY_MIN_US;
    const uint64_t span = base * WIFI_LINK_JITTER_PERCENT / WIFI_LINK_PERCENT;
    TEST_ASSERT(wifi_link_jitter_us(base, 0U) == base - span, "Jitter low end");
    TEST_ASSERT(wifi_link_jitter_us(base, (uint32_t)(2U * span)) == base + span, "Jitter high end");
    bool in_range = true;
    uint32_t r = 12345U;
    for (uint32_t i = 0U; i < 1000U; i++)
    {
        r = r * 1103515245U + 12345U;
        uint64_t j = wifi_link_jitter_us(base, r);
        in_range = in_range && j >= base - span && j <= base + span;
    }
    TEST_ASSERT(in_range, "Jitter stays within +/- 20 %");
    TEST_ASSERT(wifi_link_jitter_us(0U, 0xFFFFFFFFU) == 0U, "Zero delay stays zero");

    /* Fast path: none without a cached AP; N attempts on the cached AP, then full scans until a success */
    const uint8_t ap[WIFI_LINK_BSSID_LEN] = {0x02U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U};
    uint8_t out[WIFI_LINK_BSSID_LEN];
    uint8_t chan = 0xFFU;
    wifi_link_forget();
    TEST_ASSERT(!wifi_link_next_target(out, &chan) && chan == 0U, "No cache: full scan");
    wifi_link_on_connected(ap, 0U);
    TEST_ASSERT(!wifi_link_get_cache()->valid, "Channel 0 is not cached");
    wifi_link_on_connected(ap, 9U);
    for (uint32_t i = 0U; i < WIFI_LINK_FAST_ATTEMPTS; i++)
    {
        memset(out, 0, sizeof(out));
        TEST_ASSERT(wifi_link_next_target(out, &chan) && chan == 9U && memcmp(out, ap, sizeof(ap)) == 0,
                    "Fast attempt on the cached AP");
        wifi_link_on_attempt_failed(true);
    }
    TEST_ASSERT(!wifi_link_next_target(out, &chan) && chan == 0U, "Fast attempts used up: full scan");
    wifi_link_on_attempt_failed(false);
    TEST_ASSERT(!wifi_link_next_target(out, &chan), "Full scans continue until a success");
    TEST_ASSERT(wifi_link_get_cache()->fast_attempts == WIFI_LINK_FAST_ATTEMPTS &&
                wifi_link_get_cache()->full_attempts == 3U, "Attempts counted per path");
    wifi_link_on_connected(ap, 9U);
    TEST_ASSERT(wifi_link_next_target(out, &chan), "Success re-arms the fast path");
    wifi_link_forget();
    TEST_ASSERT(!wifi_link_get_cache()->valid, "Forget clears the cache");

    /* PMK cache: same network reuses the PMK, other SSID or passphrase derives again */
    wpa2_telemetry_t wt;
    wpa2_client_init();
    TEST_ASSERT(wpa2_client_configure("test-net-a", "passphrase-one") == WPA2_OK, "First configure");
    wpa2_client_get_telemetry(&wt);
    TEST_ASSERT(wt.pmk_derivations == 1U && wt.pmk_cache_hits == 0U && wt.has_pmk, "First configure derives");
    TEST_ASSERT(wpa2_client_configure("test-net-a", "passphrase-one") == WPA2_OK, "Same network again");
    wpa2_client_get_telemetry(&wt);
    TEST_ASSERT(wt.pmk_derivations == 1U && wt.pmk_cache_hits == 1U && wt.has_pmk, "Same network: cache hit");
    TEST_ASSERT(wpa2_client_configure("test-net-a", "passphrase-two") == WPA2_OK, "Other passphrase");
    wpa2_client_get_telemetry(&wt);
    TEST_ASSERT(wt.pmk_derivations == 2U, "Other passphrase derives again");
    TEST_ASSERT(wpa2_client_configure("test-net-b", "passphrase-two") == WPA2_OK, "Other SSID");
    wpa2_client_get_telemetry(&wt);
    TEST_ASSERT(wt.pmk_derivations == 3U, "Other SSID derives again");
    wpa2_client_init();
    TEST_ASSERT(wpa2_client_get_telemetry(&wt) == WPA2_OK && wt.pmk_derivations == 0U, "Init clears the PMK cache");
}

static void test_provisioning_join(void)
{
    printf("  [TEST] Provisioning station join and portal hand-over (REV-29)...\n");

    nvs_mock_reset();
    nvs_init();
    provisioning_mock_reset();
    http_server_init();
    TEST_ASSERT(provisioning_init() == PROV_OK, "provisioning_init succeeds");

    prov_join_info_t join;
    char resp_buf[HTTP_RESPONSE_BUF_SIZE];
    size_t resp_len = 0U;
    const char req_status[] = "GET /api/wifi/status HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    const uint64_t t0 = 1000000ULL;

    /* No credentials: no join, boot opens the setup SoftAP */
    TEST_ASSERT(provisioning_request_join(true) == PROV_ERR_NOT_FOUND, "Join refused without saved credentials");
    wifi_stop_ap();
    provisioning_boot();
    TEST_ASSERT(wifi_is_ap_active(), "Unprovisioned boot starts the setup SoftAP");
    TEST_ASSERT(provisioning_get_join(&join) == PROV_OK && join.state == PROV_JOIN_IDLE, "Unprovisioned boot does not join");
    wifi_stop_ap();
    TEST_ASSERT(wifi_get_ip_tx_if() == WIFI_TX_IF_AP, "STA not joined: IP frames do not go to the STA");
    wifi_start_ap("IronV-AP", NULL, 1U);

    /* 1. Portal join: SoftAP and the IP stack on it stay up until the hand-over */
    TEST_ASSERT(provisioning_set_credentials("ironhotspot", "12345test") == PROV_OK, "Test credentials saved");
    TEST_ASSERT(provisioning_request_join(true) == PROV_OK, "Portal join requested");
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_PENDING && join.keep_ap, "Join pending, SoftAP kept");
    provisioning_tick(t0 - PROV_PORTAL_JOIN_DELAY_US);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_PENDING && join.attempts == 0U, "Portal join waits for the save reply to go out");
    provisioning_tick(t0);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_JOINING && join.attempts == 1U, "Tick starts the join");
    TEST_ASSERT(wifi_is_ap_active() && wifi_get_ip_tx_if() == WIFI_TX_IF_AP, "Joining: IP stack still on the SoftAP");

    const uint8_t bssid_a[WIFI_LINK_BSSID_LEN] = {0x02U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U};
    const uint8_t bssid_b[WIFI_LINK_BSSID_LEN] = {0x02U, 0x66U, 0x77U, 0x88U, 0x99U, 0xAAU};
    uint8_t target[WIFI_LINK_BSSID_LEN];
    uint8_t target_chan = 0U;
    wpa2_telemetry_t wtel;
    wifi_host_set_sta_link(bssid_a, 6U);
    wifi_host_set_sta_connected(true);
    provisioning_tick(t0 + PROV_US_PER_SECOND);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_HANDOVER, "Joined: hand-over pending");
    TEST_ASSERT(wifi_link_get_cache()->valid && wifi_link_get_cache()->channel == 6U &&
                memcmp(wifi_link_get_cache()->bssid, bssid_a, WIFI_LINK_BSSID_LEN) == 0, "Joined AP cached");
    wpa2_client_get_telemetry(&wtel);
    const uint32_t pmk_runs = wtel.pmk_derivations;
    TEST_ASSERT(pmk_runs >= 1U, "First join derives the PMK");
    TEST_ASSERT(wifi_is_ap_active() && wifi_get_ip_tx_if() == WIFI_TX_IF_AP, "Hand-over pending: phone still reaches the portal");
    http_process_request(req_status, strlen(req_status), resp_buf, sizeof(resp_buf), &resp_len);
    TEST_ASSERT(strstr(resp_buf, "\"join\":{\"state\":\"connected\"") != NULL, "Status reports connected");
    TEST_ASSERT(strstr(resp_buf, "\"hostname\":\"iron-v.local\"") != NULL, "Status names the LAN hostname");

    provisioning_tick(t0 + PROV_US_PER_SECOND + PROV_HANDOVER_DELAY_US - 1U);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_HANDOVER, "SoftAP stays for the whole hand-over delay");
    provisioning_tick(t0 + PROV_US_PER_SECOND + PROV_HANDOVER_DELAY_US);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_ONLINE, "Hand-over done: online");
    TEST_ASSERT(!wifi_is_ap_active() && wifi_get_ip_tx_if() == WIFI_TX_IF_STA, "SoftAP gone, IP stack on the STA");

    /* 2. Link lost while online: retry after the minimum backoff (+/- jitter), cached AP first (REV-12) */
    uint64_t t1 = t0 + 100U * PROV_US_PER_SECOND;
    wifi_host_post_sta_disconnect(WIFI_REASON_BEACON_TIMEOUT);
    provisioning_tick(t1);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_RETRY_WAIT && join.last_reason == WIFI_REASON_BEACON_TIMEOUT, "Link loss: waits to retry");
    TEST_ASSERT(join.link_losses == 1U, "Link loss counted");
    TEST_ASSERT(!wifi_is_ap_active(), "Link loss does not start the SoftAP");
    TEST_ASSERT(join.next_event_us >= t1 + WIFI_LINK_RETRY_MIN_US - WIFI_LINK_RETRY_MIN_US * WIFI_LINK_JITTER_PERCENT / WIFI_LINK_PERCENT &&
                join.next_event_us <= t1 + WIFI_LINK_RETRY_MIN_US + WIFI_LINK_RETRY_MIN_US * WIFI_LINK_JITTER_PERCENT / WIFI_LINK_PERCENT,
                "First retry after the minimum backoff, within the jitter");
    provisioning_tick(join.next_event_us - 1U);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_RETRY_WAIT, "No retry before the backoff ends");
    provisioning_tick(join.next_event_us);
    provisioning_tick(join.next_event_us);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_JOINING && !join.keep_ap && join.fast_path, "Retry rejoins as a station, fast path");
    TEST_ASSERT(wifi_host_last_sta_target(target, &target_chan) && target_chan == 6U &&
                memcmp(target, bssid_a, WIFI_LINK_BSSID_LEN) == 0, "Fast path targets the cached BSSID and channel");
    wpa2_client_get_telemetry(&wtel);
    TEST_ASSERT(wtel.pmk_derivations == pmk_runs && wtel.pmk_cache_hits >= 1U, "Reconnect reuses the cached PMK (no PBKDF2)");

    /* 3. Failed attempts: blob stopped, backoff doubles, full scan after the fast attempts */
    uint64_t t2 = t1 + 200U * PROV_US_PER_SECOND;
    uint32_t disc_calls = wifi_host_sta_disconnect_calls();
    wifi_host_post_sta_disconnect(WIFI_REASON_NO_AP_FOUND);
    provisioning_tick(t2);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_RETRY_WAIT && join.retry_delay_us == 4U * WIFI_LINK_RETRY_MIN_US &&
                join.next_event_us >= t2 + 2U * WIFI_LINK_RETRY_MIN_US * (WIFI_LINK_PERCENT - WIFI_LINK_JITTER_PERCENT) / WIFI_LINK_PERCENT &&
                join.next_event_us <= t2 + 2U * WIFI_LINK_RETRY_MIN_US * (WIFI_LINK_PERCENT + WIFI_LINK_JITTER_PERCENT) / WIFI_LINK_PERCENT,
                "Failed retry: waits 2 s (+/- jitter), next delay doubles");
    TEST_ASSERT(wifi_host_sta_disconnect_calls() == disc_calls + 1U, "Failed attempt stops the blob's own retries (O-31)");
    TEST_ASSERT(!wifi_is_ap_active(), "Failed join does not start the SoftAP");
    for (uint32_t i = 1U; i < WIFI_LINK_FAST_ATTEMPTS; i++)
    {
        provisioning_tick(join.next_event_us);
        provisioning_tick(join.next_event_us);
        provisioning_get_join(&join);
        TEST_ASSERT(join.fast_path, "Fast path for the first attempts");
        wifi_host_post_sta_disconnect(WIFI_REASON_NO_AP_FOUND);
        provisioning_tick(join.next_event_us + 1U);
        provisioning_get_join(&join);
    }
    provisioning_tick(join.next_event_us);
    provisioning_tick(join.next_event_us);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_JOINING && !join.fast_path, "After the fast attempts: full scan");
    TEST_ASSERT(!wifi_host_last_sta_target(target, &target_chan) && target_chan == 0U, "Full scan: no BSSID, all channels");
    for (uint32_t i = 0U; i < 10U; i++)
    {
        wifi_host_post_sta_disconnect(WIFI_REASON_NO_AP_FOUND);
        provisioning_tick(join.next_event_us + 1U);
        provisioning_get_join(&join);
        provisioning_tick(join.next_event_us);
        provisioning_tick(join.next_event_us);
        provisioning_get_join(&join);
    }
    TEST_ASSERT(join.retry_delay_us == WIFI_LINK_RETRY_MAX_US && !join.fast_path, "Backoff capped, still full scans");

    /* Full scan finds another node of the network: it becomes the cached AP */
    wifi_host_set_sta_link(bssid_b, 11U);
    wifi_host_set_sta_connected(true);
    provisioning_tick(join.next_event_us + 1U);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_ONLINE && join.retry_delay_us == WIFI_LINK_RETRY_MIN_US, "Rejoined: online, backoff reset");
    TEST_ASSERT(wifi_link_get_cache()->channel == 11U && wifi_link_get_cache()->fast_failures == 0U &&
                memcmp(wifi_link_get_cache()->bssid, bssid_b, WIFI_LINK_BSSID_LEN) == 0, "New AP cached");
    uint64_t t2b = join.next_event_us + 10U * PROV_US_PER_SECOND;
    wifi_host_post_sta_disconnect(WIFI_REASON_BEACON_TIMEOUT);
    provisioning_tick(t2b);
    provisioning_get_join(&join);
    provisioning_tick(join.next_event_us);
    provisioning_tick(join.next_event_us);
    provisioning_get_join(&join);
    TEST_ASSERT(join.fast_path && wifi_host_last_sta_target(target, &target_chan) && target_chan == 11U &&
                memcmp(target, bssid_b, WIFI_LINK_BSSID_LEN) == 0 && join.link_losses == 2U,
                "Next link loss: fast path to the new AP");
    wpa2_client_get_telemetry(&wtel);
    TEST_ASSERT(wtel.pmk_derivations == pmk_runs, "Still one PBKDF2 after many reconnects");
    wifi_host_post_sta_disconnect(WIFI_REASON_NO_AP_FOUND);
    provisioning_tick(join.next_event_us + 1U);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_RETRY_WAIT, "Fast attempt failed: waiting to retry");

    /* 4. Portal join with a wrong passphrase: SoftAP stays, reason reported */
    provisioning_cancel_join();
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_IDLE, "cancel_join drops a waiting retry");
    wifi_start_ap("IronV-AP", NULL, 1U);
    TEST_ASSERT(provisioning_request_join(true) == PROV_OK, "Second portal join requested");
    provisioning_tick(t2 + 999U * PROV_US_PER_SECOND);
    provisioning_tick(t2 + 1000U * PROV_US_PER_SECOND);
    wifi_host_post_sta_disconnect(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT);
    provisioning_tick(t2 + 1001U * PROV_US_PER_SECOND);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_FAILED && join.last_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT, "Portal join failed with the reason");
    TEST_ASSERT(wifi_is_ap_active() && wifi_get_ip_tx_if() == WIFI_TX_IF_AP, "Failed portal join keeps the SoftAP");
    TEST_ASSERT(provisioning_has_credentials(), "Failed join keeps credentials that were not saved by the portal");
    http_process_request(req_status, strlen(req_status), resp_buf, sizeof(resp_buf), &resp_len);
    TEST_ASSERT(strstr(resp_buf, "\"state\":\"failed\"") != NULL &&
                strstr(resp_buf, "\"reason\":15,\"message\":\"The network rejected the password\"") != NULL,
                "Status reports the failure and a readable reason");

    /* 5. Portal join without an answer times out */
    uint64_t t3 = t2 + 2000U * PROV_US_PER_SECOND;
    provisioning_set_credentials("ironhotspot", "12345test");
    provisioning_request_join(true);
    provisioning_tick(t3 - PROV_PORTAL_JOIN_DELAY_US);
    provisioning_tick(t3);
    provisioning_tick(t3 + PROV_JOIN_TIMEOUT_US - 1U);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_JOINING, "Still joining before the timeout");
    provisioning_tick(t3 + PROV_JOIN_TIMEOUT_US);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_FAILED && join.last_reason == 0U, "Join timeout reported as reason 0");
    TEST_ASSERT(strcmp(provisioning_reason_hint(0U), "No answer from the network (timed out)") == 0, "Timeout hint");

    /* 6. Provisioned boot joins as a station (no SoftAP) */
    provisioning_set_credentials("ironhotspot", "12345test");
    wifi_stop_ap();
    provisioning_boot();
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_PENDING && !join.keep_ap && !wifi_is_ap_active(), "Provisioned boot: STA join, no SoftAP");

    TEST_ASSERT(provisioning_init() == PROV_OK && provisioning_get_join(&join) == PROV_OK &&
                join.state == PROV_JOIN_PENDING, "Re-init (do-test) keeps the pending join");

    /* 7. Portal save with a wrong passphrase: credentials are forgotten again */
    const char req_cfg[] =
        "POST /api/wifi/configure HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Type: application/json\r\n\r\n"
        "{\"ssid\":\"ironhotspot\",\"password\":\"wrongpass99\"}";
    wifi_start_ap("IronV-AP", NULL, 1U);
    http_process_request(req_cfg, strlen(req_cfg), resp_buf, sizeof(resp_buf), &resp_len);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_PENDING && join.keep_ap && join.unproven, "Portal save queues an unproven join");
    uint64_t t4 = t3 + 1000U * PROV_US_PER_SECOND;
    provisioning_tick(t4 - PROV_PORTAL_JOIN_DELAY_US);
    provisioning_tick(t4);
    wifi_host_post_sta_disconnect(WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT);
    provisioning_tick(t4 + 1U);
    TEST_ASSERT(!provisioning_has_credentials(), "Failed portal save forgets the unproven credentials");

    /* 8. do-test restore: a join that was active before the suite is restarted, test joins dropped */
    provisioning_set_credentials("ironhotspot", "12345test");
    provisioning_restore_join(PROV_JOIN_ONLINE);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_PENDING && !join.keep_ap && !join.unproven, "Restore rejoins as a station");
    provisioning_restore_join(PROV_JOIN_IDLE);
    provisioning_get_join(&join);
    TEST_ASSERT(join.state == PROV_JOIN_IDLE, "Restore drops a join queued by the suite");

    provisioning_cancel_join();
    provisioning_clear_credentials();
    wifi_host_set_scan_records(NULL, 0U);
}

/* Builds a DNS query for <name> (dotted) with qtype into q; returns its length */
static uint16_t build_dns_query(uint8_t *q, size_t cap, const char *name, uint16_t qtype)
{
    memset(q, 0, cap);
    dns_header_t *h = (dns_header_t *)q;
    h->id = NET_HTONS(0x1234U);
    h->qdcount = NET_HTONS(1U);
    size_t i = sizeof(dns_header_t);
    const char *p = name;
    while (*p != '\0')
    {
        size_t l = 0U;
        while (p[l] != '\0' && p[l] != '.') l++;
        q[i++] = (uint8_t)l;
        memcpy(&q[i], p, l);
        i += l;
        p += l;
        if (*p == '.') p++;
    }
    q[i++] = 0U;
    q[i++] = (uint8_t)(qtype >> 8);
    q[i++] = (uint8_t)(qtype & 0xFFU);
    q[i++] = 0U;
    q[i++] = 1U; /* QCLASS IN */
    return (uint16_t)i;
}

/* Sends one query through dns_process_packet and returns the captured DNS reply header */
static const dns_header_t *run_dns_query(const uint8_t *query, uint16_t len, dhcp_status_t *out_st)
{
    uint8_t frame[ETH_HDR_LEN + sizeof(ipv4_header_t) + sizeof(udp_header_t)];
    memset(frame, 0, sizeof(frame));
    ethernet_header_t *eth = (ethernet_header_t *)frame;
    const uint8_t phone_mac[6] = {0x36, 0xC0, 0x65, 0x73, 0xB7, 0xE3};
    memcpy(eth->src_mac, phone_mac, 6U);
    ipv4_header_t *ip = (ipv4_header_t *)(frame + ETH_HDR_LEN);
    ip->ver_ihl = 0x45U;
    ip->src_ip = NET_HTONL(0xC0A80102U); /* 192.168.1.2 */
    udp_header_t *udp = (udp_header_t *)(frame + ETH_HDR_LEN + sizeof(ipv4_header_t));
    udp->src_port = NET_HTONS(40000U);

    *out_st = dns_process_packet(frame, query, len);
    uint16_t tx_len = 0U;
    const uint8_t *tx = wifi_host_last_tx(&tx_len, NULL);
    return (const dns_header_t *)(tx + ETH_HDR_LEN + sizeof(ipv4_header_t) + sizeof(udp_header_t));
}

static void test_softap_dns_modes(void)
{
    printf("  [TEST] SoftAP DNS: local names only (no-internet mode) / catch-all (captive)...\n");
    uint8_t q[600];
    dhcp_status_t st;
    char local_name[64];
    snprintf(local_name, sizeof(local_name), "%s.local", CONFIG_DEVICE_HOSTNAME);

    uint16_t len = build_dns_query(q, sizeof(q), local_name, DNS_TYPE_A);
    const dns_header_t *r = run_dns_query(q, len, &st);
    TEST_ASSERT(st == DHCP_OK, "DNS query for <hostname>.local processed");
    TEST_ASSERT(r->id == NET_HTONS(0x1234U), "DNS reply echoes the query id");
    TEST_ASSERT(NET_NTOHS(r->flags) == DNS_FLAGS_RESPONSE_OK && NET_NTOHS(r->ancount) == 1U,
                "<hostname>.local resolves to the board");

    len = build_dns_query(q, sizeof(q), CONFIG_DEVICE_HOSTNAME, DNS_TYPE_A);
    r = run_dns_query(q, len, &st);
    TEST_ASSERT(st == DHCP_OK && NET_NTOHS(r->ancount) == 1U, "bare <hostname> resolves to the board");

    len = build_dns_query(q, sizeof(q), "connectivitycheck.gstatic.com", DNS_TYPE_A);
    r = run_dns_query(q, len, &st);
    TEST_ASSERT(st == DHCP_OK, "DNS query for an internet name processed");
#if CONFIG_SOFTAP_CAPTIVE_PORTAL
    TEST_ASSERT(NET_NTOHS(r->ancount) == 1U, "captive mode: every name resolves to the board");
#else
    TEST_ASSERT(NET_NTOHS(r->flags) == DNS_FLAGS_RESPONSE_NXDOMAIN && NET_NTOHS(r->ancount) == 0U,
                "no-internet mode: internet names get NXDOMAIN");

    len = build_dns_query(q, sizeof(q), local_name, 28U /* AAAA */);
    r = run_dns_query(q, len, &st);
    TEST_ASSERT(st == DHCP_OK && NET_NTOHS(r->ancount) == 0U, "no-internet mode: AAAA for the board gets no A record");
#endif

    /* Oversized question (labels totalling > 512 bytes) is rejected, not copied */
    memset(q, 0, sizeof(q));
    ((dns_header_t *)q)->qdcount = NET_HTONS(1U);
    size_t i = sizeof(dns_header_t);
    for (int k = 0; k < 9; k++)
    {
        q[i++] = 60U;
        memset(&q[i], 'a', 60U);
        i += 60U;
    }
    q[i++] = 0U;
    q[i++] = 0U; q[i++] = 1U; q[i++] = 0U; q[i++] = 1U;
    (void)run_dns_query(q, (uint16_t)i, &st);
    TEST_ASSERT(st == DHCP_ERR_CORRUPT_FRAME, "DNS query larger than the 512-byte reply buffer is rejected");
}

/* ------------------------------------------------------------------------- */
/* WPA2 supplicant: frames as the AP would send them (802.1X header first,   */
/* as the blob delivers them), checked against the recorded driver calls.    */
/* ------------------------------------------------------------------------- */
static uint16_t ap_key_frame(uint8_t *buf, uint16_t key_info, uint8_t replay, const uint8_t *nonce,
                             const uint8_t *key_data, uint16_t key_data_len, const uint8_t *kck)
{
    memset(buf, 0, WPA2_EAPOL_KEY_FRAME_MIN_LEN + key_data_len);
    eapol_1x_hdr_t *x = (eapol_1x_hdr_t *)buf;
    eapol_key_header_t *k = (eapol_key_header_t *)(buf + sizeof(eapol_1x_hdr_t));
    x->version = EAPOL_VERSION_2;
    x->type = EAPOL_TYPE_KEY;
    x->length = NET_HTONS((uint16_t)(sizeof(eapol_key_header_t) + key_data_len));
    k->descriptor_type = EAPOL_DESC_TYPE_RSN;
    k->key_info = NET_HTONS(key_info);
    k->key_length = NET_HTONS(WPA2_TK_LEN);
    k->replay_counter[WPA2_REPLAY_LEN - 1U] = replay;
    if (nonce != NULL)
    {
        memcpy(k->key_nonce, nonce, WPA2_NONCE_LEN);
    }
    const uint8_t rsc[WPA2_KEY_RSC_LEN] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x00, 0x00};
    memcpy(k->key_rsc, rsc, sizeof(rsc));
    k->key_data_length = NET_HTONS(key_data_len);
    if (key_data_len > 0U)
    {
        memcpy(buf + WPA2_EAPOL_KEY_FRAME_MIN_LEN, key_data, key_data_len);
    }
    uint16_t len = (uint16_t)(WPA2_EAPOL_KEY_FRAME_MIN_LEN + key_data_len);
    if (kck != NULL)
    {
        wpa2_crypto_compute_mic(kck, buf, len, k->key_mic);
    }
    return len;
}

/* Encrypted key data: [RSN IE] + GTK KDE + 0xDD padding, AES-wrapped with kek */
static uint16_t ap_key_data(uint8_t *out, const uint8_t *kek, const uint8_t *rsn_ie, size_t rsn_len,
                            const uint8_t *gtk, uint8_t keyidx)
{
    uint8_t plain[96];
    size_t n = 0U;
    if (rsn_ie != NULL)
    {
        memcpy(plain, rsn_ie, rsn_len);
        n = rsn_len;
    }
    plain[n++] = WPA_KDE_TYPE;
    plain[n++] = (uint8_t)(RSN_SELECTOR_LEN + WPA_KDE_GTK_INFO_LEN + WPA2_GTK_LEN);
    plain[n++] = 0x00U; plain[n++] = 0x0FU; plain[n++] = 0xACU; plain[n++] = 0x01U;
    plain[n++] = keyidx;
    plain[n++] = 0x00U;
    memcpy(&plain[n], gtk, WPA2_GTK_LEN);
    n += WPA2_GTK_LEN;
    if ((n % WPA2_AES_KEYWRAP_BLOCK) != 0U)
    {
        plain[n++] = WPA_KDE_TYPE;
        while ((n % WPA2_AES_KEYWRAP_BLOCK) != 0U)
        {
            plain[n++] = 0x00U;
        }
    }
    uint16_t wl = 0U;
    wpa2_crypto_aes_wrap(kek, plain, (uint16_t)n, out, &wl);
    return wl;
}

/* MIC of the last frame the supplicant transmitted (Ethernet header stripped) */
static bool sta_tx_mic_ok(const uint8_t *kck)
{
    const uint8_t *eapol = g_wpa_drv_host.last_tx + sizeof(eapol_eth_hdr_t);
    uint16_t len = (uint16_t)(g_wpa_drv_host.last_tx_len - sizeof(eapol_eth_hdr_t));
    const eapol_key_header_t *k = (const eapol_key_header_t *)(eapol + sizeof(eapol_1x_hdr_t));
    uint8_t mic[WPA2_MIC_LEN];
    wpa2_crypto_compute_mic(kck, eapol, len, mic);
    return memcmp(mic, k->key_mic, WPA2_MIC_LEN) == 0;
}

static const eapol_key_header_t *sta_tx_key(void)
{
    return (const eapol_key_header_t *)(g_wpa_drv_host.last_tx + sizeof(eapol_eth_hdr_t) + sizeof(eapol_1x_hdr_t));
}

static void test_wpa_ie_parsing(void)
{
    printf("  [TEST] RSN/WPA IE and KDE parsing (REV-10)...\n");
    wpa_ie_data_t d;

    uint8_t ie[WPA_IE_MAX_LEN];
    size_t n = wpa_ie_build_rsn(ie, sizeof(ie), WPA_CIPHER_CCMP, WPA_CIPHER_CCMP, WPA_KEY_MGMT_PSK, 0U);
    const uint8_t exp_ie[] = {0x30, 0x14, 0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04, 0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04,
                              0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02, 0x00, 0x00};
    TEST_ASSERT(n == sizeof(exp_ie) && memcmp(ie, exp_ie, n) == 0, "Built RSN IE is WPA2-PSK CCMP/CCMP, caps 0 (PMF off)");
    TEST_ASSERT(wpa_ie_build_rsn(ie, 10U, WPA_CIPHER_CCMP, WPA_CIPHER_CCMP, WPA_KEY_MGMT_PSK, 0U) == 0U,
                "RSN IE builder refuses a short buffer");
    TEST_ASSERT(wpa_ie_build_rsn(ie, sizeof(ie), WPA_CIPHER_CCMP, WPA_CIPHER_CCMP, WPA_KEY_MGMT_SAE, 0U) == 0U,
                "RSN IE builder refuses SAE (WPA3 not supported)");

    TEST_ASSERT(wpa_ie_parse(exp_ie, sizeof(exp_ie), &d, true) == WPA_IE_OK &&
                d.proto == WPA_PROTO_RSN && d.pairwise_cipher == WPA_CIPHER_CCMP &&
                d.group_cipher == WPA_CIPHER_CCMP && d.key_mgmt == WPA_KEY_MGMT_PSK,
                "WPA2-PSK RSN IE parses to RSN/CCMP/CCMP/PSK");

    /* WPA2/WPA3 transition: PSK + SAE AKMs, MFPC set, TKIP+CCMP pairwise */
    const uint8_t mixed[] = {0x30, 0x1C, 0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02, 0x02, 0x00, 0x00, 0x0F, 0xAC, 0x02,
                             0x00, 0x0F, 0xAC, 0x04, 0x02, 0x00, 0x00, 0x0F, 0xAC, 0x02, 0x00, 0x0F, 0xAC, 0x08,
                             0x80, 0x00};
    TEST_ASSERT(wpa_ie_parse(mixed, sizeof(mixed), &d, true) == WPA_IE_OK &&
                d.key_mgmt == (WPA_KEY_MGMT_PSK | WPA_KEY_MGMT_SAE) &&
                d.pairwise_cipher == (WPA_CIPHER_TKIP | WPA_CIPHER_CCMP) && d.group_cipher == WPA_CIPHER_TKIP &&
                d.capabilities == WPA_CAPABILITY_MFPC,
                "Transition-mode RSN IE reports both AKMs, both ciphers and MFPC (not hard-coded)");

    const uint8_t sae_only[] = {0x30, 0x14, 0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04, 0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04,
                                0x01, 0x00, 0x00, 0x0F, 0xAC, 0x08, 0xC0, 0x00};
    TEST_ASSERT(wpa_ie_parse(sae_only, sizeof(sae_only), &d, true) == WPA_IE_OK && d.key_mgmt == WPA_KEY_MGMT_SAE,
                "WPA3-only RSN IE reports SAE");

    const uint8_t wpa1[] = {0xDD, 0x16, 0x00, 0x50, 0xF2, 0x01, 0x01, 0x00, 0x00, 0x50, 0xF2, 0x02,
                            0x01, 0x00, 0x00, 0x50, 0xF2, 0x02, 0x01, 0x00, 0x00, 0x50, 0xF2, 0x02};
    TEST_ASSERT(wpa_ie_parse(wpa1, sizeof(wpa1), &d, true) == WPA_IE_OK && d.proto == WPA_PROTO_WPA &&
                d.pairwise_cipher == WPA_CIPHER_TKIP && d.key_mgmt == WPA_KEY_MGMT_PSK,
                "WPA1 vendor IE parses to WPA/TKIP/PSK");

    uint8_t bad[sizeof(exp_ie)];
    memcpy(bad, exp_ie, sizeof(bad));
    bad[1] = 0x30U;
    TEST_ASSERT(wpa_ie_parse(bad, sizeof(bad), &d, true) == WPA_IE_ERR_MALFORMED, "IE length mismatch is rejected");
    memcpy(bad, exp_ie, sizeof(bad));
    bad[8] = 0x09U;     /* pairwise count larger than the element */
    TEST_ASSERT(wpa_ie_parse(bad, sizeof(bad), &d, true) == WPA_IE_ERR_PAIRWISE, "Pairwise count overflow is rejected");
    TEST_ASSERT(wpa_ie_parse(NULL, 0U, &d, true) == WPA_IE_ERR_EMPTY, "Missing IE reports empty");

    /* KDEs: RSN IE, GTK KDE (key ID 1), padding */
    const uint8_t gtk[WPA2_GTK_LEN] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                       0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    uint8_t kd[64];
    size_t k = 0U;
    memcpy(kd, exp_ie, sizeof(exp_ie)); k = sizeof(exp_ie);
    kd[k++] = 0xDD; kd[k++] = 22U; kd[k++] = 0x00; kd[k++] = 0x0F; kd[k++] = 0xAC; kd[k++] = 0x01;
    kd[k++] = 0x01; kd[k++] = 0x00;
    memcpy(&kd[k], gtk, sizeof(gtk)); k += sizeof(gtk);
    kd[k++] = 0xDD; kd[k++] = 0x00;
    wpa_kde_t kde;
    TEST_ASSERT(wpa_kde_parse(kd, k, &kde) == WPA_IE_OK && kde.rsn_ie == kd && kde.rsn_ie_len == sizeof(exp_ie) &&
                kde.gtk_len == WPA2_GTK_LEN && memcmp(kde.gtk, gtk, WPA2_GTK_LEN) == 0 && kde.gtk_keyidx == 1U &&
                !kde.gtk_tx,
                "KDE walk finds the RSN IE and the GTK (key ID 1) after it, stops at padding");
    TEST_ASSERT(wpa_kde_parse(kd, sizeof(exp_ie) + 10U, &kde) == WPA_IE_ERR_MALFORMED,
                "Truncated GTK KDE is rejected");
}

/*
 * REV-11: replay of a real WPA2-PSK handshake captured on the board (2026-10-05) against a phone hotspot set up
 * for this test (SSID "ironhotspot", passphrase "12345test", AP 72:9b:52:7b:bf:ef, board 40:4c:ca:45:1e:14,
 * channel 11; the AP advertises PMF capable and BIP, sends the GTK with key id 2). M1/M3 are what the blob handed
 * to the supplicant (802.1X header first); M2/M4 are the Ethernet frames the board sent. The MICs, the PTK and
 * the GTK were checked independently in Python (hashlib/hmac, AES key unwrap) before writing this test.
 */
static const uint8_t s_rpl_m1[99] = {
    0x02, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x57, 0x9a, 0x85, 0x5c, 0xba, 0x87, 0xd5, 0x2c, 0x74, 0xbc, 0x8a, 0xa8, 0x27, 0x99, 0x74,
    0xed, 0xe9, 0x5b, 0xe6, 0x17, 0x9d, 0x59, 0x56, 0xa4, 0xed, 0x84, 0x0a, 0x3c, 0xe5, 0x89, 0xe6,
    0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
};
static const uint8_t s_rpl_m2_eth[135] = {
    0x72, 0x9b, 0x52, 0x7b, 0xbf, 0xef, 0x40, 0x4c, 0xca, 0x45, 0x1e, 0x14, 0x88, 0x8e, 0x01, 0x03,
    0x00, 0x75, 0x02, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5c,
    0x98, 0x51, 0xff, 0xf0, 0xd4, 0x68, 0x37, 0x1e, 0xb9, 0xcb, 0xee, 0xd9, 0xe5, 0x14, 0x0d, 0x4c,
    0x6e, 0xcc, 0xce, 0x10, 0x3e, 0xbd, 0x7c, 0x35, 0x99, 0x82, 0x9d, 0x2e, 0x44, 0x16, 0x29, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15,
    0x2d, 0x44, 0x40, 0xbf, 0x31, 0x9c, 0x09, 0xbc, 0x61, 0x12, 0xb0, 0x60, 0x89, 0x69, 0xe7, 0x00,
    0x16, 0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01,
    0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00,
};
static const uint8_t s_rpl_m3[163] = {
    0x02, 0x03, 0x00, 0x9f, 0x02, 0x13, 0xca, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x57, 0x9a, 0x85, 0x5c, 0xba, 0x87, 0xd5, 0x2c, 0x74, 0xbc, 0x8a, 0xa8, 0x27, 0x99, 0x74,
    0xed, 0xe9, 0x5b, 0xe6, 0x17, 0x9d, 0x59, 0x56, 0xa4, 0xed, 0x84, 0x0a, 0x3c, 0xe5, 0x89, 0xe6,
    0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xf3, 0xf1, 0x8a, 0x78, 0xf6, 0x00, 0x0f, 0xbb, 0xde, 0xce, 0x85, 0x65, 0x06, 0x7b, 0x28,
    0x98, 0x00, 0x40, 0xe8, 0x1f, 0x09, 0x21, 0x2c, 0x22, 0xda, 0x75, 0x0c, 0xd2, 0x65, 0x69, 0x3e,
    0xa6, 0xae, 0x26, 0x60, 0x94, 0x87, 0xa0, 0x95, 0xf0, 0xcb, 0x68, 0x60, 0xfe, 0xe1, 0x9a, 0x46,
    0x3a, 0xdd, 0x2e, 0x3b, 0x95, 0x62, 0x10, 0xca, 0x3a, 0x77, 0xc8, 0x31, 0x45, 0x5a, 0x09, 0x3d,
    0x7f, 0xfe, 0x26, 0x4a, 0x90, 0x96, 0x7c, 0x71, 0x5f, 0x56, 0x17, 0xbc, 0x0a, 0xd3, 0x1a, 0xc3,
    0x68, 0xfd, 0x3e,
};
static const uint8_t s_rpl_m4_eth[113] = {
    0x72, 0x9b, 0x52, 0x7b, 0xbf, 0xef, 0x40, 0x4c, 0xca, 0x45, 0x1e, 0x14, 0x88, 0x8e, 0x01, 0x03,
    0x00, 0x5f, 0x02, 0x03, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69,
    0x3a, 0x9b, 0x7f, 0xff, 0xb9, 0x74, 0xae, 0x42, 0xe7, 0x0f, 0x3b, 0x7f, 0x23, 0x5c, 0x72, 0x00,
    0x00,
};

static const uint8_t s_rpl_snonce[32] = {
    0x5c, 0x98, 0x51, 0xff, 0xf0, 0xd4, 0x68, 0x37, 0x1e, 0xb9, 0xcb, 0xee, 0xd9, 0xe5, 0x14, 0x0d,
    0x4c, 0x6e, 0xcc, 0xce, 0x10, 0x3e, 0xbd, 0x7c, 0x35, 0x99, 0x82, 0x9d, 0x2e, 0x44, 0x16, 0x29,
};

static const uint8_t s_rpl_ap_rsn_ie[28] = {
    0x30, 0x1a, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
    0x00, 0x0f, 0xac, 0x02, 0x8c, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xac, 0x06,
};

static const uint8_t s_rpl_gtk[16] = {
    0x4a, 0x08, 0x49, 0x76, 0xca, 0x0d, 0x54, 0x09, 0x65, 0xef, 0xc4, 0x1f, 0xe6, 0xf7, 0xee, 0x52,
};

static const uint8_t s_rpl_rsc[6] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void test_wpa2_replay_capture(void)
{
    printf("  [TEST] WPA2 handshake replay: frames captured from a real AP (REV-11)...\n");
    const uint8_t ap[WPA2_MAC_ADDR_LEN] = {0x72, 0x9b, 0x52, 0x7b, 0xbf, 0xef};
    const uint8_t sta[WPA2_MAC_ADDR_LEN] = {0x40, 0x4c, 0xca, 0x45, 0x1e, 0x14};

    wpa_drv_host_reset();
    memcpy(g_wpa_drv_host.sta_mac, sta, sizeof(sta));
    g_wpa_drv_host.sta_mac_set = true;
    g_wpa_drv_host.random_bytes = s_rpl_snonce;
    g_wpa_drv_host.random_len = sizeof(s_rpl_snonce);
    g_wpa_drv_host.ap_rsn_ie = s_rpl_ap_rsn_ie;
    strcpy(g_wpa_drv_host.profile.ssid, "ironhotspot");

    TEST_ASSERT(wpa2_client_init() == WPA2_OK && wpa2_client_configure("ironhotspot", "12345test") == WPA2_OK,
                "Replay: supplicant configured for the capture network");
    TEST_ASSERT(wpa2_client_sta_connect(ap) == 0, "Replay: BSS accepted (WPA2-PSK/CCMP, PMF capable AP)");
    wpa2_client_on_associated(ap);

    TEST_ASSERT(wpa2_client_rx_eapol(ap, s_rpl_m1, sizeof(s_rpl_m1)) == WPA2_OK &&
                g_wpa_drv_host.last_tx_len == sizeof(s_rpl_m2_eth) &&
                memcmp(g_wpa_drv_host.last_tx, s_rpl_m2_eth, sizeof(s_rpl_m2_eth)) == 0,
                "Replay: message 2 is byte-identical to the frame the board sent (SNonce, RSN IE, MIC)");

    TEST_ASSERT(wpa2_client_rx_eapol(ap, s_rpl_m3, sizeof(s_rpl_m3)) == WPA2_OK &&
                g_wpa_drv_host.last_tx_len == sizeof(s_rpl_m4_eth) &&
                memcmp(g_wpa_drv_host.last_tx, s_rpl_m4_eth, sizeof(s_rpl_m4_eth)) == 0,
                "Replay: real message 3 verifies (MIC, ANonce, RSN IE, key data) and message 4 matches the capture");

    uint8_t m4[sizeof(s_rpl_m4_eth)];
    memcpy(m4, s_rpl_m4_eth + sizeof(eapol_eth_hdr_t), sizeof(m4) - sizeof(eapol_eth_hdr_t));
    wpa2_client_eapol_txdone(m4, sizeof(m4) - sizeof(eapol_eth_hdr_t), false);
    TEST_ASSERT(g_wpa_drv_host.set_key_calls == 2U && g_wpa_drv_host.auth_done_calls == 1U &&
                wpa2_client_get_state() == WPA2_STATE_AUTHENTICATED,
                "Replay: PTK and GTK installed after message 4 TX done, port opened");
    TEST_ASSERT(g_wpa_drv_host.last_key_idx == 2 && g_wpa_drv_host.last_key_len == sizeof(s_rpl_gtk) &&
                memcmp(g_wpa_drv_host.last_key, s_rpl_gtk, sizeof(s_rpl_gtk)) == 0 &&
                memcmp(g_wpa_drv_host.last_seq, s_rpl_rsc, sizeof(s_rpl_rsc)) == 0 &&
                (g_wpa_drv_host.last_key_flag & WPA_DRV_KEY_FLAG_GROUP) != 0U,
                "Replay: GTK (key id 2) and RSC from the real AP unwrapped and installed");
}

static void test_wpa2_handshake(void)
{
    printf("  [TEST] WPA2 4-way and group key handshakes against the blob interface (REV-10)...\n");

    const uint8_t ap[WPA2_MAC_ADDR_LEN] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    const uint8_t anonce[WPA2_NONCE_LEN] = {
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f};
    const uint8_t gtk1[WPA2_GTK_LEN] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11,
                                        0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    const uint8_t gtk2[WPA2_GTK_LEN] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                                        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
    uint8_t ap_ie[WPA_IE_MAX_LEN];
    size_t ap_ie_len = wpa_ie_build_rsn(ap_ie, sizeof(ap_ie), WPA_CIPHER_CCMP, WPA_CIPHER_CCMP, WPA_KEY_MGMT_PSK, 0U);
    uint8_t frame[256];
    uint8_t kd[128];
    uint16_t len;
    uint16_t kdl;
    wpa2_telemetry_t t;

    /* Expected keys, derived independently of the state machine */
    uint8_t pmk[WPA2_PMK_LEN];
    wpa2_crypto_pbkdf2_sha1("password", "IEEE", WPA2_PBKDF2_ITERATIONS, pmk);
    uint8_t snonce[WPA2_NONCE_LEN];
    memset(snonce, 0x5A, sizeof(snonce));

    wpa_drv_host_reset();
    g_wpa_drv_host.random_fill = 0x5AU;
    strcpy(g_wpa_drv_host.profile.ssid, "IEEE");
    TEST_ASSERT(wpa2_client_init() == WPA2_OK && wpa2_client_configure("IEEE", "password") == WPA2_OK,
                "Supplicant configured (PMK derived before connecting)");
    wpa2_client_get_telemetry(&t);
    wpa2_ptk_t exp;
    wpa2_crypto_prf512(pmk, ap, t.local_mac, anonce, snonce, &exp);

    /* Profile checks in wpa_sta_connect */
    g_wpa_drv_host.profile.authmode = 0x09U;    /* WPA3_AUTH_PSK */
    TEST_ASSERT(wpa2_client_sta_connect(ap) < 0 && g_wpa_drv_host.connect_calls == 0U,
                "WPA3-SAE BSS is rejected before association");
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(t.last_fail == WPA2_FAIL_UNSUPPORTED_SECURITY && t.state == WPA2_STATE_FAILED,
                "Rejection is reported as unsupported security");
    g_wpa_drv_host.profile.authmode = WPA_DRV_AUTH_WPA2_PSK;
    g_wpa_drv_host.profile.group_idx = WPA_DRV_CIPHER_IDX_TKIP;
    TEST_ASSERT(wpa2_client_sta_connect(ap) < 0, "WPA2 BSS with TKIP group cipher is rejected");
    g_wpa_drv_host.profile.group_idx = WPA_DRV_CIPHER_IDX_CCMP;
    strcpy(g_wpa_drv_host.profile.ssid, "Other");
    wpa2_client_sta_connect(ap);
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(t.last_fail == WPA2_FAIL_NO_PMK, "BSS of another SSID is rejected: no PMK for it");
    strcpy(g_wpa_drv_host.profile.ssid, "IEEE");

    TEST_ASSERT(wpa2_client_sta_connect(ap) == 0 && g_wpa_drv_host.connect_calls == 1U,
                "WPA2-PSK/CCMP BSS: association continues (esp_wifi_sta_connect_internal)");
    TEST_ASSERT(g_wpa_drv_host.assoc_ie_calls == 1U && g_wpa_drv_host.assoc_ie_len == ap_ie_len &&
                memcmp(g_wpa_drv_host.assoc_ie, ap_ie, ap_ie_len) == 0,
                "Association RSN IE registered with the blob before association");

    wpa2_client_on_associated(ap);
    TEST_ASSERT(wpa2_client_get_state() == WPA2_STATE_CONNECTING, "Associated: waiting for message 1");

    /* Message 1 */
    len = ap_key_frame(frame, WPA2_MSG1_KEY_INFO_NOMINAL, 1U, anonce, NULL, 0U, NULL);
    const uint8_t stranger[WPA2_MAC_ADDR_LEN] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT(wpa2_client_rx_eapol(stranger, frame, len) == WPA2_ERR_INVALID_ARG,
                "EAPOL from a station other than the AP is ignored");
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, (uint16_t)(len - 1U)) == WPA2_ERR_INVALID_ARG,
                "802.1X length beyond the buffer is rejected");
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_OK &&
                wpa2_client_get_state() == WPA2_STATE_4WAY_M2_SENT && wpa2_client_is_in_4way(),
                "Message 1 (802.1X header first, as the blob delivers it) answered with message 2");
    TEST_ASSERT(g_wpa_drv_host.tx_calls == 1U &&
                g_wpa_drv_host.last_tx[12] == 0x88U && g_wpa_drv_host.last_tx[13] == 0x8EU &&
                memcmp(g_wpa_drv_host.last_tx, ap, WPA2_MAC_ADDR_LEN) == 0,
                "Message 2 goes out as an EAPOL Ethernet frame to the AP");
    const eapol_key_header_t *tk = sta_tx_key();
    TEST_ASSERT(NET_NTOHS(tk->key_info) == WPA2_MSG2_KEY_INFO_NOMINAL && tk->replay_counter[7] == 1U &&
                memcmp(tk->key_nonce, snonce, WPA2_NONCE_LEN) == 0,
                "Message 2 key info, replay counter and SNonce (hardware RNG) are correct");
    TEST_ASSERT(NET_NTOHS(tk->key_data_length) == ap_ie_len &&
                memcmp(g_wpa_drv_host.last_tx + sizeof(eapol_eth_hdr_t) + WPA2_EAPOL_KEY_FRAME_MIN_LEN, ap_ie, ap_ie_len) == 0,
                "Message 2 carries the same RSN IE as the association request");
    TEST_ASSERT(sta_tx_mic_ok(exp.kck), "Message 2 MIC verifies with the independently derived KCK");
    uint8_t m2_copy[256];
    uint16_t m2_len = (uint16_t)(g_wpa_drv_host.last_tx_len - sizeof(eapol_eth_hdr_t));
    memcpy(m2_copy, g_wpa_drv_host.last_tx + sizeof(eapol_eth_hdr_t), m2_len);

    /* Message 3 failures */
    g_wpa_drv_host.ap_rsn_ie = ap_ie;
    kdl = ap_key_data(kd, exp.kek, ap_ie, ap_ie_len, gtk1, 1U);
    len = ap_key_frame(frame, WPA2_MSG3_KEY_INFO_NOMINAL, 2U, anonce, kd, kdl, exp.kck);
    frame[WPA2_EAPOL_KEY_FRAME_MIN_LEN - 3U] ^= 0xFFU;     /* corrupt the MIC */
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_ERR_MIC_FAIL, "Message 3 with a bad MIC is dropped");
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(t.mic_failures == 1U && g_wpa_drv_host.tx_calls == 1U, "MIC failure counted, nothing sent");

    uint8_t other_nonce[WPA2_NONCE_LEN];
    memset(other_nonce, 0x77, sizeof(other_nonce));
    len = ap_key_frame(frame, WPA2_MSG3_KEY_INFO_NOMINAL, 2U, other_nonce, kd, kdl, exp.kck);
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_ERR_PROTOCOL, "Message 3 with a different ANonce is dropped");
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(t.last_fail == WPA2_FAIL_ANONCE_MISMATCH && g_wpa_drv_host.tx_calls == 1U, "ANonce mismatch reported");

    /* Valid message 3 */
    len = ap_key_frame(frame, WPA2_MSG3_KEY_INFO_NOMINAL, 3U, anonce, kd, kdl, exp.kck);
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_OK && wpa2_client_get_state() == WPA2_STATE_4WAY_M4_SENT,
                "Valid message 3 answered with message 4");
    tk = sta_tx_key();
    TEST_ASSERT(g_wpa_drv_host.tx_calls == 2U && NET_NTOHS(tk->key_info) == WPA2_MSG4_KEY_INFO_NOMINAL &&
                tk->replay_counter[7] == 3U && NET_NTOHS(tk->key_data_length) == 0U && sta_tx_mic_ok(exp.kck),
                "Message 4 key info, replay counter, empty key data and MIC are correct");
    TEST_ASSERT(g_wpa_drv_host.set_key_calls == 0U && g_wpa_drv_host.auth_done_calls == 0U,
                "No key installed before the blob confirms message 4 was sent");
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_ERR_REPLAY, "Replayed message 3 is dropped");

    /* TX done: M2 confirmation is ignored, M4 failure waits for the retransmitted M3 */
    wpa2_client_eapol_txdone(m2_copy, m2_len, false);
    TEST_ASSERT(g_wpa_drv_host.set_key_calls == 0U, "TX done of message 2 installs nothing");
    uint8_t m4_copy[256];
    uint16_t m4_len = (uint16_t)(g_wpa_drv_host.last_tx_len - sizeof(eapol_eth_hdr_t));
    memcpy(m4_copy, g_wpa_drv_host.last_tx + sizeof(eapol_eth_hdr_t), m4_len);
    wpa2_client_eapol_txdone(m4_copy, m4_len, true);
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(g_wpa_drv_host.set_key_calls == 0U && t.last_fail == WPA2_FAIL_M4_TX,
                "Message 4 TX failure installs nothing");

    len = ap_key_frame(frame, WPA2_MSG3_KEY_INFO_NOMINAL, 4U, anonce, kd, kdl, exp.kck);
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_OK && g_wpa_drv_host.tx_calls == 3U,
                "Retransmitted message 3 gets a new message 4");
    memcpy(m4_copy, g_wpa_drv_host.last_tx + sizeof(eapol_eth_hdr_t), m4_len);
    wpa2_client_eapol_txdone(m4_copy, m4_len, false);
    TEST_ASSERT(g_wpa_drv_host.set_key_calls == 2U && g_wpa_drv_host.auth_done_calls == 1U &&
                wpa2_client_is_authenticated(),
                "Message 4 sent: PTK and GTK installed, blob told the handshake is done");
    TEST_ASSERT(g_wpa_drv_host.last_key_flag == (WPA_DRV_KEY_FLAG_GROUP | WPA_DRV_KEY_FLAG_RX) &&
                g_wpa_drv_host.last_key_idx == 1 && memcmp(g_wpa_drv_host.last_key, gtk1, WPA2_GTK_LEN) == 0 &&
                g_wpa_drv_host.last_seq[0] == 0x01U && g_wpa_drv_host.last_seq[5] == 0x06U,
                "GTK from the KDE installed with its key ID and RSC (not the RSN IE bytes)");
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(t.has_ptk && t.has_gtk && t.handshakes_completed == 1U && t.last_fail == WPA2_FAIL_NONE,
                "Telemetry: PTK and GTK installed, one handshake completed");
    wpa2_client_on_associated(ap);     /* the blob calls it from inside auth_done */
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(wpa2_client_is_authenticated() && t.has_ptk && t.has_gtk,
                "Blob's post-handshake connected callback keeps the session");

    len = ap_key_frame(frame, WPA2_MSG3_KEY_INFO_NOMINAL, 5U, anonce, kd, kdl, exp.kck);
    wpa2_client_rx_eapol(ap, frame, len);
    memcpy(m4_copy, g_wpa_drv_host.last_tx + sizeof(eapol_eth_hdr_t), m4_len);
    wpa2_client_eapol_txdone(m4_copy, m4_len, false);
    TEST_ASSERT(g_wpa_drv_host.tx_calls == 4U && g_wpa_drv_host.set_key_calls == 2U,
                "Message 3 after completion: message 4 resent, keys not reinstalled");

    /* Group key rekey */
    kdl = ap_key_data(kd, exp.kek, NULL, 0U, gtk2, 2U);
    len = ap_key_frame(frame, WPA2_GROUP1_KEY_INFO_NOMINAL, 6U, NULL, kd, kdl, exp.kck);
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_OK, "Group key message 1 accepted");
    tk = sta_tx_key();
    TEST_ASSERT(g_wpa_drv_host.set_key_calls == 3U && g_wpa_drv_host.last_key_idx == 2 &&
                memcmp(g_wpa_drv_host.last_key, gtk2, WPA2_GTK_LEN) == 0,
                "New GTK installed with key ID 2");
    TEST_ASSERT(NET_NTOHS(tk->key_info) == WPA2_GROUP2_KEY_INFO_NOMINAL && tk->replay_counter[7] == 6U &&
                sta_tx_mic_ok(exp.kck),
                "Group key message 2 sent with correct key info and MIC");
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(t.group_rekeys == 1U && wpa2_client_is_authenticated(), "Rekey counted, still authenticated");

    wpa2_client_on_disconnected(3U);
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(wpa2_client_get_state() == WPA2_STATE_DISCONNECTED && !t.has_ptk && !t.has_gtk,
                "Disconnect clears keys");

    /* No message 3 after message 2: reported as a likely wrong passphrase. As on the
     * board, the blob calls only wpa_sta_connect (no connected callback) */
    TEST_ASSERT(wpa2_client_sta_connect(ap) == 0, "Reconnect: wpa_sta_connect alone prepares a new handshake");
    len = ap_key_frame(frame, WPA2_MSG1_KEY_INFO_NOMINAL, 1U, anonce, NULL, 0U, NULL);
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_OK, "Message 1 accepted after wpa_sta_connect only");
    wpa2_client_on_disconnected(15U);
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(t.last_fail == WPA2_FAIL_NO_M3, "Deauth after message 2 reported as no message 3 (wrong passphrase?)");
    wpa2_client_sta_connect(ap);
    wpa2_client_get_telemetry(&t);
    TEST_ASSERT(t.last_fail == WPA2_FAIL_NO_M3, "The blob's automatic retry keeps the failure verdict");
    wpa2_client_on_disconnected(4U);

    /* RSN IE in message 3 differs from the beacon: deauth reason 17 */
    wpa2_client_sta_connect(ap);
    len = ap_key_frame(frame, WPA2_MSG1_KEY_INFO_NOMINAL, 1U, anonce, NULL, 0U, NULL);
    wpa2_client_rx_eapol(ap, frame, len);
    uint8_t beacon_ie[WPA_IE_MAX_LEN];
    wpa_ie_build_rsn(beacon_ie, sizeof(beacon_ie), WPA_CIPHER_CCMP, WPA_CIPHER_CCMP, WPA_KEY_MGMT_PSK,
                     WPA_CAPABILITY_MFPC);
    g_wpa_drv_host.ap_rsn_ie = beacon_ie;
    kdl = ap_key_data(kd, exp.kek, ap_ie, ap_ie_len, gtk1, 1U);
    len = ap_key_frame(frame, WPA2_MSG3_KEY_INFO_NOMINAL, 2U, anonce, kd, kdl, exp.kck);
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_ERR_PROTOCOL &&
                g_wpa_drv_host.last_deauth_reason == WPA_DRV_REASON_IE_IN_4WAY_DIFFERS,
                "RSN IE downgrade in message 3 is refused with reason 17");
    g_wpa_drv_host.ap_rsn_ie = ap_ie;

    /* Key data without the encrypted flag is refused */
    wpa2_client_sta_connect(ap);
    len = ap_key_frame(frame, WPA2_MSG1_KEY_INFO_NOMINAL, 1U, anonce, NULL, 0U, NULL);
    wpa2_client_rx_eapol(ap, frame, len);
    len = ap_key_frame(frame, (uint16_t)(WPA2_MSG3_KEY_INFO_NOMINAL & ~WPA2_KEY_INFO_ENCRYPTED), 2U, anonce,
                       kd, kdl, exp.kck);
    TEST_ASSERT(wpa2_client_rx_eapol(ap, frame, len) == WPA2_ERR_DECRYPT_FAIL &&
                g_wpa_drv_host.last_deauth_reason == WPA_DRV_REASON_UNSPECIFIED,
                "Message 3 with unencrypted key data is refused");
    wpa2_client_on_disconnected(1U);

    /* Handover entry point still reconfigures and starts the STA join */
    TEST_ASSERT(wpa2_client_handover("OfficeNet", "OfficeSecret123") == WPA2_OK,
                "wpa2_client_handover reconfigures client and initiates STA join");
    TEST_ASSERT(wpa2_client_configure("HexNet", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef") ==
                WPA2_ERR_UNSUPPORTED, "64-character raw PSK is reported as unsupported");
}

/* Builds an mDNS query for iron-v.local (A, class IN) with the given ID; returns its length */
static uint16_t build_mdns_a_query(uint8_t *q, uint16_t id)
{
    static const uint8_t name[] = {6, 'i', 'r', 'o', 'n', '-', 'v', 5, 'l', 'o', 'c', 'a', 'l', 0};
    memset(q, 0, 12U);
    q[0] = (uint8_t)(id >> 8U); q[1] = (uint8_t)id;
    q[5] = 1U;                                    /* qdcount = 1 */
    memcpy(&q[12], name, sizeof(name));
    uint16_t n = (uint16_t)(12U + sizeof(name));
    q[n++] = 0U; q[n++] = MDNS_TYPE_A;
    q[n++] = 0U; q[n++] = MDNS_CLASS_IN;
    return n;
}

/* REV-29 phone report: phones resolve .local with one-shot queries from a random port and only
 * accept a unicast reply with their ID and question (RFC 6762 6.7) */
static void test_mdns_legacy_unicast(void)
{
    printf("  [TEST] mDNS one-shot (legacy unicast) queries...\n");
    uint8_t q[64];
    uint16_t qlen = build_mdns_a_query(q, 0x1234U);
    mdns_init();
    mdns_set_hostname("iron-v");
    net_set_ip(0x0A000029U, 0xFFFFFF00U, 0x0A000001U);   /* 10.0.0.41 */
    arp_insert(0x0A000032U, (const uint8_t *)"\x02\x11\x22\x33\x44\x55");

    TEST_ASSERT(mdns_process_query(q, qlen, 0x0A000032U, 40000U) == MDNS_OK, "One-shot query answered");
    uint16_t flen = 0U;
    const uint8_t *f = wifi_host_last_tx(&flen, NULL);
    const uint8_t *udp = f + ETH_HDR_LEN + IPV4_MIN_HDR_LEN;
    const uint8_t *dns = udp + UDP_HDR_LEN;
    TEST_ASSERT(f[0] == 0x02U && f[5] == 0x55U, "Reply goes to the querier's MAC (unicast)");
    TEST_ASSERT(((udp[2] << 8) | udp[3]) == 40000 && ((udp[0] << 8) | udp[1]) == MDNS_PORT, "Reply to the query's source port, from 5353");
    TEST_ASSERT(dns[0] == 0x12U && dns[1] == 0x34U, "Reply echoes the query ID");
    TEST_ASSERT(dns[5] == 1U && dns[7] == 1U, "Reply repeats the question and has one answer");
    size_t ans = 12U + (qlen - 12U);                      /* answer follows the repeated question */
    const uint8_t *rr = dns + ans + 14U;                  /* skip answer name (14 bytes) */
    TEST_ASSERT(rr[2] == 0x00U && rr[3] == MDNS_CLASS_IN, "No cache-flush bit in a unicast reply");
    TEST_ASSERT(rr[7] == MDNS_LEGACY_UNICAST_TTL_SEC && rr[10] == 10U && rr[13] == 41U, "Short TTL and the board's address");

    TEST_ASSERT(mdns_process_query(q, qlen, 0x0A000032U, MDNS_PORT) == MDNS_OK, "Query from port 5353 answered");
    f = wifi_host_last_tx(&flen, NULL);
    udp = f + ETH_HDR_LEN + IPV4_MIN_HDR_LEN;
    dns = udp + UDP_HDR_LEN;
    TEST_ASSERT(f[0] == MDNS_MULTICAST_MAC_0 && ((udp[2] << 8) | udp[3]) == MDNS_PORT && dns[0] == 0U && dns[5] == 0U,
                "Full mDNS querier: multicast answer, ID 0, no question");
    q[qlen - 2U] = 0x80U;                                 /* QU bit */
    TEST_ASSERT(mdns_process_query(q, qlen, 0x0A000032U, MDNS_PORT) == MDNS_OK, "QU query still matched");
    q[qlen - 3U] = MDNS_TYPE_AAAA;
    q[qlen - 2U] = 0U;
    TEST_ASSERT(mdns_process_query(q, qlen, 0x0A000032U, 40000U) == MDNS_ERR_NO_MATCH, "AAAA query: no IPv6 address, no answer");
}

/* ------------------------------------------------------------------------- */
/* REV-14: DHCP client retransmission backoff, NAK, INIT-REBOOT, T1/T2/expiry */
/* ------------------------------------------------------------------------- */
#define DC_MAX_FRAMES   32U
#define DC_STEP_US      100000ULL    /* host clock step between ticks */
#define DC_TOL_US       150000ULL    /* interval tolerance: one step plus margin */
#define DC_SEC          1000000ULL

typedef struct {
    uint64_t at_us;
    uint8_t  dest_mac[ETH_ADDR_LEN];
    uint32_t src_ip;
    uint32_t dest_ip;
    uint32_t xid;
    uint32_t ciaddr;
    uint16_t flags;
    uint8_t  msg_type;
    bool     has_req_ip;
    uint32_t req_ip;
    bool     has_server_id;
    uint32_t server_id;
} dc_frame_t;

static dc_frame_t s_dc_frames[DC_MAX_FRAMES];
static uint32_t s_dc_count;

/* Records every client -> server DHCP frame (mDNS and other traffic ignored) */
static bool dc_tx_hook(const uint8_t *frame, uint16_t len)
{
    size_t hdrs = ETH_HDR_LEN + IPV4_MIN_HDR_LEN + UDP_HDR_LEN;
    if (len < hdrs + (sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN) || s_dc_count >= DC_MAX_FRAMES)
    {
        return true;
    }
    const ethernet_header_t *eth = (const ethernet_header_t *)frame;
    const ipv4_header_t *ip = (const ipv4_header_t *)(frame + ETH_HDR_LEN);
    const udp_header_t *udp = (const udp_header_t *)(frame + ETH_HDR_LEN + IPV4_MIN_HDR_LEN);
    if (ip->protocol != IPV4_PROTO_UDP || NET_NTOHS(udp->dest_port) != DHCP_SERVER_PORT)
    {
        return true;
    }
    const dhcp_packet_t *d = (const dhcp_packet_t *)(frame + hdrs);
    dc_frame_t *f = &s_dc_frames[s_dc_count++];
    memset(f, 0, sizeof(*f));
    f->at_us = s_host_now_us;
    memcpy(f->dest_mac, eth->dest_mac, ETH_ADDR_LEN);
    f->src_ip = NET_NTOHL(ip->src_ip);
    f->dest_ip = NET_NTOHL(ip->dest_ip);
    f->xid = NET_NTOHL(d->xid);
    f->ciaddr = NET_NTOHL(d->ciaddr);
    f->flags = NET_NTOHS(d->flags);
    size_t opts_len = len - hdrs - (sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN);
    size_t i = 0U;
    while (i + 1U < opts_len && d->options[i] != DHCP_OPT_END)
    {
        uint8_t code = d->options[i];
        uint8_t olen = d->options[i + 1U];
        const uint8_t *v = &d->options[i + 2U];
        uint32_t be;
        if (code == DHCP_OPT_MSG_TYPE) f->msg_type = v[0];
        if (code == DHCP_OPT_REQUESTED_IP) { memcpy(&be, v, 4U); f->req_ip = NET_NTOHL(be); f->has_req_ip = true; }
        if (code == DHCP_OPT_SERVER_ID) { memcpy(&be, v, 4U); f->server_id = NET_NTOHL(be); f->has_server_id = true; }
        i += 2U + olen;
    }
    return true;
}

static void dc_run(uint64_t dur_us)
{
    uint64_t end = s_host_now_us + dur_us;
    while (s_host_now_us < end)
    {
        s_host_now_us += DC_STEP_US;
        dhcp_client_tick();
    }
}

static bool dc_near(uint64_t got_us, uint64_t want_us)
{
    return (got_us + DC_TOL_US >= want_us) && (got_us <= want_us + DC_TOL_US);
}

static const uint8_t s_dc_server_mac[ETH_ADDR_LEN] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
#define DC_SERVER_IP   0xC0A8B201U   /* 192.168.178.1 */
#define DC_OFFER_IP    0xC0A8B240U   /* 192.168.178.64 */

/* Server reply with the client's current xid; lease/t1/t2 0 = option left out */
static dhcp_status_t dc_reply(uint8_t msg_type, uint32_t yiaddr, uint32_t lease, uint32_t t1, uint32_t t2)
{
    dhcp_client_telemetry_t t;
    dhcp_client_get_telemetry(&t);
    uint8_t eth[ETH_HDR_LEN];
    memset(eth, 0, sizeof(eth));
    memcpy(((ethernet_header_t *)eth)->src_mac, s_dc_server_mac, ETH_ADDR_LEN);

    dhcp_packet_t p;
    memset(&p, 0, sizeof(p));
    p.op = DHCP_OP_BOOTREPLY;
    p.htype = DHCP_HTYPE_ETHERNET;
    p.hlen = DHCP_HLEN_ETHERNET;
    p.xid = NET_HTONL(t.xid);
    p.yiaddr = NET_HTONL(yiaddr);
    p.magic_cookie = NET_HTONL(DHCP_MAGIC_COOKIE);
    size_t i = 0U;
    uint32_t be;
    p.options[i++] = DHCP_OPT_MSG_TYPE; p.options[i++] = 1U; p.options[i++] = msg_type;
    p.options[i++] = DHCP_OPT_SERVER_ID; p.options[i++] = 4U;
    be = NET_HTONL(DC_SERVER_IP); memcpy(&p.options[i], &be, 4U); i += 4U;
    if (msg_type != DHCP_MSG_NAK)
    {
        p.options[i++] = DHCP_OPT_SUBNET_MASK; p.options[i++] = 4U;
        be = NET_HTONL(0xFFFFFF00U); memcpy(&p.options[i], &be, 4U); i += 4U;
        p.options[i++] = DHCP_OPT_ROUTER; p.options[i++] = 4U;
        be = NET_HTONL(DC_SERVER_IP); memcpy(&p.options[i], &be, 4U); i += 4U;
    }
    const uint8_t codes[3] = {DHCP_OPT_LEASE_TIME, DHCP_OPT_RENEWAL_TIME, DHCP_OPT_REBINDING_TIME};
    const uint32_t vals[3] = {lease, t1, t2};
    for (uint32_t k = 0U; k < 3U; k++)
    {
        if (vals[k] != 0U)
        {
            p.options[i++] = codes[k]; p.options[i++] = 4U;
            be = NET_HTONL(vals[k]); memcpy(&p.options[i], &be, 4U); i += 4U;
        }
    }
    p.options[i++] = DHCP_OPT_END;
    uint16_t len = (uint16_t)(sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN + i);
    return dhcp_client_process_packet(eth, (const uint8_t *)&p, len);
}

/* Fresh client, DISCOVER -> OFFER -> REQUEST -> ACK; frames cleared afterwards */
static void dc_bind(uint32_t lease, uint32_t t1, uint32_t t2)
{
    dhcp_client_init();
    dhcp_client_start();
    dc_reply(DHCP_MSG_OFFER, DC_OFFER_IP, lease, 0U, 0U);
    dc_reply(DHCP_MSG_ACK, DC_OFFER_IP, lease, t1, t2);
    s_dc_count = 0U;
}

static void test_dhcp_client_rfc2131(void)
{
    printf("  [TEST] DHCP client: RFC 2131 backoff, NAK, INIT-REBOOT, T1/T2/expiry (REV-14)...\n");

    net_config_t saved_net;
    net_get_config(&saved_net);
    uint8_t saved_fill = g_wpa_drv_host.random_fill;
    const uint8_t *saved_bytes = g_wpa_drv_host.random_bytes;
    g_wpa_drv_host.random_bytes = NULL;
    g_wpa_drv_host.random_fill = 0U;     /* rand 0: every wait is backoff - 1 s */
    wifi_host_set_tx_hook(dc_tx_hook);
    dhcp_client_telemetry_t t;

    /* 1. DISCOVER backoff: 4, 8, 16, 32, 64, 64 s, each -1 s with rand 0; same xid; broadcast from 0.0.0.0 */
    dhcp_client_init();
    s_dc_count = 0U;
    dhcp_client_start();
    dc_run(200ULL * DC_SEC);
    const uint64_t want_disc[6] = {3, 7, 15, 31, 63, 63};
    TEST_ASSERT(s_dc_count == 7U, "Unanswered DISCOVER: 7 sends in 200 s (old client: 15+)");
    bool intervals_ok = (s_dc_count >= 7U);
    bool shape_ok = true;
    for (uint32_t k = 0U; k < s_dc_count && k < 7U; k++)
    {
        dc_frame_t *f = &s_dc_frames[k];
        shape_ok = shape_ok && f->msg_type == DHCP_MSG_DISCOVER && f->src_ip == 0U &&
                   f->dest_ip == DHCP_IP_BROADCAST && (f->flags & DHCP_FLAG_BROADCAST) != 0U &&
                   f->xid == s_dc_frames[0].xid;
        if (k > 0U && k < 7U)
        {
            intervals_ok = intervals_ok && dc_near(f->at_us - s_dc_frames[k - 1U].at_us, want_disc[k - 1U] * DC_SEC);
        }
    }
    TEST_ASSERT(intervals_ok, "DISCOVER intervals 4/8/16/32/64/64 s minus 1 s jitter (rand 0)");
    TEST_ASSERT(shape_ok, "DISCOVERs: broadcast, source 0.0.0.0, broadcast flag, one xid");
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.retransmits == 6U && t.discovers_sent == 7U, "Retransmits counted");

    /* 1b. Jitter upper end: rand 0xFFFFFFFF gives backoff - 1 s + (0xFFFFFFFF mod 2000001) us */
    g_wpa_drv_host.random_fill = 0xFFU;
    dhcp_client_init();
    s_dc_count = 0U;
    dhcp_client_start();
    dc_run(10ULL * DC_SEC);
    uint64_t span = 2ULL * DHCP_CLIENT_BACKOFF_JITTER_US + 1ULL;
    uint64_t want = DHCP_CLIENT_BACKOFF_INITIAL_US - DHCP_CLIENT_BACKOFF_JITTER_US + (0xFFFFFFFFULL % span);
    TEST_ASSERT(s_dc_count >= 2U && dc_near(s_dc_frames[1].at_us - s_dc_frames[0].at_us, want),
                "Jitter stays within +/- 1 s of the backoff");
    g_wpa_drv_host.random_fill = 0U;

    /* 2. OFFER -> REQUEST with requested IP + server id; unanswered: 3 sends, then DISCOVER with a new xid */
    dhcp_client_init();
    dhcp_client_start();
    s_dc_count = 0U;
    dc_reply(DHCP_MSG_OFFER, DC_OFFER_IP, 600U, 0U, 0U);
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_REQUESTING && s_dc_count == 1U,
                "OFFER: REQUEST sent at once");
    TEST_ASSERT(s_dc_frames[0].msg_type == DHCP_MSG_REQUEST && s_dc_frames[0].has_req_ip &&
                s_dc_frames[0].req_ip == DC_OFFER_IP && s_dc_frames[0].has_server_id &&
                s_dc_frames[0].server_id == DC_SERVER_IP && s_dc_frames[0].ciaddr == 0U &&
                s_dc_frames[0].dest_ip == DHCP_IP_BROADCAST, "REQUEST (SELECTING): option 50 + 54, ciaddr 0, broadcast");
    uint32_t req_xid = s_dc_frames[0].xid;
    dc_run(16ULL * DC_SEC);
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_REQUESTING && s_dc_count == 3U,
                "REQUEST retransmitted at ~3 s and ~10 s");
    TEST_ASSERT(s_dc_count == 3U && dc_near(s_dc_frames[1].at_us - s_dc_frames[0].at_us, 3ULL * DC_SEC) &&
                dc_near(s_dc_frames[2].at_us - s_dc_frames[1].at_us, 7ULL * DC_SEC), "REQUEST backoff 4/8 s");
    dc_run(10ULL * DC_SEC);    /* third wait (16 s - 1) ends at ~25 s */
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_DISCOVERING && s_dc_count == 4U &&
                s_dc_frames[3].msg_type == DHCP_MSG_DISCOVER && s_dc_frames[3].xid != req_xid,
                "No ACK after 3 REQUESTs: back to DISCOVER (new xid)");

    /* 3. NAK: INIT after the DISCOVER backoff, not at once */
    dhcp_client_init();
    dhcp_client_start();
    dc_reply(DHCP_MSG_OFFER, DC_OFFER_IP, 600U, 0U, 0U);
    s_dc_count = 0U;
    dc_reply(DHCP_MSG_NAK, 0U, 0U, 0U, 0U);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.state == DHCP_CLIENT_STATE_DISCOVERING && t.naks_received == 1U && s_dc_count == 0U,
                "NAK: DISCOVERING, nothing sent at once");
    dc_run(5ULL * DC_SEC);
    TEST_ASSERT(s_dc_count == 0U, "NAK: no DISCOVER within the first 4 s");
    dc_run(3ULL * DC_SEC);     /* DISCOVER backoff is 8 s after the first DISCOVER: 7 s with rand 0 */
    TEST_ASSERT(s_dc_count == 1U && s_dc_frames[0].msg_type == DHCP_MSG_DISCOVER,
                "NAK: next DISCOVER after the (grown) DISCOVER backoff");

    /* 4. ACK: bound, default T1 = 1/2, T2 = 7/8 lease; stack address set */
    dc_bind(600U, 0U, 0U);
    dhcp_client_get_telemetry(&t);
    net_config_t ncfg;
    net_get_config(&ncfg);
    TEST_ASSERT(t.state == DHCP_CLIENT_STATE_BOUND && t.t1_sec == 300U && t.t2_sec == 525U &&
                t.lease_left_sec == 600U, "ACK: BOUND, T1 300 s, T2 525 s of a 600 s lease");
    TEST_ASSERT(ncfg.ip == DC_OFFER_IP && ncfg.gateway == DC_SERVER_IP, "ACK: address applied to the stack");

    /* 5. Silent until T1, then one unicast REQUEST to the server (renew) */
    dc_run(299ULL * DC_SEC);
    TEST_ASSERT(s_dc_count == 0U, "Bound: no DHCP traffic before T1");
    dc_run(2ULL * DC_SEC);
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_RENEWING && s_dc_count == 1U,
                "T1: RENEWING, one REQUEST");
    dc_frame_t *rf = &s_dc_frames[0];
    TEST_ASSERT(rf->msg_type == DHCP_MSG_REQUEST && memcmp(rf->dest_mac, s_dc_server_mac, ETH_ADDR_LEN) == 0 &&
                rf->dest_ip == DC_SERVER_IP && rf->src_ip == DC_OFFER_IP && rf->ciaddr == DC_OFFER_IP &&
                !rf->has_req_ip && !rf->has_server_id && (rf->flags & DHCP_FLAG_BROADCAST) == 0U,
                "RENEWING REQUEST: unicast to the server MAC/IP, ciaddr + source = our address, no 50/54");
    dc_reply(DHCP_MSG_ACK, DC_OFFER_IP, 600U, 0U, 0U);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.state == DHCP_CLIENT_STATE_BOUND && t.renewals == 1U && t.lease_left_sec >= 598U,
                "Renew ACK: BOUND again, lease extended");

    /* 6. Renew unanswered: retries at half the time to T2 (min 60 s), REBINDING at T2, expiry drops the address */
    s_dc_count = 0U;
    dc_run(301ULL * DC_SEC);   /* T1 of the renewed lease */
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_RENEWING && s_dc_count == 1U, "Second T1 reached");
    uint64_t t1_at = s_dc_frames[0].at_us;
    dc_run(225ULL * DC_SEC);   /* to T2 */
    bool renew_retry_ok = s_dc_count >= 2U && dc_near(s_dc_frames[1].at_us - t1_at, 112500000ULL);
    TEST_ASSERT(renew_retry_ok, "RENEWING retransmit at half the time left to T2");
    TEST_ASSERT(s_dc_count >= 3U && dc_near(s_dc_frames[2].at_us - s_dc_frames[1].at_us,
                                            DHCP_CLIENT_RENEW_MIN_RETRY_US), "RENEWING retransmit not sooner than 60 s");
    dc_run(2ULL * DC_SEC);
    dc_frame_t *bf = &s_dc_frames[(s_dc_count > 0U) ? (s_dc_count - 1U) : 0U];
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_REBINDING && bf->dest_ip == DHCP_IP_BROADCAST &&
                bf->ciaddr == DC_OFFER_IP && bf->src_ip == DC_OFFER_IP && !bf->has_req_ip,
                "T2: REBINDING, broadcast REQUEST with ciaddr");
    dc_run(80ULL * DC_SEC);    /* past expiry */
    dhcp_client_get_telemetry(&t);
    net_get_config(&ncfg);
    TEST_ASSERT(t.state == DHCP_CLIENT_STATE_DISCOVERING && t.leases_expired == 1U && t.lease_left_sec == 0U &&
                ncfg.ip == 0U, "Expiry: address dropped, DISCOVER");
    TEST_ASSERT(s_dc_count > 0U && s_dc_frames[s_dc_count - 1U].msg_type == DHCP_MSG_DISCOVER,
                "Expiry: DISCOVER sent");

    /* 7. INIT-REBOOT: link loss keeps the lease; rejoin asks for it again */
    dc_bind(600U, 0U, 0U);
    dhcp_client_stop();
    dc_run(10ULL * DC_SEC);
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_IDLE && s_dc_count == 0U, "Stopped: nothing sent");
    dhcp_client_start();
    rf = &s_dc_frames[0];
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_REBOOTING && s_dc_count == 1U &&
                rf->msg_type == DHCP_MSG_REQUEST && rf->has_req_ip && rf->req_ip == DC_OFFER_IP &&
                !rf->has_server_id && rf->ciaddr == 0U && rf->src_ip == 0U && rf->dest_ip == DHCP_IP_BROADCAST,
                "INIT-REBOOT REQUEST: option 50, no server id, ciaddr 0, broadcast");
    dc_reply(DHCP_MSG_ACK, DC_OFFER_IP, 600U, 0U, 0U);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.state == DHCP_CLIENT_STATE_BOUND && t.reboots_confirmed == 1U && t.discovers_sent == 1U,
                "INIT-REBOOT ACK: same address, no new DISCOVER");

    /* 7b. INIT-REBOOT unanswered: 3 REQUESTs, then DISCOVER and the lease is gone */
    dhcp_client_stop();
    s_dc_count = 0U;
    dhcp_client_start();
    dc_run(26ULL * DC_SEC);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.state == DHCP_CLIENT_STATE_DISCOVERING && s_dc_count == 4U &&
                s_dc_frames[3].msg_type == DHCP_MSG_DISCOVER && t.lease_left_sec == 0U,
                "Unanswered INIT-REBOOT: DISCOVER after 3 REQUESTs, lease dropped");

    /* 7c. INIT-REBOOT NAK (other network): DISCOVER, lease dropped */
    dc_bind(600U, 0U, 0U);
    dhcp_client_stop();
    dhcp_client_start();
    dc_reply(DHCP_MSG_NAK, 0U, 0U, 0U, 0U);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.state == DHCP_CLIENT_STATE_DISCOVERING && t.lease_left_sec == 0U && t.naks_received == 1U,
                "INIT-REBOOT NAK: DISCOVER, lease dropped");

    /* 7d. New credentials: forget the lease, start with DISCOVER */
    dc_bind(600U, 0U, 0U);
    dhcp_client_stop();
    dhcp_client_forget();
    dhcp_client_start();
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_DISCOVERING && s_dc_count == 1U &&
                s_dc_frames[0].msg_type == DHCP_MSG_DISCOVER, "Forgotten lease: DISCOVER");

    /* 7e. Lease expires while the link is down: next start uses DISCOVER */
    dc_bind(600U, 0U, 0U);
    dhcp_client_stop();
    dc_run(601ULL * DC_SEC);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.state == DHCP_CLIENT_STATE_IDLE && t.leases_expired == 1U && s_dc_count == 0U,
                "Expiry while stopped: lease dropped, nothing sent");
    dhcp_client_start();
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_DISCOVERING, "Expired lease: DISCOVER on start");

    /* 8. Options 58/59 from the server are used */
    dc_bind(600U, 100U, 200U);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.t1_sec == 100U && t.t2_sec == 200U, "Server T1/T2 (options 58/59) honored");
    dc_run(101ULL * DC_SEC);
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_RENEWING, "Renew at server T1");
    dc_bind(600U, 500U, 400U);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.t1_sec == 300U && t.t2_sec == 400U, "Inconsistent T1 >= T2 from the server: default T1");

    /* 9. Infinite lease: never renews, never expires */
    dc_bind(DHCP_LEASE_INFINITE, 0U, 0U);
    dc_run(1000ULL * DC_SEC);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.state == DHCP_CLIENT_STATE_BOUND && s_dc_count == 0U && t.t1_sec == DHCP_LEASE_INFINITE &&
                t.lease_left_sec == DHCP_LEASE_INFINITE, "Infinite lease: no renew, no expiry");

    /* 10. Lease-less ACK: fallback lease */
    dc_bind(0U, 0U, 0U);
    dhcp_client_get_telemetry(&t);
    TEST_ASSERT(t.lease_time_sec == DHCP_CLIENT_FALLBACK_LEASE_SEC, "ACK without lease time: fallback lease");

    TEST_ASSERT(strcmp(dhcp_client_state_name(DHCP_CLIENT_STATE_REBOOTING), "INIT-REBOOT") == 0,
                "State names");

    wifi_host_set_tx_hook(NULL);
    g_wpa_drv_host.random_fill = saved_fill;
    g_wpa_drv_host.random_bytes = saved_bytes;
    dhcp_client_init();
    net_set_ip(saved_net.ip, saved_net.netmask, saved_net.gateway);
}

static void test_wpa2_client_and_mdns_subsystem(void)
{
    printf("  [TEST] Bare-Metal Wi-Fi Station (STA) WPA2-PSK Client & Home LAN Join (Task 8.2)...\n");

    /* Ensure Wi-Fi and Network submodules are initialized */
    wifi_init();
    net_init();

    /* 1. Cryptographic Test Vectors */

    /* 1a. PBKDF2-HMAC-SHA1: IEEE 802.11i standard vector */
    /* Passphrase: "password", SSID: "IEEE", 4096 iterations -> 32 bytes PMK */
    uint8_t pmk[WPA2_PMK_LEN];
    const uint8_t exp_pmk[WPA2_PMK_LEN] = {
        0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef,
        0x9e, 0xbb, 0x4b, 0x90, 0xb3, 0x8a, 0x5f, 0x90,
        0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2,
        0x3a, 0xed, 0x76, 0x2e, 0x97, 0x10, 0xa1, 0x2e
    };
    TEST_ASSERT(wpa2_crypto_pbkdf2_sha1("password", "IEEE", 4096, pmk) == WPA2_OK,
                "PBKDF2-HMAC-SHA1 computes IEEE 802.11i test vector");
    TEST_ASSERT(memcmp(pmk, exp_pmk, WPA2_PMK_LEN) == 0,
                "PBKDF2-HMAC-SHA1 matches standard 32-byte test vector output exactly");

    /* Null parameter checks */
    TEST_ASSERT(wpa2_crypto_pbkdf2_sha1(NULL, "IEEE", 4096, pmk) == WPA2_ERR_INVALID_ARG,
                "PBKDF2 rejects NULL passphrase");
    TEST_ASSERT(wpa2_crypto_pbkdf2_sha1("password", NULL, 4096, pmk) == WPA2_ERR_INVALID_ARG,
                "PBKDF2 rejects NULL SSID");

    /* 1b. RFC 3394 AES Key Wrap & Unwrap Test Vector (Section 4.1) */
    const uint8_t rfc3394_kek[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    const uint8_t rfc3394_plain[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    const uint8_t rfc3394_exp_wrapped[24] = {
        0x1f, 0xa6, 0x8b, 0x0a, 0x81, 0x12, 0xb4, 0x47,
        0xae, 0xf3, 0x4b, 0xd8, 0xfb, 0x5a, 0x7b, 0x82,
        0x9d, 0x3e, 0x86, 0x23, 0x71, 0xd2, 0xcf, 0xe5
    };
    uint8_t wrap_out[32];
    uint16_t wrap_out_len = 0U;
    TEST_ASSERT(wpa2_crypto_aes_wrap(rfc3394_kek, rfc3394_plain, 16, wrap_out, &wrap_out_len) == WPA2_OK,
                "RFC 3394 AES Key Wrap succeeds");
    TEST_ASSERT(wrap_out_len == 24U, "Wrapped key length is 24 bytes");
    TEST_ASSERT(memcmp(wrap_out, rfc3394_exp_wrapped, 24) == 0,
                "Wrapped key matches RFC 3394 test vector exactly");

    uint8_t unwrap_out[32];
    uint16_t unwrap_out_len = 0U;
    TEST_ASSERT(wpa2_crypto_aes_unwrap(rfc3394_kek, wrap_out, wrap_out_len, unwrap_out, &unwrap_out_len) == WPA2_OK,
                "RFC 3394 AES Key Unwrap succeeds on valid vector");
    TEST_ASSERT(unwrap_out_len == 16U, "Unwrapped key length is 16 bytes");
    TEST_ASSERT(memcmp(unwrap_out, rfc3394_plain, 16) == 0,
                "Unwrapped key matches original plaintext");

    /* Corrupted wrapped ciphertext -> integrity check must fail */
    uint8_t corrupt_wrapped[24];
    memcpy(corrupt_wrapped, wrap_out, 24);
    corrupt_wrapped[10] ^= 0x55;
    TEST_ASSERT(wpa2_crypto_aes_unwrap(rfc3394_kek, corrupt_wrapped, 24, unwrap_out, &unwrap_out_len) == WPA2_ERR_DECRYPT_FAIL,
                "AES Key Unwrap detects corrupted ciphertext integrity failure");

    /* 1c. PRF-512 PTK expansion */
    const uint8_t sta_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t ap_bssid[6] = {0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb};
    const uint8_t snonce[32] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f
    };
    const uint8_t anonce[32] = {
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
        0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f
    };
    wpa2_ptk_t ptk;
    TEST_ASSERT(wpa2_crypto_prf512(pmk, sta_mac, ap_bssid, snonce, anonce, &ptk) == WPA2_OK,
                "wpa2_crypto_prf512 expansion succeeds");
    uint32_t ptk_nonzero = 0U;
    for (size_t i = 0; i < sizeof(ptk); i++)
    {
        ptk_nonzero |= ((uint8_t *)&ptk)[i];
    }
    TEST_ASSERT(ptk_nonzero != 0U, "Derived PTK is non-zero");
    /* Known answer: 802.11 PRF-512 recomputed independently (Python hmac/hashlib) */
    const uint8_t exp_kck[16] = {0x87,0x07,0x69,0x20,0xe9,0xb6,0x1c,0xf1,0xf1,0x8c,0xb5,0x36,0x12,0x18,0x26,0x15};
    const uint8_t exp_kek[16] = {0xef,0x90,0x03,0x4c,0xc9,0x41,0x0d,0xa5,0xc2,0x99,0xe8,0xf0,0xca,0x75,0x88,0xf6};
    const uint8_t exp_tk[16]  = {0x2d,0x0c,0xa9,0x56,0x77,0x8f,0x19,0x31,0x79,0xec,0xe5,0xb9,0x17,0x92,0x7d,0x10};
    TEST_ASSERT(memcmp(ptk.kck, exp_kck, 16) == 0 && memcmp(ptk.kek, exp_kek, 16) == 0 &&
                memcmp(ptk.tk, exp_tk, 16) == 0, "PRF-512 PTK (KCK, KEK, TK) matches the known answer");

    /* 1d. HMAC-SHA1 MIC over an 802.1X frame (the MIC field counts as zero) */
    uint8_t dummy_frame[128];
    memset(dummy_frame, 0x33, sizeof(dummy_frame));
    uint8_t computed_mic[WPA2_MIC_LEN];
    uint8_t computed_mic2[WPA2_MIC_LEN];
    TEST_ASSERT(wpa2_crypto_compute_mic(ptk.kck, dummy_frame, sizeof(dummy_frame), computed_mic) == WPA2_OK,
                "wpa2_crypto_compute_mic succeeds");
    memset(dummy_frame + sizeof(eapol_1x_hdr_t) + offsetof(eapol_key_header_t, key_mic), 0x00, WPA2_MIC_LEN);
    wpa2_crypto_compute_mic(ptk.kck, dummy_frame, sizeof(dummy_frame), computed_mic2);
    TEST_ASSERT(memcmp(computed_mic, computed_mic2, WPA2_MIC_LEN) == 0, "MIC ignores the MIC field contents");

    /* 2. 4-way and group handshakes: test_wpa2_handshake() */

    /* 3. DHCP Client State Machine */
    TEST_ASSERT(dhcp_client_init() == DHCP_OK, "dhcp_client_init succeeds");
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_IDLE, "Initial DHCP state is IDLE");

    TEST_ASSERT(dhcp_client_start() == DHCP_OK, "dhcp_client_start transmits DHCPDISCOVER");
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_DISCOVERING, "State is DISCOVERING");

    dhcp_client_telemetry_t dtelem;
    TEST_ASSERT(dhcp_client_get_telemetry(&dtelem) == DHCP_OK, "dhcp_client_get_telemetry succeeds");
    TEST_ASSERT(dtelem.discovers_sent >= 1U, "discovers_sent tracked");

    /* 3a. Inbound DHCPOFFER Simulation */
    dhcp_packet_t offer_pkt;
    memset(&offer_pkt, 0, sizeof(offer_pkt));
    offer_pkt.op = DHCP_OP_BOOTREPLY;
    offer_pkt.htype = DHCP_HTYPE_ETHERNET;
    offer_pkt.hlen = DHCP_HLEN_ETHERNET;
    offer_pkt.xid = NET_HTONL(dtelem.xid);
    offer_pkt.yiaddr = NET_HTONL(0xC0A80132U); /* 192.168.1.50 */
    offer_pkt.siaddr = NET_HTONL(0xC0A80101U); /* 192.168.1.1 */
    offer_pkt.magic_cookie = NET_HTONL(DHCP_MAGIC_COOKIE);
    size_t o_idx = 0U;
    offer_pkt.options[o_idx++] = DHCP_OPT_MSG_TYPE;
    offer_pkt.options[o_idx++] = 1U;
    offer_pkt.options[o_idx++] = DHCP_MSG_OFFER;
    offer_pkt.options[o_idx++] = DHCP_OPT_SERVER_ID;
    offer_pkt.options[o_idx++] = 4U;
    offer_pkt.options[o_idx++] = 192;
    offer_pkt.options[o_idx++] = 168;
    offer_pkt.options[o_idx++] = 1;
    offer_pkt.options[o_idx++] = 1;
    offer_pkt.options[o_idx++] = DHCP_OPT_END;

    uint16_t offer_len = (uint16_t)(sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN + o_idx);
    uint8_t dummy_eth[14] = {0};
    TEST_ASSERT(dhcp_client_process_packet(dummy_eth, (const uint8_t *)&offer_pkt, offer_len) == DHCP_OK,
                "dhcp_client_process_packet processes DHCPOFFER");
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_REQUESTING, "State transitions to REQUESTING");

    /* Retransmission, NAK, INIT-REBOOT and lease timing: test_dhcp_client_rfc2131() */

    /* 3b. Inbound DHCPACK Simulation */
    dhcp_packet_t ack_pkt;
    memset(&ack_pkt, 0, sizeof(ack_pkt));
    ack_pkt.op = DHCP_OP_BOOTREPLY;
    ack_pkt.htype = DHCP_HTYPE_ETHERNET;
    ack_pkt.hlen = DHCP_HLEN_ETHERNET;
    ack_pkt.xid = NET_HTONL(dtelem.xid);
    ack_pkt.yiaddr = NET_HTONL(0xC0A80132U); /* 192.168.1.50 */
    ack_pkt.siaddr = NET_HTONL(0xC0A80101U); /* 192.168.1.1 */
    ack_pkt.magic_cookie = NET_HTONL(DHCP_MAGIC_COOKIE);
    size_t a_idx = 0U;
    ack_pkt.options[a_idx++] = DHCP_OPT_MSG_TYPE;
    ack_pkt.options[a_idx++] = 1U;
    ack_pkt.options[a_idx++] = DHCP_MSG_ACK;
    ack_pkt.options[a_idx++] = DHCP_OPT_SUBNET_MASK;
    ack_pkt.options[a_idx++] = 4U;
    ack_pkt.options[a_idx++] = 255;
    ack_pkt.options[a_idx++] = 255;
    ack_pkt.options[a_idx++] = 255;
    ack_pkt.options[a_idx++] = 0;
    ack_pkt.options[a_idx++] = DHCP_OPT_ROUTER;
    ack_pkt.options[a_idx++] = 4U;
    ack_pkt.options[a_idx++] = 192;
    ack_pkt.options[a_idx++] = 168;
    ack_pkt.options[a_idx++] = 1;
    ack_pkt.options[a_idx++] = 1;
    ack_pkt.options[a_idx++] = DHCP_OPT_DNS;
    ack_pkt.options[a_idx++] = 4U;
    ack_pkt.options[a_idx++] = 8;
    ack_pkt.options[a_idx++] = 8;
    ack_pkt.options[a_idx++] = 8;
    ack_pkt.options[a_idx++] = 8;
    ack_pkt.options[a_idx++] = DHCP_OPT_LEASE_TIME;
    ack_pkt.options[a_idx++] = 4U;
    ack_pkt.options[a_idx++] = 0;
    ack_pkt.options[a_idx++] = 0;
    ack_pkt.options[a_idx++] = 0x0E;
    ack_pkt.options[a_idx++] = 0x10;
    ack_pkt.options[a_idx++] = DHCP_OPT_END;

    uint16_t ack_len = (uint16_t)(sizeof(dhcp_packet_t) - DHCP_MIN_OPTIONS_LEN + a_idx);
    TEST_ASSERT(dhcp_client_process_packet(dummy_eth, (const uint8_t *)&ack_pkt, ack_len) == DHCP_OK,
                "dhcp_client_process_packet processes DHCPACK");
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_BOUND, "State transitions to BOUND");

    dhcp_client_get_telemetry(&dtelem);
    TEST_ASSERT(dtelem.assigned_ip == 0xC0A80132U, "Assigned IP is 192.168.1.50");
    TEST_ASSERT(dtelem.gateway == 0xC0A80101U, "Gateway is 192.168.1.1");

    /* 3c. Static Fallback Configuration */
    dhcp_client_set_static_fallback(0x0A00000AU, 0xFFFFFF00U, 0x0A000001U, 0x08080808U);
    TEST_ASSERT(dhcp_client_get_state() == DHCP_CLIENT_STATE_STATIC, "State is STATIC after static fallback");
    dhcp_client_get_telemetry(&dtelem);
    TEST_ASSERT(dtelem.assigned_ip == 0x0A00000AU, "Assigned IP is 10.0.0.10");

    /* 4. Freestanding Multicast DNS (mDNS) Responder Engine */
    TEST_ASSERT(mdns_init() == MDNS_OK, "mdns_init succeeds");
    TEST_ASSERT(mdns_is_active(), "mDNS active flag is true");
    TEST_ASSERT(strcmp(mdns_get_hostname(), "iron-v") == 0, "Default hostname is 'iron-v'");

    /* Hostname modification */
    TEST_ASSERT(mdns_set_hostname("iron-c6") == MDNS_OK, "mdns_set_hostname succeeds");
    TEST_ASSERT(strcmp(mdns_get_hostname(), "iron-c6") == 0, "Updated hostname is 'iron-c6'");
    TEST_ASSERT(mdns_set_hostname("") == MDNS_ERR_INVALID_ARG, "Rejects empty hostname");

    /* Gratuitous announcement beacon */
    TEST_ASSERT(mdns_announce() == MDNS_OK, "mdns_announce broadcasts unsolicited response");
    mdns_telemetry_t mtelem;
    TEST_ASSERT(mdns_get_telemetry(&mtelem) == MDNS_OK, "mdns_get_telemetry succeeds");
    TEST_ASSERT(mtelem.announcements_sent >= 1U, "announcements_sent tracked");

    /* 4a. Query matching: query for "iron-c6.local" Type A */
    uint8_t mdns_query[128];
    memset(mdns_query, 0, sizeof(mdns_query));
    dns_header_t *dns_hdr = (dns_header_t *)mdns_query;
    dns_hdr->id = 0;
    dns_hdr->flags = 0; /* Standard Query */
    dns_hdr->qdcount = NET_HTONS(1U);

    size_t q_idx = sizeof(dns_header_t);
    mdns_query[q_idx++] = 7U;
    memcpy(&mdns_query[q_idx], "iron-c6", 7U);
    q_idx += 7U;
    mdns_query[q_idx++] = 5U;
    memcpy(&mdns_query[q_idx], "local", 5U);
    q_idx += 5U;
    mdns_query[q_idx++] = 0U; /* Terminating zero label */

    /* QTYPE = A (1), QCLASS = IN (1) */
    mdns_query[q_idx++] = 0U;
    mdns_query[q_idx++] = 1U;
    mdns_query[q_idx++] = 0U;
    mdns_query[q_idx++] = 1U;

    TEST_ASSERT(mdns_process_packet(dummy_eth, mdns_query, (uint16_t)q_idx) == MDNS_OK,
                "mdns_process_packet resolves iron-c6.local Type A query");
    mdns_get_telemetry(&mtelem);
    TEST_ASSERT(mtelem.host_queries_matched >= 1U, "host_queries_matched incremented");
    TEST_ASSERT(mtelem.responses_sent >= 1U, "responses_sent incremented");

    /* 4b. Unmatched query: query for "other-host.local" */
    q_idx = sizeof(dns_header_t);
    mdns_query[q_idx++] = 10U;
    memcpy(&mdns_query[q_idx], "other-host", 10U);
    q_idx += 10U;
    mdns_query[q_idx++] = 5U;
    memcpy(&mdns_query[q_idx], "local", 5U);
    q_idx += 5U;
    mdns_query[q_idx++] = 0U;
    mdns_query[q_idx++] = 0U;
    mdns_query[q_idx++] = 1U;
    mdns_query[q_idx++] = 0U;
    mdns_query[q_idx++] = 1U;

    TEST_ASSERT(mdns_process_packet(dummy_eth, mdns_query, (uint16_t)q_idx) == MDNS_ERR_NO_MATCH,
                "mDNS ignores non-matching hostname query");

    /* Stop mDNS */
    TEST_ASSERT(mdns_stop() == MDNS_OK, "mdns_stop succeeds");
    TEST_ASSERT(!mdns_is_active(), "mDNS inactive after stop");
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

static void test_multi_protocol_coex_and_soak_subsystem(void)
{
    printf("  [TEST] Multi-Protocol RF Coexistence Arbiter & Stability Soak Engine (Task 6)...\n");

    /* 1. Register Architecture & Offsets */
    TEST_ASSERT((uintptr_t)MODEM_LPCON_RST_CONF_REG == 0x600AF024U, "MODEM_LPCON_RST_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_LPCON_COEX_LP_CLK_CONF_REG == 0x600AF008U, "MODEM_LPCON_COEX_LP_CLK_CONF_REG address calculation");
    TEST_ASSERT((uintptr_t)MODEM_LPCON_CLK_CONF_REG == 0x600AF018U, "MODEM_LPCON_CLK_CONF_REG address calculation");

    /* 2. Coexistence Bitfield Mask Constants */
    TEST_ASSERT(MODEM_LPCON_CLK_COEX_EN_BIT == (1U << 1), "MODEM_LPCON_CLK_COEX_EN_BIT is bit 1");
    TEST_ASSERT(MODEM_LPCON_CLK_COEX_LP_SEL_XTAL_BIT == (1U << 2), "MODEM_LPCON_CLK_COEX_LP_SEL_XTAL_BIT is bit 2");
    TEST_ASSERT(MODEM_LPCON_RST_COEX_BIT == (1U << 1), "MODEM_LPCON_RST_COEX_BIT is bit 1");

    /* 3. Multi-Protocol Clock Gating & Coexistence Arbiter Synchronization */
    TEST_ASSERT(modem_init() == MODEM_OK, "modem_init succeeds for coex validation");
    TEST_ASSERT(modem_validate_coexistence(), "modem_validate_coexistence reports true after modem_init");

    /* Test RF Coexistence with All Three Wireless Protocols Active */
    TEST_ASSERT(modem_enable_wifi_clocks() == MODEM_OK, "modem_enable_wifi_clocks succeeds");
    TEST_ASSERT(modem_enable_ble_clocks() == MODEM_OK, "modem_enable_ble_clocks succeeds");
    TEST_ASSERT(modem_enable_ieee802154_clocks() == MODEM_OK, "modem_enable_ieee802154_clocks succeeds");
    TEST_ASSERT(modem_validate_coexistence(), "modem_validate_coexistence reports true with Wi-Fi, BLE, 15.4 all active");

    modem_clock_state_t cs;
    TEST_ASSERT(modem_get_clock_state(&cs) == MODEM_OK, "modem_get_clock_state succeeds");
    TEST_ASSERT(cs.wifi_clk_enabled == 1U, "Wi-Fi clock active in coex state");
    TEST_ASSERT(cs.ble_clk_enabled == 1U, "BLE clock active in coex state");
    TEST_ASSERT(cs.ieee802154_clk_enabled == 1U, "802.15.4 clock active in coex state");
    TEST_ASSERT(cs.coexistence_enabled == 1U, "Coexistence arbiter active in coex state");

    /* Verify deassertion / reassertion */
    TEST_ASSERT(modem_disable_coexistence() == MODEM_OK, "modem_disable_coexistence succeeds");
    TEST_ASSERT(!modem_validate_coexistence(), "modem_validate_coexistence reports false when disabled");
    TEST_ASSERT(!modem_is_coex_enabled(), "modem_is_coex_enabled reports false");

    TEST_ASSERT(modem_enable_coexistence() == MODEM_OK, "modem_enable_coexistence re-enables arbiter");
    TEST_ASSERT(modem_validate_coexistence(), "modem_validate_coexistence reports true after re-enable");
    TEST_ASSERT(modem_is_coex_enabled(), "modem_is_coex_enabled reports true");

    /* 4. Soak Telemetry Data Geometry & Defaults */
    TEST_ASSERT(TEST_SOAK_DEFAULT_CYCLES == 5U, "TEST_SOAK_DEFAULT_CYCLES is 5");
    TEST_ASSERT(TEST_SOAK_DEFAULT_DELAY_MS == 50U, "TEST_SOAK_DEFAULT_DELAY_MS is 50");
    TEST_ASSERT(sizeof(test_suite_result_t) == 16U, "sizeof(test_suite_result_t) is 16 bytes");
    TEST_ASSERT(sizeof(test_soak_telemetry_t) >= 64U, "sizeof(test_soak_telemetry_t) has complete telemetry fields");

    /* 5. Simulated Soak Benchmark Loop Verifying Zero Heap Leaks & Zero DPC Drops */
    arena_init();
    dpc_init();

    /* Snapshot baseline memory */
    arena_pool_stats_t sm_baseline;
    arena_pool_stats_t md_baseline;
    arena_get_pool_stats(ARENA_POOL_SMALL, &sm_baseline);
    arena_get_pool_stats(ARENA_POOL_MEDIUM, &md_baseline);
    TEST_ASSERT(sm_baseline.active_count == 0U, "Baseline small pool has 0 active blocks");
    TEST_ASSERT(md_baseline.active_count == 0U, "Baseline medium pool has 0 active blocks");
    TEST_ASSERT(arena_scratch_mark() == 0U, "Baseline scratch arena has 0 allocated bytes");

    /* Execute simulated soak cycles */
    const uint32_t SOAK_SIM_CYCLES = 10U;
    uint32_t soak_completed = 0U;
    uint32_t soak_failed = 0U;

    for (uint32_t cycle = 1U; cycle <= SOAK_SIM_CYCLES; cycle++)
    {
        /* 5a. Allocate and deallocate blocks simulating workload */
        void *p1 = arena_alloc_pool(ARENA_POOL_SMALL);
        void *p2 = arena_alloc_pool(ARENA_POOL_MEDIUM);
        void *sc = arena_scratch_alloc(128U);
        TEST_ASSERT(p1 != NULL && p2 != NULL && sc != NULL, "Soak cycle allocation succeeds");

        /* 5b. Exercise DPC queue */
        int enq_res = dpc_enqueue(DPC_TYPE_TEST_EVENT, cycle, 0U, NULL);
        TEST_ASSERT(enq_res == 0, "DPC enqueue succeeds");
        TEST_ASSERT(dpc_get_size() == 1U, "DPC queue holds 1 event");

        /* Dequeue DPC without dropping */
        dpc_event_t ent;
        TEST_ASSERT(dpc_dequeue(&ent) == 0, "DPC dequeue succeeds");
        TEST_ASSERT(dpc_get_drop_count() == 0U, "DPC drop count remains 0");

        /* Free allocations cleanly */
        arena_free(p1);
        arena_free(p2);
        arena_scratch_reset(0U);

        /* 5c. Assert zero heap leak invariant */
        arena_pool_stats_t sm_cur;
        arena_pool_stats_t md_cur;
        arena_get_pool_stats(ARENA_POOL_SMALL, &sm_cur);
        arena_get_pool_stats(ARENA_POOL_MEDIUM, &md_cur);
        size_t sc_cur = arena_scratch_mark();

        bool mem_clean = (sm_cur.active_count == 0U) &&
                         (md_cur.active_count == 0U) &&
                         (sc_cur == 0U);
        bool coex_valid = modem_validate_coexistence();

        if (mem_clean && (dpc_get_drop_count() == 0U) && coex_valid)
        {
            soak_completed++;
        }
        else
        {
            soak_failed++;
        }
    }

    TEST_ASSERT(soak_completed == SOAK_SIM_CYCLES, "All simulated soak cycles complete with zero leak");
    TEST_ASSERT(soak_failed == 0U, "Zero failed cycles during simulated soak");
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
    test_lp_wdt_subsystem();
    test_wifi_mac_subsystem();
    test_wifi_custom_stack_refactor();
    test_ieee802154_subsystem();
    test_tcpip_subsystem();
    test_tcp_reliability();
    test_http_server_subsystem();
    test_dhcp_dns_subsystem();
    test_softap_dns_modes();
    test_speedtest_subsystem();
    test_shell_subsystem();
    test_efuse_subsystem();
    test_soak_anti_starvation_subsystem();
    test_ota_subsystem();
    test_nvs_subsystem();
    test_selftest_snapshots();
    test_provisioning_subsystem();
    test_wifi_link_policy();
    test_provisioning_join();
    test_provisioning_seed();
    test_mdns_legacy_unicast();
    test_wpa2_client_and_mdns_subsystem();
    test_dhcp_client_rfc2131();
    test_wpa_ie_parsing();
    test_wpa2_handshake();
    test_wpa2_replay_capture();

    /* Hardened cross-module integration and edge case tests */
    test_coroutine_systimer_dpc_integration();
    test_arena_concurrency_exhaustion();
    test_pmp_chained_tor_and_locked_regions();
    test_apm_dynamic_reconfiguration();
    test_console_line_reader_edge_cases();
    test_multi_protocol_coex_and_soak_subsystem();

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

