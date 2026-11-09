// Block-buffered file I/O (bio.c): every file through a small buffer.
// Include after <stdio.h>.
#ifndef BIO_H
#define BIO_H

#define BLOCK        128    // file I/O buffer size (ProDOS buffers
                            // each file's blocks itself)

typedef struct {
  FILE *f;
  int writing;
  int pos, len;       // next byte in buf; bytes in buf when reading
  int error;          // a write failed
  unsigned char buf[BLOCK];
} Bfile;

int  bopen(Bfile *b, const char *path, int writing);
int  bgetc(Bfile *b);
void bungetc(Bfile *b, int c);
void bfill(Bfile *b);               // refill the read buffer
void bputc(Bfile *b, int c);
void bputw(Bfile *b, int v);
void bwrite(Bfile *b, const unsigned char *p, int n);
int  bclose(Bfile *b);

#endif
