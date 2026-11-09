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
    fatal(171 /* "cannot open" */, stream_name(base, SUFFIX_I));
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

// The one-character tokens the stream may hold.
static const char singles[] = "!%&()*+,-./:;<=>?[]^{|}~";

// Advance to the next token.
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
    nam_ids = n;        // the name file's stamp, for the IR file;
                        // ccgen checks it, and the ids, against
                        // the name file
    nam_sum = i;
    at_end = 1;
    tok = T_EOF;
    return;
  case TC_ID:
  case TC_KEYWORD:
  case TC_TAG:
    // A struct's or union's tag, and only that, has its spelling's
    // hash.
    if ((c == TC_TAG)
        != (T_IS_STRUCT_UNION(tok) && c != TC_KEYWORD))
      damaged();
    tok = c == TC_KEYWORD ? T_KEYWORD : T_ID;
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
  // By offset: the keywords and operators are in the same order in
  // both (asserted below next()).
  if (c >= TC_KEYWORDS && c < TC_KEYWORDS + NKEYWORDS)
    tok = T_INT + (c - TC_KEYWORDS);
  else if (c >= TC_OPS && c < TC_OPS + NOPS)
    tok = T_ADDEQ + (c - TC_OPS);
  else if (c >= ' ' && strchr(singles, c))
    tok = c;
  else
    damaged();
}

// next() maps a stream code to a token by its offset in its block
// (T_INT + (c - TC_KEYWORDS), T_ADDEQ + (c - TC_OPS)), so the tokens must
// be in the stream's order (token.h, CCPP.md): each keyword and each
// operator at its code's place. Here, since next() is what relies on
// it. (The keywords' spellings, pplex.c's keywords[], follow the same
// order; strings can't be checked here, but tests/pp's token streams
// do.)
#define SAME_KW(x) _Static_assert(T_##x - T_INT == TC_##x - TC_KEYWORDS, \
                                  "T_" #x " isn't at TC_" #x "'s place")
SAME_KW(INT); SAME_KW(CHAR); SAME_KW(SIGNED); SAME_KW(UNSIGNED);
SAME_KW(VOID); SAME_KW(CONST); SAME_KW(VOLATILE); SAME_KW(STRUCT);
SAME_KW(ENUM); SAME_KW(RETURN); SAME_KW(IF); SAME_KW(ELSE);
SAME_KW(WHILE); SAME_KW(DO); SAME_KW(FOR); SAME_KW(BREAK);
SAME_KW(CONTINUE); SAME_KW(SIZEOF); SAME_KW(STATIC); SAME_KW(EXTERN);
SAME_KW(SWITCH); SAME_KW(CASE); SAME_KW(DEFAULT); SAME_KW(TYPEDEF);
SAME_KW(GOTO); SAME_KW(UNION); SAME_KW(STATIC_ASSERT); SAME_KW(CONSTEXPR);
#undef SAME_KW
#define SAME_OP(x) _Static_assert(T_##x - T_ADDEQ == TC_##x - TC_OPS, \
                                  "T_" #x " isn't at TC_" #x "'s place")
SAME_OP(EQ); SAME_OP(NE); SAME_OP(LE); SAME_OP(GE);
SAME_OP(SHL); SAME_OP(SHR); SAME_OP(ANDAND); SAME_OP(OROR);
SAME_OP(INC); SAME_OP(DEC); SAME_OP(ADDEQ); SAME_OP(SUBEQ);
SAME_OP(MULEQ); SAME_OP(DIVEQ); SAME_OP(MODEQ); SAME_OP(ANDEQ);
SAME_OP(OREQ); SAME_OP(XOREQ); SAME_OP(SHLEQ); SAME_OP(SHREQ);
SAME_OP(ARROW); SAME_OP(ELLIPSIS);
#undef SAME_OP

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
// the keywords, in T_INT's order, and messages 352-383 the operators,
// in T_ADDEQ's, each block with a "(spare)" text at each UNUSED code.
#define MSG_KEYWORDS 262
#define MSG_OPS      352
// The ranges above, which ccish.msg must match (tests/spec/msgs.sh
// checks the comment against it); and tok_desc()'s order: the
// one-character tokens below T_INT, and the keywords below T_ADDEQ.
_Static_assert(MSG_KEYWORDS + NKEYWORDS - 1 == 301,
               "the keywords' messages aren't 262-301: fix the comment"
               " and ccish.msg");
_Static_assert(MSG_OPS + NOPS - 1 == 383,
               "the operators' messages aren't 352-383: fix the comment"
               " and ccish.msg");
_Static_assert(T_INT > '~' && T_INT + NKEYWORDS <= T_ADDEQ,
               "tok_desc() needs the one-character tokens below T_INT, and"
               " the keywords below T_ADDEQ");

#ifdef CCISH_DIAG
// Describe the current token for an error message, e.g. "'while'" or
// "a number". The result is in a static buffer. Without the message
// file, a keyword or operator is its message's number.
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
    n = tok >= T_ADDEQ ? MSG_OPS + (tok - T_ADDEQ)
                       : MSG_KEYWORDS + (tok - T_INT);
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
