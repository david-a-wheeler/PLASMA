/* fopen.c: C's fopen and fclose over FILEIO. */
#include <stdio.h>
#include <fileio.h>

char *__path(const char *path);

/* On an Apple II, FILEIO opens a file only into a free 1K I/O buffer:
 * it has one, ProDOS's system buffer at $0800, and more only from
 * iobufalloc, which takes them from the heap, at most MAX_IOBUFS in
 * all (src/libsrc/apple/fileio.pla). So fopen asks for one more each
 * time every buffer is in use: nopen files open, nbufs buffers. They
 * stay for later files, and go with the program's heap; CMD discards
 * FILEIO then too, unless a kept module had loaded it, whose table
 * would then point into freed memory. (plvm needs no buffers, and its
 * iobufalloc does nothing.) */
#define MAX_IOBUFS 4
static int nopen;
static int nbufs = 1;

/* Open path: mode "r..." to read, "w..." to write (destroying and
 * creating it first, as a binary file); NULL if that fails, or for any
 * other mode. FILEIO opens a file for both, so "r" and "w" differ only
 * in the create. */
FILE *fopen(const char *path, const char *mode)
{
  char *p = __path(path);
  int ref;
  if (!p)
    return NULL;
  if (*mode == 'w') {
    /* Their results aren't reliable (plvm returns a stale errno), but
     * the open fails if they did. */
    fileio.destroy(p);
    fileio.create(p, 6, 0);
  } else if (*mode != 'r')
    return NULL;
  if (nopen == nbufs && nbufs < MAX_IOBUFS) {
    fileio.iobufalloc(2);       /* the system buffer and one more */
    nbufs++;
  }
  if ((ref = fileio.open(p)) != 0)
    nopen++;
  return (FILE *)ref;
}

/* Close f, a file fopen opened; 0. */
int fclose(FILE *f)
{
  if ((int)f > 0) {
    fileio.close((int)f);
    nopen--;
  }
  return 0;
}
