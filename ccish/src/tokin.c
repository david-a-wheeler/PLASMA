/*
 * cccomp's token reader (approach.md, "ccpp design"): reads the token
 * stream ccpp wrote, NAME.i (../CCPP.md), a token at a time, keeping
 * only the current one, and counts its lines for messages. An
 * identifier is an id there, and the parser uses the id; a spelling is
 * looked up in the name file, NAME.nam (nam.c), only to be printed in a
 * message.
 */
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diagsrc.h"
#include "bio.h"
#include "token.h"
#include "nam.h"
#include "tokin.h"

int tok;
int tok_val;
int tok_unsigned;
char tok_text[MAX_STR + 1];
int tok_len;
int tok_hsum;
int tok_hmul;

static Bfile in;            /* NAME.i */
static const char *base_name;
static int at_end;          /* the end code has been read */
static char source[MAX_PATH];   /* tok_file's name, for messages */
int tok_file = 1;           /* the file the token is in (the name file's
                             * number), for #pragma plasma import */

/* The token stream is damaged: stop. */
static void damaged(void)
{
  fatal(231 /* "token file is damaged:" */, stream_name(base_name, SUFFIX_I));
}

/* The token stream's next byte, which must be there. */
static int get_byte(void)
{
  int c = bgetc(&in);
  if (c == EOF)
    damaged();
  return c;
}

/* The token stream's next word. */
static int get_word(void)
{
  int lo = get_byte();
  return lo | (get_byte() << 8);
}

/* Look up the name of the current token's file, for a message
 * (diag.c's src_name_hook): only then, so that a correct compile reads
 * the name file just once, at the start, and has it open only then. */
static void name_source(void)
{
  strcpy(source, nam_file(tok_file));
  nam_close();
}

/* Open the token stream and name file of base, check the stream's
 * header and the name file (which is closed again), and read the first
 * token. Messages give the token's file and line from now on. */
void tok_open(const char *base)
{
  base_name = base;
  at_end = 0;
  if (!bopen(&in, stream_name(base, SUFFIX_I), 0))
    fatal(171 /* "cannot open" */, stream_name(base, SUFFIX_I));
  if (bgetc(&in) != 'C' || bgetc(&in) != 'T' || bgetc(&in) != 'K'
      || bgetc(&in) != TOK_VERSION)
    fatal(230 /* "not a token file from ccpp, or the wrong version:" */,
          stream_name(base, SUFFIX_I));
  nam_open(base);
  tok_file = 1;
  src_name = source;
  src_name_hook = name_source;
  src_line = 1;
  next();
}

/* Close the token stream (and the name file, after an error). */
void tok_close(void)
{
  bclose(&in);
  nam_close();
}

/* The one-character tokens the stream may hold. */
static const char singles[] = "!%&()*+,-./:;<=>?[]^{|}~";

/* Advance to the next token. */
void next(void)
{
  int c, n, i;
  if (at_end) {
    tok = T_EOF;
    return;
  }
  for (;;) {
    c = get_byte();
    if (c == TC_NL) {
      src_line++;
    } else if (c == TC_LINE) {
      src_line = get_word();
    } else if (c == TC_FILE) {
      c = get_byte();
      tok_file = c;
      src_line = get_word();
    } else {
      break;
    }
  }
  switch (c) {
  case TC_END:
    n = get_word();
    i = get_word();
    if (bgetc(&in) != EOF)
      damaged();
    if (n != nam_ids || i != nam_sum)
      nam_stale();      /* from another run of ccpp */
    at_end = 1;
    tok = T_EOF;
    return;
  case TC_ID:
  case TC_KEYWORD:
  case TC_TAG:
    /* A struct's or union's tag, and only that, has its spelling's
     * hash. */
    if ((c == TC_TAG)
        != ((tok == T_STRUCT || tok == T_UNION) && c != TC_KEYWORD))
      damaged();
    tok = c == TC_KEYWORD ? T_KEYWORD : T_ID;
    tok_val = get_word();
    if (tok_val < 1 || tok_val > nam_ids)
      damaged();
    if (c == TC_TAG) {
      tok_hsum = get_word();
      tok_hmul = get_word();
    }
    return;
  case TC_NUM:
  case TC_UNUM:
    tok = T_NUM;
    tok_unsigned = c == TC_UNUM;
    tok_val = get_word();
    return;
  case TC_STR:
    tok = T_STR;
    tok_len = get_byte();
    for (i = 0; i < tok_len; i++)
      tok_text[i] = (char)get_byte();
    tok_text[tok_len] = 0;
    return;
  }
  if (c >= TC_KEYWORDS && c < TC_KEYWORDS + NKEYWORDS)
    tok = T_INT + (c - TC_KEYWORDS);
  else if (c >= TC_EQ && c <= TC_ELLIPSIS)
    tok = T_EQ + (c - TC_EQ);
  else if (c >= ' ' && strchr(singles, c))
    tok = c;
  else
    damaged();
}

/* Is the token after the current one a ']' on the same line? The
 * parser uses this to see whether an index is simple. */
int next_is_rbracket(void)
{
  int c;
  if (at_end)
    return 0;
  c = bgetc(&in);
  if (c != EOF)
    bungetc(&in, c);
  return c == ']';
}

/* Is the token after the current one a ':'? The parser uses this to
 * tell a label (an identifier followed by ':') from an expression. */
int next_is_colon(void)
{
  int c;
  if (at_end)
    return 0;
  c = bgetc(&in);
  if (c != EOF)
    bungetc(&in, c);
  return c == ':';
}

/* The spellings of the supported keywords and the multi-character
 * operators, for messages, are in the message file, as messages' texts
 * are, not in cccomp (approach.md, "C2 design"): messages 262-287 are
 * the keywords, in T_INT's order, and messages 288-309 the operators,
 * in T_EQ's. */
#define MSG_KEYWORDS 262
#define MSG_OPS      288

#ifdef CCISH_DIAG
/* Describe the current token for an error message, e.g. "'while'" or
 * "a number". The result is in a static buffer. Without the message
 * file, a keyword or operator is its message's number. */
const char *tok_desc(void)
{
  static char buf[MAX_NAME + 16];
  int n;
  switch (tok) {
  case T_EOF: return "end of file";
  case T_NUM: return "a number";
  case T_STR: return "a string";
  }
  strcpy(buf, "'");
  if (tok == T_ID || tok == T_KEYWORD) {
    strcat(buf, nam_name(tok_val));
  } else if (tok >= T_INT) {
    n = tok >= T_EQ ? MSG_OPS + (tok - T_EQ) : MSG_KEYWORDS + (tok - T_INT);
    if (!msg_text(n, buf + 1, MAX_NAME + 1))
      strcpy(buf + 1, num_text(n));
  } else {
    buf[1] = (char)tok;
    buf[2] = 0;
  }
  strcat(buf, "'");
  return buf;
}
#endif
