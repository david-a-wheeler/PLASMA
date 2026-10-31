/*
 * ccpp's lexer (approach.md, "ccpp design"). Reads the source a line at
 * a time into a fixed buffer, and splits it into tokens: their codes in
 * the token stream (../CCPP.md), with numbers converted, escapes
 * decoded, and identifiers given their ids. Accepts LF, CR and CRLF
 * line endings, and the C95 digraphs.
 */
#include <string.h>
#include "ccish.h"
#include "token.h"
#include "ppname.h"
#include "pplex.h"

int pt_val;
unsigned char pt_text[MAX_STR + 1];
int pt_len;
int pt_first;

static Bfile *in;
static unsigned char line[MAX_LINE + 1];  /* the current line, no newline */
static int line_len;
static int line_nl;         /* it ended with a newline (not the file's end) */
static int pos;             /* the next character in line[] */
static int ch;              /* current character, '\n' at the line's end,
                             * or EOF */
static int at_line_start;   /* no token yet on this line */
static int pend_nl;         /* newlines passed, not yet returned as TC_NL */

/* Read the next line into line[]; 0 at the end of the file. */
static int next_line(void)
{
  int c;
  line_len = 0;
  line_nl = 0;
  pos = 0;
  for (;;) {
    c = bgetc(in);
    if (c == EOF)
      return line_len > 0;
    if (c == '\r') {
      c = bgetc(in);
      if (c != '\n' && c != EOF)
        bungetc(in, c);
      c = '\n';
    }
    if (c == '\n') {
      line_nl = 1;
      return 1;
    }
    if (line_len >= MAX_LINE)
      error(233 /* "line too long (over 255 characters)" */, NULL);
    line[line_len++] = (unsigned char)c;
  }
}

/* Advance to the next character. The line count goes up when we move
 * past a newline, not when we first see one, so an error found at the
 * end of a line reports that line. */
static void getch(void)
{
  if (ch == '\n') {
    src_line++;
    pend_nl++;
    at_line_start = 1;
    if (!next_line()) {
      ch = EOF;
      return;
    }
  }
  if (pos < line_len)
    ch = line[pos++];
  else
    ch = line_nl ? '\n' : EOF;
}

/* The character after ch, without moving past ch. */
static int peek(void)
{
  if (pos < line_len)
    return line[pos];
  return line_nl ? '\n' : EOF;
}

/* Start reading source from f. */
void pp_lex_open(Bfile *f)
{
  in = f;
  src_line = 1;
  at_line_start = 1;
  pend_nl = 0;
  next_line();
  ch = 0;
  getch();
}

