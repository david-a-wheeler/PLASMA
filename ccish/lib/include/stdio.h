/* stdio.h for ccish: unbuffered streams over PLASMA's FILEIO (lib/,
 * doc/bootstrap-test.md R5). A FILE pointer holds FILEIO's reference
 * number for an open file, or is negative for the console, so there's
 * no table of streams: don't look inside one. stdin, stdout and stderr
 * are all the console. Only binary files: on the VM, a newline is
 * whatever byte the file holds. */
#ifndef STDIO_H
#define STDIO_H
#include <stddef.h>
typedef struct __file { char __ref; } FILE;
#define EOF (-1)
#define stdin  ((FILE *)-1)
#define stdout ((FILE *)-2)
#define stderr ((FILE *)-3)
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
int getc(FILE *f);
int putc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int fflush(FILE *f);
int remove(const char *path);
size_t fread(void *p, size_t size, size_t n, FILE *f);
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
/* Not C: the ProDOS file type and aux type of the files fopen creates
 * from now on, BIN ($06) and 0 to start with; cclink makes its REL
 * modules $FE with aux type $1000, so that CMD runs them. */
void __filetype(int type, int aux);
/* There's no buffer to flush: 0, evaluating f once (without the comma
 * operator, which ccish lacks). */
#define fflush(f) ((f) && 0)
#endif
