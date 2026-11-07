/*
 * ccpp's name pool (../CCPP.md, "NAME.nam"; approach.md, "ccpp
 * design"). Each entry is a length byte and the characters, with no
 * NUL: an identifier, or, with ENT_FILE in its length byte, a source
 * file's name. An identifier's length byte also says whether it names a
 * macro now (ENT_MACRO). Entries are never removed, so an identifier's
 * id is simply its place among the identifiers, and NAME.nam is the pool
 * written out in order, with no table from ids to entries.
 */
#include "stdio.h"
#include "string.h"
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "bio.h"
#include "token.h"
#include "fixed.h"
#include "ppname.h"

#define MAX_PP_POOL  8432   /* bytes of ccpp's names and file names,
                             * with a length byte each (7,665: parse.c
                             * with its headers, macros' names too) */

#define ENT_FILE  0x80          /* in a length byte: a file's name */
#define ENT_MACRO 0x40          /* an identifier that names a macro */
#define FILE_LEN  0x7F          /* a file's name's length */
#define NAME_LEN  0x1F          /* an identifier's (at most MAX_NAME) */

int pp_macro_flag;

static unsigned char pool[MAX_PP_POOL];
static int pool_used;
static int nids;                /* identifiers in the pool */

/* The names every file has, in id order from 1 (CCPP.md, "The fixed
 * ids"): the CMDSYS functions in cmdsys_sigs[]'s order (parse.c), main
 * and pstr, then the names ccpp itself gives a meaning. */
static const char *const fixed[NFIXED] = {
  "putc", "putln", "puts", "puti", "putb", "puth", "getc", "gets",
  "call", "syscall", "heapmark", "heapallocalign", "heapalloc",
  "heaprelease", "heapavail", "memset", "memcpy", "strcpy", "strcat",
  "toupper", "sext", "isugt", "isuge", "isult", "isule", "main", "pstr",
  "__CCISH__", "__FILE__", "__LINE__", "defined"
};

/* The length of the entry at pool offset i. */
static int entry_len(int i)
{
  return pool[i] & ENT_FILE ? pool[i] & FILE_LEN : pool[i] & NAME_LEN;
}

/* Is the entry at pool offset i spelled s (n chars, as long as it)? */
static int same(int i, const char *s, int n)
{
  int j;
  for (j = 0; j < n; j++)
    if (pool[i + 1 + j] != (unsigned char)s[j])
      return 0;
  return 1;
}

/* Find the entry of kind kind (0 or ENT_FILE) spelled s (n chars):
 * return its number among the entries of that kind, from 1, or 0 if
 * there is none; its offset goes in *at. */
static int find(const char *s, int n, int kind, int *at)
{
  int i = 0, k = 0, len;
  while (i < pool_used) {
    len = entry_len(i);
    if ((pool[i] & ENT_FILE) == kind) {
      k++;
      if (len == n && same(i, s, n)) {
        *at = i;
        return k;
      }
    }
    i += len + 1;
  }
  return 0;
}

/* The pool offset of entry k (from 1) of kind kind. */
static int entry_at(int k, int kind)
{
  int i = 0, j = 0;
  for (;;) {
    if ((pool[i] & ENT_FILE) == kind && ++j == k)
      return i;
    i += entry_len(i) + 1;
  }
}

/* Add an entry of kind kind spelled s (n chars). */
static void add(const char *s, int n, int kind)
{
  if (pool_used + n + 1 > MAX_PP_POOL)
    fatal(18 /* "too many names" */, "(the name pool is full)");
  pool[pool_used] = (unsigned char)(n | kind);
  memcpy(pool + pool_used + 1, s, n);
  pool_used += n + 1;
}

/* Start a new pool with the fixed ids. */
void pp_names_init(void)
{
  int i;
  pool_used = 0;
  nids = 0;
  for (i = 0; i < NFIXED; i++)
    pp_intern(fixed[i], (int)strlen(fixed[i]));
}

/* The id of identifier s, of n characters (at most MAX_NAME), giving
 * it the next one if it is new; pp_macro_flag says whether it names a
 * macro. */
int pp_intern(const char *s, int n)
{
  int at, id = find(s, n, 0, &at);
  if (id) {
    pp_macro_flag = (pool[at] & ENT_MACRO) != 0;
    return id;
  }
  add(s, n, 0);
  pp_macro_flag = 0;
  return ++nids;
}

/* Record whether identifier id names a macro (on is 1) or not. */
void pp_set_macro(int id, int on)
{
  int i = entry_at(id, 0);
  if (on)
    pool[i] |= ENT_MACRO;
  else
    pool[i] &= ~ENT_MACRO;
}

/* Does identifier id name a macro? */
int pp_is_macro(int id)
{
  return (pool[entry_at(id, 0)] & ENT_MACRO) != 0;
}

/* The number of source file path, adding it if it is new. */
int pp_add_file(const char *path)
{
  int n = (int)strlen(path), at, k;
  if (n > FILE_LEN)
    fatal(1 /* "output name too long" */, NULL);
  k = find(path, n, ENT_FILE, &at);
  if (k)
    return k;
  add(path, n, ENT_FILE);
  return find(path, n, ENT_FILE, &at);
}

/* Copy the name of source file k into buf (MAX_PATH bytes). */
void pp_file_path(int k, char *buf)
{
  int i = entry_at(k, ENT_FILE), n = entry_len(i);
  memcpy(buf, pool + i + 1, n);
  buf[n] = 0;
}

/* The spelling of identifier id, in a static buffer that the next call
 * reuses, for messages. */
const char *pp_id_spelling(int id)
{
  static char buf[MAX_NAME + 1];
  int i = entry_at(id, 0), n = entry_len(i);
  memcpy(buf, pool + i + 1, n);
  buf[n] = 0;
  return buf;
}

/* The hash of id's spelling, as cccomp's type hashes need it for a
 * struct tag (CCPP.md, TC_TAG): adding its n bytes one at a time to a
 * hash h, as h = h * 31 + byte, gives h * *mul + the result, where
 * *mul is 31^n; all mod 65536. */
int pp_name_hash(int id, int *mul)
{
  int i = entry_at(id, 0), len = entry_len(i), j, h = 0, m = 1;
  for (j = 1; j <= len; j++) {
    h = (h * 31 + pool[i + j]) & 0xFFFF;
    m = (m * 31) & 0xFFFF;
  }
  *mul = m;
  return h;
}

#ifdef CCISH_STATS
/* The name pool's bytes in use (CCISH_STATS). */
int pp_pool_used(void)
{
  return pool_used;
}
#endif

/* The number of ids given so far. */
int pp_nids(void)
{
  return nids;
}

/* Write byte c to f, adding it to checksum *h. */
static void put_sum(Bfile *f, int c, int *h)
{
  bputc(f, c);
  *h = (*h * 31 + c) & 0xFFFF;
}

/* Write v as 4 upper-case hex digits. */
static void put_hex(Bfile *f, int v)
{
  int i, d;
  for (i = 12; i >= 0; i -= 4) {
    d = (v >> i) & 15;
    bputc(f, d < 10 ? '0' + d : 'A' + d - 10);
  }
}

/* Write the pool to f as NAME.nam: the identifiers, a line each; then
 * the files, each after an '@'; a blank line; and the stamp. Return
 * the checksum, for NAME.i's stamp too. */
int pp_names_write(Bfile *f)
{
  int pass, i, j, len, h = 0;
  for (pass = 0; pass <= ENT_FILE; pass += ENT_FILE)
    for (i = 0; i < pool_used; i += len + 1) {
      len = entry_len(i);
      if ((pool[i] & ENT_FILE) != pass)
        continue;
      if (pass)
        put_sum(f, '@', &h);
      for (j = 1; j <= len; j++)
        put_sum(f, pool[i + j], &h);
      put_sum(f, '\n', &h);
    }
  bputc(f, '\n');
  bputc(f, '=');
  bputc(f, '0' + NAM_VERSION);
  bputc(f, ' ');
  put_hex(f, nids);
  bputc(f, ' ');
  put_hex(f, h);
  bputc(f, '\n');
  return h;
}