/* Is c a decimal digit? */
static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* Can c start an identifier (a letter or '_')? */
static int is_alpha(int c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
/* Is c a hexadecimal digit? */
static int is_hex(int c)
{
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
/* The value (0-15) of hexadecimal digit c. */
static int hex_val(int c)
{
  if (is_digit(c))
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}

/* Skip blanks and comments. */
static void skip_blank(void)
{
  for (;;) {
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\f' || ch == '\v') {
      getch();
    } else if (ch == '/' && peek() == '/') {
      while (ch != '\n' && ch != EOF)
        getch();
    } else if (ch == '/' && peek() == '*') {
      int start = src_line;
      getch();
      getch();
      for (;;) {
        if (ch == EOF) {
          src_line = start;
          error(4 /* "unterminated comment" */, NULL);
        }
        if (ch == '*') {
          getch();
          if (ch == '/') {
            getch();
            break;
          }
        } else {
          getch();
        }
      }
    } else {
      return;
    }
  }
}

/* Read one character of a character or string literal, handling escapes. */
static int literal_char(void)
{
  int c = ch;
  if (c == '\n' || c == EOF)
    error(5 /* "unterminated literal" */, NULL);
  getch();
  if (c != '\\')
    return c;
  c = ch;
  getch();
  switch (c) {
  case 'n': return 10;
  case 't': return 9;
  case 'r': return 13;
  case 'a': return 7;
  case 'b': return 8;
  case 'f': return 12;
  case 'v': return 11;
  case '\\': case '\'': case '"': case '?': return c;
  case 'x': {
    int v = 0;
    if (!is_hex(ch))
      error(6 /* "\\x used with no following hex digits" */, NULL);
    while (is_hex(ch)) {
      v = (v * 16 + hex_val(ch)) & 0xFFF;
      getch();
    }
    return v & 0xFF;
  }
  default:
    if (c >= '0' && c <= '7') {
      int v = c - '0', n = 1;
      while (n < 3 && ch >= '0' && ch <= '7') {
        v = v * 8 + (ch - '0');
        getch();
        n++;
      }
      return v & 0xFF;
    }
    error(7 /* "unknown escape sequence" */, NULL);
  }
  return 0;
}

/* Read a decimal, octal (leading 0) or hex (0x) integer constant into
 * pt_val, and return its code. It must fit in 16 bits. It is unsigned
 * (TC_UNUM) with a u or U suffix, or if it is hex or octal and doesn't
 * fit in an int, as C says. (A decimal one that doesn't fit would be a
 * long in C; here it is the int with those bits, a documented
 * difference.) */
static int lex_number(void)
{
  long v = 0;
  int base = 10, uns;
  if (ch == '0') {
    getch();
    base = 8;
    if (ch == 'x' || ch == 'X') {
      getch();
      base = 16;
      if (!is_hex(ch))
        error(8 /* "bad hexadecimal number" */, NULL);
    }
  }
  for (;;) {
    int d;
    if (is_digit(ch))
      d = ch - '0';
    else if (base == 16 && is_hex(ch))
      d = hex_val(ch);
    else
      break;
    if (d >= base)
      error(9 /* "bad digit in octal number" */, NULL);
    v = v * base + d;
    if (v > 0xFFFF)
      error(10 /* "number does not fit in 16 bits" */, NULL);
    getch();
  }
  uns = base != 10 && v > 0x7FFF;
  if (ch == 'u' || ch == 'U') {
    getch();
    uns = 1;
  }
  if (is_alpha(ch) || is_digit(ch) || ch == '.')
    error(11 /* "number suffixes other than u, and floating point, are "
                "not supported" */, NULL);
  pt_val = (int)v;
  return uns ? TC_UNUM : TC_NUM;
}

/* Read one string literal into pt_text and pt_len, with escapes
 * decoded. (The output joins adjacent ones.) */
static void lex_string(void)
{
  pt_len = 0;
  getch();
  while (ch != '"') {
    int c = literal_char();
    if (pt_len >= MAX_STR)
      error(12 /* "string literal too long" */, NULL);
    pt_text[pt_len++] = (unsigned char)c;
  }
  getch();
}

static const char *const keywords[] = {
  /* supported: in the order of TC_KEYWORDS (CCPP.md) and T_INT ... */
  "int", "void", "return", "if", "else", "while", "do", "for", "break",
  "continue", "char", "sizeof", "static", "const", "volatile", "struct",
  "switch", "case", "default", "typedef", "enum", "extern", "signed",
  "unsigned",
  /* the rest of C89's keywords: recognized so the error is clear */
  "auto", "double",
  "float", "goto", "long", "register", "short", "union",
  NULL
};

/* Read an identifier or keyword, and return its code: a supported
 * keyword's, or TC_ID or TC_KEYWORD with its id in pt_val. */
static int lex_word(void)
{
  char word[MAX_NAME + 1];
  int n = 0, i;
  while (is_alpha(ch) || is_digit(ch)) {
    if (n < MAX_NAME)
      word[n++] = (char)ch;
    getch();
  }
  word[n] = 0;
  for (i = 0; keywords[i]; i++)
    if (!strcmp(keywords[i], word)) {
      if (i < NKEYWORDS)
        return TC_KEYWORDS + i;
      pt_val = pp_intern(word, n);
      return TC_KEYWORD;
    }
  pt_val = pp_intern(word, n);
  return TC_ID;
}

/* Consume ch if it equals c. */
static int accept(int c)
{
  if (ch == c) {
    getch();
    return 1;
  }
  return 0;
}

/* The next token: its code, with its value in pt_val (a number, or an
 * id) or its bytes in pt_text and pt_len (a string); TC_NL for each
 * newline passed before it, and TC_END at the end of the file. '#' is a
 * token, for the caller to treat as a directive if pt_first (it is the
 * line's first). */
int pp_lex(void)
{
  int c;
  if (!pend_nl)
    skip_blank();
  if (pend_nl) {
    pend_nl--;
    return TC_NL;
  }
  if (ch == EOF)
    return TC_END;
  pt_first = at_line_start;
  at_line_start = 0;
  if (is_digit(ch))
    return lex_number();
  if (is_alpha(ch))
    return lex_word();
  if (ch == '"') {
    lex_string();
    return TC_STR;
  }
  if (ch == '\'') {
    getch();
    if (ch == '\'')
      error(14 /* "empty character constant" */, NULL);
    pt_val = literal_char();
    if (ch != '\'')
      error(15 /* "multi-character constants are not supported" */,
            NULL);
    getch();
    return TC_NUM;
  }
  c = ch;
  getch();
  switch (c) {
  case '=': return accept('=') ? TC_EQ : '=';
  case '!': return accept('=') ? TC_NE : '!';
  case '*': return accept('=') ? TC_MULEQ : '*';
  case '/': return accept('=') ? TC_DIVEQ : '/';
  case '^': return accept('=') ? TC_XOREQ : '^';
  case '+': return accept('+') ? TC_INC : accept('=') ? TC_ADDEQ : '+';
  case '&': return accept('&') ? TC_ANDAND : accept('=') ? TC_ANDEQ : '&';
  case '|': return accept('|') ? TC_OROR : accept('=') ? TC_OREQ : '|';
  case '-':
    return accept('-') ? TC_DEC : accept('=') ? TC_SUBEQ
         : accept('>') ? TC_ARROW : '-';
  case '<':
    if (accept('<'))
      return accept('=') ? TC_SHLEQ : TC_SHL;
    if (accept('='))
      return TC_LE;
    if (accept('%'))
      return '{';         /* digraph <% */
    if (accept(':'))
      return '[';         /* digraph <: */
    return '<';
  case '>':
    if (accept('>'))
      return accept('=') ? TC_SHREQ : TC_SHR;
    return accept('=') ? TC_GE : '>';
  case '%':
    if (accept('>'))
      return '}';         /* digraph %> */
    if (accept(':'))
      return '#';         /* digraph %: */
    return accept('=') ? TC_MODEQ : '%';
  case ':':
    return accept('>') ? ']' : ':';  /* digraph :> */
  case '.':
    if (accept('.')) {
      if (!accept('.'))
        error(16 /* "unexpected '..'" */, NULL);
      return TC_ELLIPSIS;
    }
    return '.';
  case '(': case ')': case '{': case '}': case '[': case ']':
  case ',': case ';': case '?': case '~': case '#':
    return c;
  }
  error(17 /* "unexpected character in source" */, NULL);
  return 0;
}
