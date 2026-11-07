/*
 * The library a build by ccish itself needs, for the bootstrap test
 * (doc/bootstrap-test.md): the string functions (libc's, on the
 * host), stdio and exit (src/std/stdio.h), and the command line
 * (the VM's, not a host argv). Only compiled into a module; the host
 * build never sees the body.
 *
 * The stdio design: files go through the VM's SOS file calls, the
 * parameter-block protocol of the PLASMA source's
 * libsrc/apple/fileio.pla (a2open, a23read, a2write, a23destroy),
 * through CMDSYS's 6502 syscall natv, exported as CALL
 * (vmsrc/cmdsys.c). That VM-side implementation is not written yet,
 * so a module that opens a file reports the SOS error it gets; a
 * planned way to provide it is a plvm option that maps these calls
 * onto ordinary host files in a directory. Messages go to the
 * console one byte at a time, CMDSYS's putc. stdout and stderr are
 * the same console under the VM.
 */
#ifdef __CCISH__
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "build.h"

/* The string functions (src/std/string.h). */

/* Character by character, unsigned, as C requires. */
int strcmp(const char *a, const char *b)
{
  while (*a && *a == *b)
    { a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}

/* The count of bytes before the NUL. */
size_t strlen(const char *s)
{
  const char *p = s;
  while (*p)
    p++;
  return p - s;
}

/* Copy with the NUL; return the start. */
char *strcpy(char *dst, const char *src)
{
  char *d = dst;
  while ((*d++ = *src++) != '\0')
    ;
  return dst;
}

/* Append with the NUL; return the start. */
char *strcat(char *dst, const char *src)
{
  char *d = dst + strlen(dst);
  while ((*d++ = *src++) != '\0')
    ;
  return dst;
}

/* Copy up to n bytes, NUL padding; return the start. */
char *strncpy(char *dst, const char *src, size_t n)
{
  size_t i;
  for (i = 0; i < n && src[i]; i++)
    dst[i] = src[i];
  for (; i < n; i++)
    dst[i] = '\0';
  return dst;
}

/* The first c, or its NUL when c is 0, else 0. */
char *strchr(const char *s, int c)
{
  for (; *s; s++)
    if (*s == c)
      return (char *)s;
  return *s == c ? (char *)s : (char *)0;
}

/* The last c, or its NUL when c is 0, else 0. */
char *strrchr(const char *s, int c)
{
  char *last = (char *)0;
  for (; *s; s++)
    if (*s == c)
      last = (char *)s;
  if (!*s && *s == c)
    last = (char *)s;
  return last;
}

/* Append up to n bytes of src, NUL ending; return the start. */
char *strncat(char *dst, const char *src, size_t n)
{
  char *d = dst + strlen(dst);
  size_t i;
  for (i = 0; i < n && src[i]; i++)
    d[i] = src[i];
  d[i] = '\0';
  return dst;
}

/* Copy n bytes over an overlap, either way; return the start. */
void *memmove(void *dst, const void *src, size_t n)
{
  unsigned char *d = dst;
  const unsigned char *s = src;
  size_t i;
  if (d < s) {
    for (i = 0; i < n; i++)
      d[i] = s[i];
  } else {
    for (i = n; i > 0; i--)
      d[i - 1] = s[i - 1];
  }
  return dst;
}

/* Set n bytes; return the start. */
/* stdio and exit (src/std/stdio.h, src/std/stdlib.h).
 * memset and memcpy are not defined here: they are CMDSYS imports
 * (src/std/string.h, src/parse.c cmdsys_sigs). */

/* SOS (ProDOS/CFFA2) function numbers. */
#define SOS_OPENIN  0xC7
#define SOS_OPENOUT 0xC8
#define SOS_READ    0xCA
#define SOS_WRITE   0xCB
#define SOS_CLOSE   0xCC
#define SOS_DESTROY 0xC1

#define MAX_OPEN 8

struct file {
  int refnum;   /* the VM's reference number; the console is
                   recognized by its address, not by this */
  int writing;
};

/* CMDSYS (src/parse.c, cmdsys_sigs): the 6502 syscall, and the
 * console byte. call's second argument is the parameter count and its
 * third the parameter block's address; the block's byte 0 is that
 * count, and its words are the values (fileio.pla). The signature has
 * five int parameters, and the extra ones are unused. */
extern int call(int cmd, int n, int params, int r1, int r2);
extern void putc(char c);

static char sosblock[12];

/* A little-endian word at byte off of the block. */
static void sosw(int off, int value)
{
  sosblock[off] = (char)value;
  sosblock[off + 1] = (char)(value >> 8);
}

/* The little-endian word at byte off of the block. */
static int sosl(int off)
{
  return (unsigned char)sosblock[off]
       | ((unsigned char)sosblock[off + 1] << 8);
}

/* Zero the block, set its count, and make the call. */
static int sos(int cmd, int n)
{
  int i;
  for (i = 0; i < 12; i++)
    sosblock[i] = 0;
  sosblock[0] = (char)n;
  return call(cmd, n, (int)sosblock, 0, 0);
}

static struct file files[MAX_OPEN];
static int nopen;

/* The console: the VM's two streams are one. */
static struct file std_console;
FILE *stdout = &std_console;
FILE *stderr = &std_console;

/* One byte to the VM's console. */
static void conputc(int c)
{
  putc((char)c);
}

/* The console stream, by address. */
static int is_console(FILE *f)
{
  return f == &std_console;
}

/* mode: "rb" or "wb" (bio.c). The refnum comes back in the block's
 * byte 5, as fileio.pla's a2open reads it. */
FILE *fopen(const char *path, const char *mode)
{
  int err, ref;
  if (nopen >= MAX_OPEN)
    return 0;
  sosw(1, (int)path);
  sosw(3, 0);  /* no iobuf */
  if (mode[0] == 'w')
    err = sos(SOS_OPENOUT, 3);
  else
    err = sos(SOS_OPENIN, 3);
  ref = (unsigned char)sosblock[5];
  if (err || !ref)
    return 0;
  files[nopen].refnum = ref;
  files[nopen].writing = (mode[0] == 'w');
  nopen++;
  return &files[nopen - 1];
}

/* Read up to size times n bytes; return the elements got. */
size_t fread(void *dst, size_t size, size_t n, FILE *f)
{
  int err, got;
  if (!f || is_console(f) || f->writing)
    return 0;
  sosw(1, f->refnum);
  sosw(3, (int)dst);
  sosw(5, (int)(size * n));
  err = sos(SOS_READ, 4);
  if (err)
    return 0;
  got = sosl(7);
  return (size_t)got / (size ? (int)size : 1);
}

/* Write size times n elements; return the elements put. */
size_t fwrite(const void *src, size_t size, size_t n, FILE *f)
{
  int err, put;
  if (!f || is_console(f) || !f->writing)
    return 0;
  sosw(1, f->refnum);
  sosw(3, (int)src);
  sosw(5, (int)(size * n));
  err = sos(SOS_WRITE, 4);
  if (err)
    return 0;
  put = sosl(7);
  return (size_t)put / (size ? (int)size : 1);
}

/* Close, and drop the file from the table. */
int fclose(FILE *f)
{
  int i, err;
  if (!f || is_console(f))
    return -1;
  sosw(1, f->refnum);
  err = sos(SOS_CLOSE, 1);
  for (i = (int)(f - files); i < nopen - 1; i++)
    files[i] = files[i + 1];
  nopen--;
  return err ? -1 : 0;
}

/* The VM's writes go out at once. */
int fflush(FILE *f)
{
  (void) f;
  return 0;
}

/* The PLASMA source's a23destroy: DESTROY, [1, path]. */
int remove(const char *path)
{
  int err;
  sosw(1, (int)path);
  err = sos(SOS_DESTROY, 1);
  return err;
}

/* To the console byte by byte; to a file, one SOS write. */
int fputs(const char *s, FILE *f)
{
  int err, len, off;
  if (!f)
    return -1;
  len = (int)strlen(s);
  if (is_console(f)) {
    while (*s)
      conputc(*s++);
    return 0;
  }
  sosw(1, f->refnum);
  sosw(3, (int)s);
  sosw(5, len);
  err = sos(SOS_WRITE, 4);
  off = sosl(7);
  return err || off < len ? -1 : 0;
}

/* Fill a buffer with a number's digits, most significant first, and
 * return their count. */
static int decbuf(char *b, int v)
{
  int n = 0, i;
  if (v == 0)
    b[n++] = '0';
  while (v > 0) {
    b[n++] = (char)('0' + v % 10);
    v /= 10;
  }
  for (i = 0; i < n / 2; i++) {
    char t = b[i];
    b[i] = b[n - 1 - i];
    b[n - 1 - i] = t;
  }
  return n;
}

/* width hex digits, zero padded, most significant first. */
static void hexbuf(char *b, int v, int upper, int width)
{
  int i;
  for (i = 0; i < width; i++) {
    int d = (v >> (4 * (width - 1 - i))) & 0xF;
    if (d < 10)
      b[i] = (char)('0' + d);
    else
      b[i] = (char)(upper ? 'A' + d - 10 : 'a' + d - 10);
  }
}

/* The formats here use: %d, %+d, %s, %02X, %04X, and %%. */
int printf(const char *fmt, int a1, int a2, int a3, int a4, int a5)
{
  int args[5];
  int ai;
  const char *p;
  args[0] = a1;
  args[1] = a2;
  args[2] = a3;
  args[3] = a4;
  args[4] = a5;
  ai = 0;
  for (p = fmt; *p; p++) {
    int c = *p;
    if (c != '%') {
      conputc(c);
      continue;
    }
    p++;
    if (!*p)
      break;
    c = *p;
    if (c == '%') {
      conputc('%');
      continue;
    }
    {
      int sign = 0, zero = 0, width = 0, upper = 0, v, val;
      char b[8];
      int n, i;
      if (c == '+') {
        sign = 1;
        p++;
        c = *p;
      } else if (c == '0') {
        zero = 1;
        p++;
        c = *p;
      }
      while (c >= '0' && c <= '9') {
        width = width * 10 + (c - '0');
        p++;
        c = *p;
      }
      v = args[ai < 5 ? ai : 0];
      ai++;
      if (c == 's') {
        const char *s = (const char *)v;
        while (*s)
          conputc(*s++);
        continue;
      }
      if (c == 'x' || c == 'X') {
        upper = (c == 'X');
        if (!width)
          width = 4;
        if (width > 4)
          width = 4;
        hexbuf(b, v & 0xFFFF, upper, width);
        for (i = 0; i < width; i++)
          conputc(b[i]);
        continue;
      }
      val = v;
      if (val < 0) {
        conputc('-');
        val = -val;
      } else if (sign) {
        conputc('+');
      }
      n = decbuf(b, val);
      if (zero && width > n)
        for (i = n; i < width; i++)
          conputc('0');
      else
        for (i = 0; i < width - n; i++)
          conputc(' ');
      for (i = 0; i < n; i++)
        conputc(b[i]);
    }
  }
  return 0;
}

/* One byte from a file, or EOF (the getc of src/std/stdio.h). */
int getc_f(FILE *f)
{
  char c;
  if (!fread(&c, 1, 1, f))
    return EOF;
  return (unsigned char)c;
}

/* The VM has no exit: report, and spin until the host stops us. */
void exit(int code)
{
  printf("exit %d\n", code, 0, 0, 0, 0);
  for (;;)
    ;
}

#define CMDLINE_STR 0x01FF
#define CMDLINE_BUF 0x0200

#define MAX_ARGV 16

static char *argv_list[MAX_ARGV + 1];
static char progname[8];

/* The command line: the VM (plvm) passes a module no arguments; it
 * joins the module's command-line words, with a space after each,
 * into the fixed buffer CMDLINE_BUF, and stores their count, without
 * the last space, in the byte before it, CMDLINE_STR
 * (vmsrc/cmdsys.c). cmd_argv splits that, in place, into an argv
 * like a host's, with a program name first. The host build never
 * calls it. */
void cmd_argv(int *argc, char ***argv)
{
  char *buf = (char *)CMDLINE_BUF;
  int n = (unsigned char)*(char *)CMDLINE_STR;
  int i, na = 0;
  progname[0] = 'c';
  progname[1] = 'c';
  progname[2] = '\0';
  argv_list[0] = progname;
  if (n <= 0) {
    *argc = 1;
    *argv = argv_list;
    return;
  }
  for (i = 0; i < n; i++)
    if (buf[i] == ' ')
      buf[i] = '\0';
  i = 0;
  while (i < n && na < MAX_ARGV) {
    if (buf[i]) {
      argv_list[++na] = buf + i;
      while (i < n && buf[i])
        i++;
    } else
      i++;
  }
  *argc = na + 1;
  *argv = argv_list;
}

#else
/* The host build: libc provides the strings, stdio and argv. */
int __vmsys_host(void);
#endif
