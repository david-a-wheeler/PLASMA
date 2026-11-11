// cccomp's token reader (approach.md, "ccpp design"): reads the token
// stream ccpp wrote, NAME.i (../CCPP.md), a token at a time, keeping
// only the current one, and counts its lines for messages. An
// identifier is an id there, and the parser uses the id; a spelling is
// looked up in the name file, NAME.nam (nam.c), only to be printed in a
// message.
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diag.src.h"
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

static Bfile in;            // NAME.i
#ifdef CCISH_DIAG
static Bfile nam_in;        // lent to nam.c: NAME.i may still be open
#endif
static const char *base_name;
static int at_end;          // the end code has been read
static char source[MAX_PATH];   // tok_file's name, for messages
int tok_file = 1;           // the file the token is in (the name file's
                            // number), for #pragma plasma import

// The token stream is damaged: stop.
static void damaged(void)
{
  fatal(231 /* "token file is damaged:" */, stream_name(base_name, SUFFIX_I));
}

// The token stream's next byte, which must be there.
static int get_byte(void)
{
  int c = bgetc(&in);
  if (c == EOF)
    damaged();
  return c;
}

// The token stream's next word.
static int get_word(void)
{
  int lo = get_byte();
  return lo | (get_byte() << 8);
}

// Look up the name of the current token's file, for a message
// (diag.c's src_name_hook), opening and checking the name file: only
// then, so that a correct compile never reads it (CCPP.md, "The
// stamp"). An
// error is reported after the other files are closed, so this needs no
// more I/O buffers.
static void name_source(void)
{
#ifndef CCISH_DIAG
  nam_bf = &in;             // closed by now: an error closes it first
#endif
  nam_open(base_name);
  strcpy(source, nam_file(tok_file));
  nam_close();
}

// Open the token stream of base, check its header, and read the first
// token. Messages give the token's file and line from now on.
void tok_open(const char *base)
{
  base_name = base;
  at_end = 0;
  if (!bopen(&in, stream_name(base, SUFFIX_I), 0))
    fatal_d(171 /* "cannot open" */, stream_name(base, SUFFIX_I));
  if (bgetc(&in) != 'C' || bgetc(&in) != 'T' || bgetc(&in) != 'K'
      || bgetc(&in) != TOK_VERSION)
    fatal(230 /* "not a token file from ccpp, or the wrong version:" */,
          stream_name(base, SUFFIX_I));
#ifdef CCISH_DIAG
  nam_bf = &nam_in;         // names are spelled before an error stops
  nam_base(base);           // for a message's details (nam_name())
#endif
  tok_file = 1;
  src_name = source;
  src_name_hook = name_source;
  src_line = 1;
  next();
}

// Close the token stream (and the name file, after an error).
void tok_close(void)
{
  bclose(&in);
  nam_close();
}

// Advance to the next token.
void next(void)
{
  int c, n, i;
  if (at_end) {
    tok = TC_END;
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
    nam_ids = n;        // the name file's stamp, for the IR file;
                        // ccgen checks it, and the ids, against
                        // the name file
    nam_sum = i;
    at_end = 1;
    tok = TC_END;
    return;
  case TC_ID:
  case TC_KEYWORD:
  case TC_TAG:
    // A struct's or union's tag, and only that, has its spelling's
    // hash.
    if ((c == TC_TAG)
        != (TC_IS_STRUCT_UNION(tok) && c != TC_KEYWORD))
      damaged();
    tok = c == TC_KEYWORD ? TC_KEYWORD : TC_ID;
    tok_val = get_word();
    if (tok_val < 1)
      damaged();
    if (c == TC_TAG) {
      tok_hsum = get_word();
      tok_hmul = get_word();
    }
    return;
  case TC_NUM:
  case TC_UNUM:
    tok = TC_NUM;
    tok_unsigned = c == TC_UNUM;
    tok_val = get_word();
    return;
  case TC_STR:
    tok = TC_STR;
    tok_len = get_byte();
    for (i = 0; i < tok_len; i++)
      tok_text[i] = (char)get_byte();
    tok_text[tok_len] = 0;
    return;
  }
  // A one-character token, a keyword, or an operator is its code. A
  // one-character code is checked only as printable: one ccpp never
  // writes (a letter, '#', ...) is an unexpected token to the parser,
  // so a damaged file is still reported, as a syntax error.
  if ((c > ' ' && c <= '~')
      || (c >= TC_KEYWORDS && c < TC_KEYWORDS + NKEYWORDS)
      || (c >= TC_OPS && c < TC_OPS + NOPS))
    tok = c;
  else
    damaged();
}

// Is the token after the current one a ']' on the same line? The
// parser uses this to see whether an index is simple.
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

// Is the token after the current one a ':'? The parser uses this to
// tell a label (an identifier followed by ':') from an expression.
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

// The spellings of the supported keywords and the multi-character
// operators, for messages, are in the message file, as messages' texts
// are, not in cccomp (approach.md, "C2 design"): messages 262-301 are
// the keywords, in their codes' order (token.h), and messages 352-383
// the operators, in theirs, each block with a "(spare)" text at each UNUSED code.
#define MSG_KEYWORDS 262
#define MSG_OPS      352
// The ranges above, which ccish.msg must match (tests/spec/msgs.sh
// checks the comment against it); and tok_desc()'s order: the
// one-character tokens below the keywords, and those below the
// operators.
_Static_assert(MSG_KEYWORDS + NKEYWORDS - 1 == 301,
               "the keywords' messages aren't 262-301: fix the comment"
               " and ccish.msg");
_Static_assert(MSG_OPS + NOPS - 1 == 383,
               "the operators' messages aren't 352-383: fix the comment"
               " and ccish.msg");
_Static_assert(TC_KEYWORDS > '~' && TC_KEYWORDS + NKEYWORDS <= TC_OPS,
               "tok_desc() needs the one-character tokens below the"
               " keywords, and the keywords below the operators");

#ifdef CCISH_DIAG
// Describe the current token for an error message, e.g. "'while'" or
// "a number". The result is in a static buffer. Without the message
// file, a keyword or operator is its message's number.
const char *tok_desc(void)
{
  static char buf[MAX_NAME + 16];
  int n;
  switch (tok) {
  case TC_END: return "end of file";
  case TC_NUM: return "a number";
  case TC_STR: return "a string";
  }
  strcpy(buf, "'");
  if (tok == TC_ID || tok == TC_KEYWORD) {
    strcat(buf, nam_name(tok_val));
  } else if (tok >= TC_KEYWORDS) {
    n = tok >= TC_OPS ? MSG_OPS + (tok - TC_OPS)
                      : MSG_KEYWORDS + (tok - TC_KEYWORDS);
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
