#ifndef STRING_H
#define STRING_H

#include <stdint.h>
#include <stddef.h>

int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, size_t n);
size_t strlen(const char *str);
size_t strnlen(const char *str, size_t maxlen);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
int s_htoi(char **str, uint32_t *out);
int is_hex(char c);
void skip_space(char **str);
char *strstr(const char *haystack, const char *needle);

#endif // STRING_H