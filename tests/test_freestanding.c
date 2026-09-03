/*
 * tests/test_freestanding.c
 *
 * Authentic host-native unit test harness for Iron V freestanding runtime library.
 * Compiles natively with host GCC: gcc -O2 -Wall -Wextra -Isrc tests/test_freestanding.c src/string.c -o tests/test_freestanding
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include "string.h"

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
