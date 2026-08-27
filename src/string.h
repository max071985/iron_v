#ifndef STRING_H
#define STRING_H

#include <stdint.h>
#include <stddef.h>

int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, size_t n);
size_t strlen(const char *str);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int s_htoi(char **str, uint32_t *out);
int is_hex(char c);
void skip_space(char **str);

#endif // STRING_H