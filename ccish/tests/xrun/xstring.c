/* xstring.c: the library's string.h (lib/), against the host's: each
 * function, with the edge cases (empty strings, a NUL searched for, n
 * of 0, padding, overlapping moves both ways, a nonzero memset of an
 * odd length). Printed through the library's stdio, so no ptest.h.
 * strcmp's result is compared only by sign, as C defines it. */
#include <stdio.h>
#include <string.h>

/* Print n in decimal, then a space. */
static void num(int n)
{
  char buf[8];
  int i = 0;
  if (n < 0) {
    putc('-', stdout);
    n = -n;
  }
  do {
    buf[i++] = (char)('0' + n % 10);
    n /= 10;
  } while (n);
  while (i)
    putc(buf[--i], stdout);
  putc(' ', stdout);
}

/* Print the sign of n: -1, 0 or 1. */
static void sign(int n)
{
  num(n < 0 ? -1 : n > 0);
}

/* Print n bytes of p, a NUL as '.'. */
static void bytes(const char *p, int n)
{
  int i;
  for (i = 0; i < n; i++)
    putc(p[i] ? p[i] : '.', stdout);
  putc(' ', stdout);
}

int main(void)
{
  char a[16], b[16];
  const char *s = "a/bc/d";
  const char *e = "";

  num((int)strlen(""));
  num((int)strlen("hello"));
  sign(strcmp("abc", "abc"));
  sign(strcmp("abc", "abd"));
  sign(strcmp("abd", "abc"));
  sign(strcmp("ab", "abc"));
  sign(strcmp("abc", "ab"));
  sign(strcmp("\x80", "a"));     /* unsigned chars */
  sign(strcmp("", ""));
  fputs("\n", stdout);

  fputs(strcpy(a, "one"), stdout);
  fputs(strcat(a, "two"), stdout);
  fputs(strcat(a, ""), stdout);
  putc(' ', stdout);
  memset(b, 'x', sizeof b);
  strncpy(b, "abc", 6);          /* padded with NULs to 6 */
  bytes(b, 8);
  memset(b, 'x', sizeof b);
  strncpy(b, "abcdef", 3);       /* no NUL */
  bytes(b, 5);
  strcpy(a, "ab");
  fputs(strncat(a, "cdef", 2), stdout);
  fputs(strncat(a, "gh", 5), stdout);
  fputs(strncat(a, "zz", 0), stdout);
  fputs("\n", stdout);

  num((int)(strchr(s, '/') - s));
  num((int)(strrchr(s, '/') - s));
  num((int)(strchr(s, 0) - s));
  num((int)(strrchr(s, 0) - s));
  num(strchr(s, 'z') == NULL);
  num(strrchr(s, 'z') == NULL);
  num((int)(strchr(s, 'a') - s));
  num((int)(strrchr(e, 0) - e));
  fputs("\n", stdout);

  memset(a, 0, sizeof a);
  memset(a, 'q', 5);             /* odd: CMDSYS fills by words */
  bytes(a, 7);
  memset(a, 0x141, 2);           /* only the low byte, 'A' */
  bytes(a, 3);
  memcpy(b, "123456789", 10);
  bytes(b, 10);
  memmove(b + 2, b, 5);          /* overlapping, up */
  bytes(b, 10);
  memcpy(b, "123456789", 10);
  memmove(b, b + 3, 6);          /* overlapping, down */
  bytes(b, 10);
  num(memcpy(a, b, 0) == a);
  num(memmove(a, b, 0) == a);
  num(memset(a, 0, 0) == a);
  fputs("\n", stdout);
  return 0;
}
