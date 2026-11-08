/* remove.c: C's remove over FILEIO. */
#include <stdio.h>
#include <fileio.h>

char *__path(const char *path);

/* Remove the file path; 0, or EOF if path is too long. FILEIO's
 * destroy gives no reliable result under plvm (a stale errno), so a
 * missing file isn't reported. */
int remove(const char *path)
{
  char *p = __path(path);
  if (!p)
    return EOF;
  fileio.destroy(p);
  return 0;
}
