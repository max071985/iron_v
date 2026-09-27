#include "string.h"

int strcmp(const char *str1, const char *str2)
{
    while (*str1 && (*str1 == *str2))
    {
        str1++;
        str2++;
    }
    return *(const unsigned char *)str1 - *(const unsigned char *)str2;
}

int strncmp(const char *str1, const char *str2, size_t n)
{
    while (n && *str1 && (*str1 == *str2))
    {
        str1++;
        str2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)str1 - *(const unsigned char *)str2;
}

size_t strlen(const char *str)
{
    size_t len = 0;
    while (str[len])
    {
        len++;
    }
    return len;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--)
    {
        *p++ = (unsigned char)c;
        __asm__ __volatile__("" : "+r"(p));
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
    {
        *d++ = *s++;
        __asm__ __volatile__("" : "+r"(d));
    }
    return dest;
}

int is_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

void skip_space(char **str)
{
    while (**str == ' ' || **str == '\t')
    {
        (*str)++;
    }
}

int s_htoi(char **s, uint32_t *out)
{
    char *str = *s;
    int to_add = 0;
    int digits_found = 0;
    *out = 0;

    skip_space(&str);

    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
    {
        str += 2;
    }

    while (*str && *str != ' ' && *str != '\t' && *str != '\r' && *str != '\n')
    {
        to_add = is_hex(*str);
        if (to_add < 0)
        {
            *out = 0;
            return 0;
        }
        *out = (*out << 4) | (uint32_t)to_add;
        str++;
        digits_found++;
    }

    if (digits_found == 0)
    {
        return 0;
    }

    *s = str;
    return 1;
}

size_t strnlen(const char *str, size_t maxlen)
{
    size_t len = 0U;
    while (len < maxlen && str[len] != '\0')
    {
        len++;
    }
    return len;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++) != '\0')
    {
    }
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n > 0U && (*d++ = *src++) != '\0')
    {
        n--;
    }
    while (n > 0U)
    {
        *d++ = '\0';
        n--;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s)
    {
        while (n--)
        {
            *d++ = *s++;
            __asm__ __volatile__("" : "+r"(d));
        }
    }
    else if (d > s)
    {
        d += n;
        s += n;
        while (n--)
        {
            *--d = *--s;
            __asm__ __volatile__("" : "+r"(d));
        }
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    while (n--)
    {
        if (*p1 != *p2)
        {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!*needle)
    {
        return (char *)haystack;
    }

    for (; *haystack != '\0'; haystack++)
    {
        if (*haystack == *needle)
        {
            const char *h = haystack;
            const char *n = needle;
            while (*h != '\0' && *n != '\0' && *h == *n)
            {
                h++;
                n++;
            }
            if (*n == '\0')
            {
                return (char *)haystack;
            }
        }
    }
    return NULL;
}