/*
 * The reader of the name file, NAME.nam (../CCPP.md), for cccomp and
 * ccgen: it checks the whole file once, when it is opened, and then
 * looks spellings up by id. It reads through stdio, as the message
 * file is read, so that it needs no 512-byte buffer of its own, and it
 * keeps its place: a lookup reads on from the last one, and starts
 * again from the top only for an earlier line (bio can't seek). Ids are
 * given in the order names are first met, which is nearly the order of
 * the functions and globals a symbol summary lists, so writing all their
 * names takes about one pass.
 */
#include "stdio.h"
#include "string.h"
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "token.h"
#include "nam.h"

int nam_ids;
int nam_sum;

static FILE *nf;                /* the name file, while open */
static const char *base_name;
static int at;                  /* the number of the line nf reads next */
static char line[MAX_PATH];     /* the line last read */

/* Close the name file, if it is open. */
void nam_close(void)
{
  if (nf)
    fclose(nf);
  nf = NULL;
}

/* The name file is damaged, or from another run of ccpp: stop. */
void nam_stale(void)
{
  nam_close();
  fatal(232 /* "name file is stale or damaged:" */,
        stream_name(base_name, SUFFIX_NAM));
}

/* Open the name file, to read it from the start. */
static void reopen(void)
{
  nam_close();
  nf = fopen(stream_name(base_name, SUFFIX_NAM), "rb");
  if (!nf)
    fatal(171 /* "cannot open" */, stream_name(base_name, SUFFIX_NAM));
  at = 1;
}

/* Read the name file's next line into line[], without its newline,
 * adding its bytes and newline to checksum *h; return its length. A
 * line that is too long, or the file's end, is damage. */
static int read_line(int *h)
{
  int c, n = 0;
  for (;;) {
    c = getc(nf);
    if (c == EOF || (n >= MAX_PATH - 1 && c != '\n'))
      nam_stale();
    *h = (*h * 31 + c) & 0xFFFF;
    if (c == '\n')
      break;
    line[n++] = (char)c;
  }
  line[n] = 0;
  at++;
  return n;
}

/* The value of the 4 upper-case hex digits at s, or -1. */
static int hex4(const char *s)
{
  int i, v = 0, c;
  for (i = 0; i < 4; i++) {
    c = s[i];
    if (c >= '0' && c <= '9')
      v = v * 16 + c - '0';
    else if (c >= 'A' && c <= 'F')
      v = v * 16 + c - 'A' + 10;
    else
      return -1;
  }
  return v;
}

/* Open and check the name file of output name base (CCPP.md, "NAME.nam"),
 * before any name in it is used, and keep its stamp in nam_ids and
 * nam_sum, for the caller to match. The names come before the files,
 * none longer than MAX_NAME, at least one file follows them, then the
 * blank line, then a stamp that matches the file, and then nothing. */
void nam_open(const char *base)
{
  int n, h = 0, ids = 0, files = 0, d = 0, before;
  base_name = base;
  reopen();
  for (;;) {
    before = h;
    n = read_line(&h);
    if (n == 0)
      break;
    if (line[0] == '@')
      files++;
    else if (files || n > MAX_NAME)
      nam_stale();      /* a name after the files, or too long */
    else
      ids++;
  }
  h = before;           /* the blank line isn't summed */
  if (read_line(&d) != 12 || line[0] != '='
      || line[1] != '0' + NAM_VERSION || line[2] != ' ' || line[7] != ' '
      || hex4(line + 3) != ids || hex4(line + 8) != h || files == 0
      || getc(nf) != EOF)
    nam_stale();
  nam_close();
  nam_ids = ids;
  nam_sum = h;
}

/* Read line n of the name file (from 1) into line[]. */
static void line_at(int n)
{
  int d = 0;
  if (!nf || n < at)
    reopen();
  while (at <= n)
    read_line(&d);
}

/* The spelling of name (an id), in a static buffer that the next lookup
 * reuses. */
const char *nam_name(int name)
{
  if (name < 1 || name > nam_ids)
    nam_stale();
  line_at(name);
  return line;
}

/* The name of source file k (from 1), in the same static buffer. */
const char *nam_file(int k)
{
  line_at(nam_ids + k);
  if (line[0] != '@')
    nam_stale();
  return line + 1;
}
