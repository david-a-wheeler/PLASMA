/* fread.c: C's fread over FILEIO. */
#include <stdio.h>
#include <plasma.h>

/* Read up to n items of size bytes from f into p; how many whole items
 * were read. */
size_t fread(void *p, size_t size, size_t n, FILE *f)
{
  size_t got = fileio.read((int)f, p, size * n);
  return size == 1 ? got : got / size;
}
