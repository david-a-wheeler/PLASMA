/*
 * ccpp's output (../CCPP.md): the token stream, NAME.i, a token at a
 * time; adjacent strings are joined here (C's phase 6), so a string is
 * held until the next token shows it is complete. Lines are brought up
 * to date just before each token is written: next-line codes for up to 3
 * lines, a line's number for more (after a skipped group, say), and a
 * file's number and line on a change of file. At the end, the name file,
 * NAME.nam, and the stamp both files carry.
 */
#include "ccish.h"
#include "token.h"
#include "ppname.h"
#include "ppout.h"
#include "pp.h"

static Bfile out;
static Bfile nam;
static const char *base_name;
static unsigned char str[MAX_STR];  /* the string being joined */
static int str_len;
static int str_held;                /* a string is in str[] */
static int last;                    /* the last code written, but lines' */
static int out_file;                /* the file and line a reader of the */
static int out_line;                /* stream is at */

/* Create the token stream for output name base, and write its header. */
void pp_out_open(const char *base)
{
  base_name = base;
  str_held = 0;
  last = 0;
  out_file = 1;
  out_line = 1;
  if (!bopen(&out, stream_name(base, SUFFIX_I), 1))
    fatal(176 /* "cannot create" */, stream_name(base, SUFFIX_I));
  bwrite(&out, (const unsigned char *)"CTK", 3);
  bputc(&out, TOK_VERSION);
}

/* Bring the stream's file and line up to the current ones. */
static void sync(void)
{
  if (pp_cur_file != out_file) {
    bputc(&out, TC_FILE);
    bputc(&out, pp_cur_file);
    bputw(&out, src_line);
  } else if (src_line > out_line && src_line - out_line <= 3) {
    while (out_line < src_line) {
      bputc(&out, TC_NL);
      out_line++;
    }
  } else if (src_line != out_line) {
    bputc(&out, TC_LINE);
    bputw(&out, src_line);
  }
  out_file = pp_cur_file;
  out_line = src_line;
}

/* Write the held string, if any. */
static void flush_str(void)
{
  if (!str_held)
    return;
  bputc(&out, TC_STR);
  bputc(&out, str_len);
  bwrite(&out, str, str_len);
  str_held = 0;
}

/* Write a token: code, with its value val (a number or an id) or, for a
 * string, its n bytes at s. A string is joined to one held before it;
 * the line is brought up to date before the held string is written, so
 * that a string's line is that of the token after it. An identifier
 * after "struct" is a tag, with its spelling's hash (CCPP.md, TC_TAG). */
void pp_out(int code, int val, const unsigned char *s, int n)
{
  int i, mul, hash;
  if (code == TC_STR) {
    if (!str_held) {
      str_held = 1;
      str_len = 0;
    }
    if (str_len + n > MAX_STR)
      error(12 /* "string literal too long" */, NULL);
    for (i = 0; i < n; i++)
      str[str_len++] = s[i];
    return;
  }
  sync();
  flush_str();
  if (code == TC_ID && last == TC_STRUCT) {
    hash = pp_name_hash(val, &mul);
    bputc(&out, TC_TAG);
    bputw(&out, val);
    bputw(&out, hash);
    bputw(&out, mul);
    last = TC_TAG;
    return;
  }
  last = code;
  bputc(&out, code);
  if (code == TC_ID || code == TC_NUM || code == TC_UNUM
      || code == TC_KEYWORD)
    bputw(&out, val);
}

/* End the token stream: write NAME.nam, then the end code and the
 * stamp (the id count and NAME.nam's checksum), and close both. */
void pp_out_finish(void)
{
  int sum;
  sync();
  flush_str();
  if (!bopen(&nam, stream_name(base_name, SUFFIX_NAM), 1))
    fatal(176 /* "cannot create" */, stream_name(base_name, SUFFIX_NAM));
  sum = pp_names_write(&nam);
  if (bclose(&nam) != 0)
    fatal(187 /* "error writing" */, stream_name(base_name, SUFFIX_NAM));
  bputc(&out, TC_END);
  bputw(&out, pp_nids());
  bputw(&out, sum);
  if (bclose(&out) != 0)
    fatal(187 /* "error writing" */, stream_name(base_name, SUFFIX_I));
}

/* After an error: close the output files, so that they can be removed. */
void pp_out_abort(void)
{
  bclose(&nam);
  bclose(&out);
}
