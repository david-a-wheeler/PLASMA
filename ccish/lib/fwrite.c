/* fwrite.c: C's fwrite over FILEIO. */
#include <stdio.h>
#include <plasma.h>

/* Write n items of size bytes from p to f; how many whole items were
 * written. */
size_t fwrite(const void *p, size_t size, size_t n, FILE *f)
{
  size_t put = fileio.write((int)f, (char *)p, size * n);
  return size == 1 ? put : put / size;
}
