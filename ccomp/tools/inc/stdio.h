#ifndef STDIO_H
#define STDIO_H
#include <stddef.h>
typedef int FILE;
#define EOF (-1)
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
int getc(FILE *f);
int putc(int c, FILE *f);
int puts(const char *s);
int fflush(FILE *f);
int remove(const char *path);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
#define printf(...) 0
#define fprintf(...) 0
#define sprintf(...) 0
#endif
