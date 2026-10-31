/*
 * cccomp's token reader (approach.md, "ccpp design"): reads the token
 * stream ccpp wrote, NAME.i (../CCPP.md), a token at a time, keeping
 * only the current one, and counts its lines for messages. An
 * identifier is an id there; its spelling is looked up in the name
 * file, NAME.nam, which is read from the start each time (bio can't
 * seek), through stdio as the message file is, so that it needs no
 * buffer of cccomp's own. For now every identifier's spelling is looked up, so that the
 * parser is unchanged; from step 2, only a message's.
 */
#include <string.h>
#include "ccish.h"
#include "token.h"
#include "tokin.h"

int tok;
int tok_val;
int tok_unsigned;
char tok_text[MAX_STR + 1];
int tok_len;

static Bfile in;            /* NAME.i */
static FILE *nam;           /* NAME.nam, while a name is looked up */
static const char *base_name;
static int at_end;          /* the end code has been read */
static int nam_ids;         /* the name file's stamp: its id count */
static int nam_sum;         /* and its checksum */
static char source[MAX_PATH];   /* file 1's name, for messages */

/* The token stream is damaged: stop. */
static void damaged(void)
{
  fatal(231 /* "token file is damaged:" */, stream_name(base_name, SUFFIX_I));
}

/* Close the name file, if it is open. */
static void nam_close(void)
{
  if (nam)
    fclose(nam);
  nam = NULL;
}

/* The name file is damaged, or from another run of ccpp: stop. */
static void stale(void)
{
  nam_close();
  fatal(232 /* "name file is stale or damaged:" */,
        stream_name(base_name, SUFFIX_NAM));
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

/* Open the name file, to read it from the start. */
static void nam_open(void)
{
  nam = fopen(stream_name(base_name, SUFFIX_NAM), "rb");
  if (!nam)
    fatal(171 /* "cannot open" */, stream_name(base_name, SUFFIX_NAM));
}

/* Read the name file's next line into buf (size bytes, with a NUL),
 * without its newline, adding its bytes and newline to checksum *h;
 * return its length. A line that is too long, or the file's end, is
 * damage. */
static int nam_line(char *buf, int size, int *h)
{
  int c, n = 0;
  for (;;) {
    c = getc(nam);
    if (c == EOF || (n >= size - 1 && c != '\n'))
      stale();
    *h = (*h * 31 + c) & 0xFFFF;
    if (c == '\n')
      break;
    buf[n++] = (char)c;
  }
  buf[n] = 0;
  return n;
}

/* Copy entry k of the name file into buf (MAX_PATH bytes): id k if
 * file is 0, or file k, without its '@'. */
static void nam_entry(int file, int k, char *buf)
{
  int n, i = 0, h = 0;
  nam_open();
  for (;;) {
    n = nam_line(buf, MAX_PATH, &h);
    if (n == 0)
      stale();          /* the blank line: no such entry */
    if ((buf[0] == '@') == file && ++i == k)
      break;
  }
  nam_close();
  if (file)
    memmove(buf, buf + 1, n);
  else if (n > MAX_NAME)
    stale();
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

/* Check the name file (CCPP.md, "NAME.nam"), before any name in it is
 * used, and keep its stamp in nam_ids and nam_sum, for the token
 * stream's to match at its end. The names come before the files, a
 * blank line follows them, then a stamp that matches the file, and then
 * nothing. */
static void nam_check(void)
{
  char buf[MAX_PATH];
  int n, h = 0, ids = 0, files = 0, d = 0, before;
  nam_open();
  for (;;) {
    before = h;
    n = nam_line(buf, MAX_PATH, &h);
    if (n == 0)
      break;
    if (buf[0] == '@')
      files++;
    else if (files || n > MAX_NAME)
      stale();          /* a name after the files, or too long */
    else
      ids++;
  }
  h = before;           /* the blank line isn't summed */
  if (nam_line(buf, MAX_PATH, &d) != 12 || buf[0] != '='
      || buf[1] != '0' + NAM_VERSION || buf[2] != ' ' || buf[7] != ' '
      || hex4(buf + 3) != ids || hex4(buf + 8) != h || files == 0
      || getc(nam) != EOF)
    stale();
  nam_close();
  nam_ids = ids;
  nam_sum = h;
}

/* Open the token stream and name file of base, check the stream's
 * header and the name file, and read the first token. Messages give the
 * source's name, file 1 of the name file, from now on. */
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
  nam_check();
  nam_entry(1, 1, source);
  src_name = source;
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
  while ((c = get_byte()) == TC_NL)
    src_line++;
  switch (c) {
  case TC_END:
    n = get_word();
    i = get_word();
    if (bgetc(&in) != EOF)
      damaged();
    if (n != nam_ids || i != nam_sum)
      stale();          /* from another run of ccpp */
    at_end = 1;
    tok = T_EOF;
    return;
  case TC_ID:
  case TC_KEYWORD:
    tok = c == TC_ID ? T_ID : T_KEYWORD;
    n = get_word();
    if (n < 1)
      damaged();
    nam_entry(0, n, tok_text);
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

/* The supported keywords' spellings, for messages: in T_INT's order. */
static const char *const keywords[NKEYWORDS] = {
  "int", "void", "return", "if", "else", "while", "do", "for", "break",
  "continue", "char", "sizeof", "static", "const", "volatile", "struct",
  "switch", "case", "default", "typedef", "enum", "extern", "signed",
  "unsigned"
};

/* Describe the current token for an error message, e.g. "'while'" or
 * "a number". The result is in a static buffer. */
const char *tok_desc(void)
{
  static char buf[MAX_NAME + 16];
  static const char *const ops[] = {
    "==", "!=", "<=", ">=", "<<", ">>", "&&", "||", "++", "--",
    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=",
    "->", "..."
  };
  switch (tok) {
  case T_EOF: return "end of file";
  case T_NUM: return "a number";
  case T_STR: return "a string";
  }
  strcpy(buf, "'");
  if (tok == T_ID || tok == T_KEYWORD)
    strcat(buf, tok_text);
  else if (tok >= T_INT && tok < T_KEYWORD)
    strcat(buf, keywords[tok - T_INT]);
  else if (tok >= T_EQ)
    strcat(buf, ops[tok - T_EQ]);
  else {
    buf[1] = (char)tok;
    buf[2] = 0;
  }
  strcat(buf, "'");
  return buf;
}
