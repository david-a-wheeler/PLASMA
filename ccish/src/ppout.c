/*
 * ccpp's output (../CCPP.md): the token stream, NAME.i, a token at a
 * time; adjacent strings are joined here (C's phase 6), so a string is
 * held until the next token shows it is complete. Lines are brought up
 * to date just before each token is written: next-line codes for up to 3
 * lines, a line's number for more (after a skipped group, say), and a
 * file's number and line on a change of file. At the end, the name file,
 * NAME.nam, and the stamp both files carry.
 *
 * With -E (pp_opt_text, in the full build only: CCISH_TEXT), the tokens
 * are written as text to the standard output instead, as gcc -E writes
 * them: a space between tokens, newlines to keep their lines, and a
 * line marker, # line "file", for a change of file or a long jump. All
 * of it is in the CCISH_TEXT parts below, so that it can be put in the
 * small build too by defining CCISH_TEXT there.
 */
#include "stdio.h"
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "bio.h"
#include "token.h"
#include "ppname.h"
#include "pplex.h"
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

#ifdef CCISH_TEXT
int pp_opt_text;
static int text_col;                /* tokens written on the line */

/* -E: bring the text's line up to the current one: newlines for up to
 * 8 lines, else a line marker. */
static void text_sync(void)
{
  char path[MAX_PATH];
  if (pp_cur_file == out_file && src_line >= out_line
      && src_line - out_line <= 8) {
    for (; out_line < src_line; out_line++) {
      fputs("\n", stdout);
      text_col = 0;
    }
    return;
  }
  if (text_col)
    fputs("\n", stdout);
  pp_file_path(pp_cur_file, path);
  fputs("# ", stdout);
  fputs(num_text(src_line), stdout);
  fputs(" \"", stdout);
  fputs(path, stdout);
  fputs("\"\n", stdout);
  text_col = 0;
  out_file = pp_cur_file;
  out_line = src_line;
}

/* -E: write a token as source, after a space if it isn't the line's
 * first. */
static void put_text(int code, int val, const unsigned char *s, int n)
{
  static char buf[4 * MAX_STR + 3];
  if (text_col++)
    fputs(" ", stdout);
  pp_spell_token(code, val, s, n, buf, sizeof(buf));
  fputs(buf, stdout);
}
#endif

/* Create the token stream for output name base, and write its header. */
void pp_out_open(const char *base)
{
  base_name = base;
  str_held = 0;
  last = 0;
  out_file = 1;
  out_line = 1;
#ifdef CCISH_TEXT
  text_col = 0;
  if (pp_opt_text)
    return;
#endif
  if (!bopen(&out, stream_name(base, SUFFIX_I), 1))
    fatal(176 /* "cannot create" */, stream_name(base, SUFFIX_I));
  bwrite(&out, (const unsigned char *)"CTK", 3);
  bputc(&out, TOK_VERSION);
}

/* Bring the stream's file and line up to the current ones. */
static void sync(void)
{
#ifdef CCISH_TEXT
  if (pp_opt_text) {
    text_sync();
    return;
  }
#endif
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
  str_held = 0;
#ifdef CCISH_TEXT
  if (pp_opt_text) {
    put_text(TC_STR, 0, str, str_len);
    return;
  }
#endif
  bputc(&out, TC_STR);
  bputc(&out, str_len);
  bwrite(&out, str, str_len);
}

/* Write a token: code, with its value val (a number or an id) or, for a
 * string, its n bytes at s. A string is joined to one held before it;
 * the line is brought up to date before the held string is written, so
 * that a string's line is that of the token after it. An identifier
 * after "struct" or "union" is a tag, with its spelling's hash
 * (CCPP.md, TC_TAG). */
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
#ifdef CCISH_TEXT
  if (pp_opt_text) {
    put_text(code, val, s, n);
    return;
  }
#endif
  if (code == TC_ID && (last == TC_STRUCT || last == TC_UNION)) {
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
  if (TC_IS_WORDVAL(code))
    bputw(&out, val);
}

/* End the token stream: write NAME.nam, then the end code and the
 * stamp (the id count and NAME.nam's checksum), and close both. */
void pp_out_finish(void)
{
  int sum;
  sync();
  flush_str();
#ifdef CCISH_TEXT
  if (pp_opt_text) {
    if (text_col)
      fputs("\n", stdout);
    return;
  }
#endif
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
