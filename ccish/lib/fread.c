/* fread.c: C's fread over FILEIO. */
#include <stdio.h>
#include <fileio.h>

/* Read up to n items of size bytes from f into p; how many whole items
 * were read. */
size_t fread(void *p, size_t size, size_t n, FILE *f)
{
  return fileio.read((int)f, p, size * n) / size;
}
