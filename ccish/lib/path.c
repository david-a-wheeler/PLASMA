/* path.c: C path names to FILEIO's, for fopen and remove. */
#include <string.h>

/* The last path converted. A path under plvm is a host path, longer
 * than ProDOS's 64 characters if the directory is deep. */
static char buf[128];

/* Path as a PLASMA string (a length byte, then the text), in a static
 * buffer, or NULL if it's too long. */
char *_path(const char *path)
{
  size_t n = strlen(path);
  if (n >= sizeof buf)
    return NULL;
  buf[0] = (char)n;
  __cmd_memcpy(buf + 1, path, n);
  return buf;
}
