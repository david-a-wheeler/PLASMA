/* stdio.h for ccish: the parts the compiler's source uses. The host
 * build gets libc's; a build by ccish itself (__CCISH__) gets the
 * one in src/vmsys.c, where files go through the VM's SOS file calls
 * and the console goes through CMDSYS. */
#ifndef STDIO_H
#define STDIO_H

#include "stddef.h"

typedef struct file FILE;

extern FILE *stdout;
extern FILE *stderr;

#define EOF (-1)

/* getc is a CMDSYS function (src/parse.c, cmdsys_sigs) with a
 * different signature, so the C one goes by another name here. */
#define getc getc_f

FILE *fopen(const char *path, const char *mode);
size_t fread(void *dst, size_t size, size_t n, FILE *f);
size_t fwrite(const void *src, size_t size, size_t n, FILE *f);
int  fclose(FILE *f);
int  fflush(FILE *f);
int  fputs(const char *s, FILE *f);
int  remove(const char *path);
int  getc_f(FILE *f);
/* A build by ccish has no variadic arguments (section 9.2), so
 * printf takes the most a call here uses: a format and five
 * values, and every call passes all six. */
int  printf(const char *fmt, int a1, int a2, int a3, int a4,
            int a5);

#endif
