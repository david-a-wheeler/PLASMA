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
/* As CMDSYS has them, returning nothing: ccish's own struct copies and
 * array zeroing call these, so the standard void * versions would
 * conflict at link time. ccish's source never uses their results. */
void memcpy(void *d, const void *s, size_t n);
void memset(void *d, int c, size_t n);
#endif
