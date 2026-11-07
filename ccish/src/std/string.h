/* string.h for ccish: the parts the compiler's source uses. The
 * host build gets libc's; a build by ccish itself (__CCISH__) gets
 * the ones in src/vmsys.c. */
#ifndef STRING_H
#define STRING_H

#include "stddef.h"

int  strcmp(const char *a, const char *b);
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strcat(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strrchr(const char *s, int c);
char *strncat(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
void *memmove(void *dst, const void *src, size_t n);
/* memset and memcpy are CMDSYS functions (src/parse.c,
 * cmdsys_sigs): their signatures must match there, and a call to
 * them in a module is an import, not a call into this build. */
void memset(void *dst, int c, int n);
void memcpy(void *dst, const void *src, int n);

#endif
