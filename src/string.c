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