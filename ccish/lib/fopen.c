/* fopen.c: C's fopen and fclose over FILEIO. */
#include <stdio.h>
#include <plasma.h>

char *__path(const char *path);

/* Open path: mode "r..." to read, "w..." to write (destroying and
 * creating it first, as a binary file); NULL if that fails, or for any
 * other mode. FILEIO opens a file for both, so "r" and "w" differ only
 * in the create. */
FILE *fopen(const char *path, const char *mode)
{
  char *p = __path(path);
  if (!p)
    return NULL;
  if (*mode == 'w') {
    /* Their results aren't reliable (plvm returns a stale errno), but
     * the open fails if they did. */
    fileio.destroy(p);
    fileio.create(p, 6, 0);
  } else if (*mode != 'r')
    return NULL;
  return (FILE *)fileio.open(p);
}

/* Close f, a file fopen opened; 0. */
int fclose(FILE *f)
{
  fileio.close((int)f);
  return 0;
}
