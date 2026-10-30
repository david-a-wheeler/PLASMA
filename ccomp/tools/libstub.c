/* Stand-ins for the C library ccish's own source calls, so that the
 * measured programs link (tools/measure.py). They do nothing: ccish
 * has no C library yet (M9), and their code isn't counted. main calls
 * the program's renamed main(argc, argv), which ccish doesn't accept
 * yet. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *stdin;
FILE *stdout;
FILE *stderr;

int main_args(int argc, char **argv);

/* The program's entry point. */
int main() { return main_args(0, NULL); }

/* stdio.h */
FILE *fopen(const char *path, const char *mode) { return NULL; }
int fclose(FILE *f) { return 0; }
int getc(FILE *f) { return EOF; }
int putc(int c, FILE *f) { return c; }
int puts(const char *s) { return 0; }
int fflush(FILE *f) { return 0; }
int remove(const char *path) { return 0; }
size_t fread(void *p, size_t size, size_t n, FILE *f) { return 0; }
size_t fwrite(const void *p, size_t size, size_t n, FILE *f) { return 0; }

/* stdlib.h */
void exit(int status) { }
void free(void *p) { }

/* string.h */
int strcmp(const char *a, const char *b) { return 0; }
char *strncat(char *d, const char *s, size_t n) { return d; }
char *strncpy(char *d, const char *s, size_t n) { return d; }
size_t strlen(const char *s) { return 0; }
char *strchr(const char *s, int c) { return NULL; }
char *strrchr(const char *s, int c) { return NULL; }
void *memmove(void *d, const void *s, size_t n) { return d; }
/* memcpy, memset, strcpy and strcat come from CMDSYS. */
