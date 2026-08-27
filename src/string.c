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