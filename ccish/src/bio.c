/*
 * Block-buffered file I/O. Every file the compiler reads or writes goes
 * through a 512-byte buffer, the size of a ProDOS block, so that on an
 * Apple II each system call moves a whole block and not a byte
 * (approach.md 18.1). On the host it sits on top of stdio.
 */
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "diag.h"
#include "bio.h"

/* Open path for reading (writing = 0) or writing (writing = 1).
 * Returns 0 if the file can't be opened. */
int bopen(Bfile *b, const char *path, int writing)
{
  memset(b, 0, sizeof(*b));
  b->f = fopen(path, writing ? "wb" : "rb");
  b->writing = writing;
  return b->f != NULL;
}

/* Refill the read buffer with the file's next block (len = 0 at the
 * end of the file). */
static void fill(Bfile *b)
{
  b->len = (int)fread(b->buf, 1, BLOCK, b->f);
  b->pos = 0;
}

/* The next byte, or EOF at the end of the file. */
int bgetc(Bfile *b)
{
  if (b->pos >= b->len) {
    fill(b);
    if (b->len == 0)
      return EOF;
  }
  return b->buf[b->pos++];
}

/* Push back the byte just read by bgetc. Only one byte, and only right
 * after a bgetc that didn't return EOF. */
void bungetc(Bfile *b, int c)
{
  b->buf[--b->pos] = (unsigned char)c;
}

/* A little-endian word. Running out of data here means the file is
 * damaged, which only the final pass can see. */
int bgetw(Bfile *b)
{
  int lo = bgetc(b), hi = bgetc(b);
  if (lo == EOF || hi == EOF)
    fatal(3 /* "unexpected end of a compiler stream file" */, NULL);
  return lo | (hi << 8);
}

/* Read past the next n bytes, which must be there: the rest of the
 * buffer at a time, not a byte at a time. */
void bskip(Bfile *b, int n)
{
  int k;
  while (n > 0) {
    if (b->pos >= b->len) {
      fill(b);
      if (b->len == 0)
        fatal(3 /* "unexpected end of a compiler stream file" */, NULL);
    }
    k = b->len - b->pos;
    if (k > n)
      k = n;
    b->pos += k;
    n -= k;
  }
}

/* Write out the buffered bytes, remembering any write error for
 * bclose to report. */
static void flush(Bfile *b)
{
  if (b->pos > 0 && fwrite(b->buf, 1, b->pos, b->f) != (size_t)b->pos)
    b->error = 1;
  b->pos = 0;
}

/* Write one byte. */
void bputc(Bfile *b, int c)
{
  if (b->pos >= BLOCK)
    flush(b);
  b->buf[b->pos++] = (unsigned char)c;
}

/* Write a 16-bit word, low byte first. */
void bputw(Bfile *b, int v)
{
  bputc(b, v & 0xFF);
  bputc(b, (v >> 8) & 0xFF);
}

/* Write n bytes starting at p. */
void bwrite(Bfile *b, const unsigned char *p, int n)
{
  while (n-- > 0)
    bputc(b, *p++);
}

/* Close the file, writing any buffered bytes. Returns 0 on success. */
int bclose(Bfile *b)
{
  int bad;
  if (!b->f)
    return 0;
  if (b->writing)
    flush(b);
  bad = fclose(b->f) != 0 || b->error;
  b->f = NULL;
  return bad ? -1 : 0;
}
