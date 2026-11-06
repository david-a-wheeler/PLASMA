/*
 * Block-buffered file I/O (bio.c): every file through a 512-byte
 * buffer, a ProDOS block. Include after <stdio.h>.
 */
#ifndef BIO_H
#define BIO_H

#define BLOCK        512    /* file I/O buffer size: one ProDOS block */

typedef struct {
  FILE *f;
  int writing;
  int pos, len;       /* next byte in buf; bytes in buf when reading */
  int error;          /* a write failed */
  unsigned char buf[BLOCK];
} Bfile;

int  bopen(Bfile *b, const char *path, int writing);
int  bgetc(Bfile *b);
void bungetc(Bfile *b, int c);
int  bgetw(Bfile *b);
void bskip(Bfile *b, int n);
void bputc(Bfile *b, int c);
void bputw(Bfile *b, int v);
void bwrite(Bfile *b, const unsigned char *p, int n);
int  bclose(Bfile *b);

#endif
