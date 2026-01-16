#include "string.h"

/* Compare str1 and str2
Returns 0 if equal, a positive value if str1 is greater, a negative value otherwise */
int strcmp(const char *str1, const char *str2)
{
    while (*str1)
    {
        if (*str1 != *str2) return *str1 - *str2;
        str1++;
        str2++;
    }
    if (*str2) return -1;
    return 0;
}

/* Converts the next hexadecimal number in the string into an integer, alters the input string to point to the next space character or the end of the string.
Returns: 1 if successful, 0 if the string was invalid.
Output: writes the parsed value to *out on success
        points string to next ' ' character or '\0' (whichever is first) */
int s_htoi(char **s, uint32_t *out)
{
    char *str = *s;
    int to_add = 0;
    *out = 0;
    if (*str == ' ') skip_space(&str);
    if (*str && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;    // If string starts with 0x or 0X ignore prefix
    while(*str && *str != ' ')
    {
        if ((to_add = is_hex(*str++)) < 0)
        {
            *out = 0;
            return 0;
        }
        *out <<= 4;
        *out += to_add;
    }
    *s = str;
    return 1;
}

/* Converts a hex character into int
Returns the 0-15 int value if valid, otherwise -1. */
int is_hex(char c)
{
    if (c >= '0' && c <= '9') return c - 48;
    if (c >= 'A' && c <= 'F') return c - 55;
    if (c >= 'a' && c <= 'f') return c - 87;
    return -1;
}

void skip_space(char **str)
{
    while (**str == ' ') (*str)++;
}