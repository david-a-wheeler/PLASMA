/* string.h for ccish: each function is its own object in lib/, so a
 * program links only what it calls (doc/bootstrap-test.md, R4). */
#ifndef STRING_H
#define STRING_H
#include <stddef.h>
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strcpy(char *d, const char *s);
char *strcat(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
char *strncat(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
#endif
