#ifndef STRING_H
#define STRING_H
#include <stddef.h>
int strcmp(const char *a, const char *b);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, size_t n);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
void *memmove(void *d, const void *s, size_t n);
/* The standard ones: ccish's own struct copies and array zeroing call
 * CMDSYS's __memcpy and __memset, a different name, so these don't
 * conflict with them (approach.md decision 35). */
void *memcpy(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
#endif
