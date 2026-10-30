/*
 * Lexer. Reads the source one character at a time and keeps only the
 * current token, so memory use doesn't depend on the file size.
 * Accepts LF, CR and CRLF line endings, and the C95 digraphs.
 */
#include <string.h>
#include "ccish.h"

int tok;
int tok_val;
char tok_text[MAX_STR + 1];
int tok_len;

static Bfile *in;
static int ch;              /* current character, or EOF */
static int at_line_start;   /* only blanks seen so far on this line */

/* Advance to the next character. The line count goes up when we move
 * past a newline, not when we first see one, so an error found at the
 * end of a line reports that line. */
static void getch(void)
{
  if (ch == '\n') {
    src_line++;
    at_line_start = 1;
  }
  ch = bgetc(in);
  if (ch == '\r') {
    int c2 = bgetc(in);
    if (c2 != '\n' && c2 != EOF)
      bungetc(in, c2);
    ch = '\n';
  }
}

/* Start reading source from f, and read the first token. */
void lex_open(Bfile *f)
{
  in = f;
  src_line = 1;
  at_line_start = 1;
  ch = 0;
  getch();
  next();
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
    } else if (ch == '/') {
      int c2 = bgetc(in);
      if (c2 == '/') {
        while (ch != '\n' && ch != EOF)
          getch();
      } else if (c2 == '*') {
        int line = src_line;
        getch();
        for (;;) {
          if (ch == EOF) {
            src_line = line;
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
        if (c2 != EOF)
          bungetc(in, c2);
        return;
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
 * tok_val. It must fit in 16 bits. */
static void lex_number(void)
{
  long v = 0;
  int base = 10;
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
  if (is_alpha(ch) || is_digit(ch) || ch == '.')
    error(11 /* "number suffixes and floating point are not supported" */,
          NULL);
  tok = T_NUM;
  tok_val = (int)v;
}

/* Read a string literal, and any that follow it, into tok_text and
 * tok_len, with escapes decoded. */
static void lex_string(void)
{
  tok_len = 0;
  /* Adjacent string literals are joined, as in C. */
  while (ch == '"') {
    getch();
    while (ch != '"') {
      int c = literal_char();
      if (tok_len >= MAX_STR)
        error(12 /* "string literal too long" */, NULL);
      tok_text[tok_len++] = (char)c;
    }
    getch();
    skip_blank();
  }
  tok_text[tok_len] = 0;
  tok = T_STR;
}

static const char *const keywords[] = {
  /* supported: in the order of T_INT ... T_ENUM */
  "int", "void", "return", "if", "else", "while", "do", "for", "break",
  "continue", "char", "sizeof", "static", "const", "volatile", "struct",
  "switch", "case", "default", "typedef", "enum",
  /* the rest of C89's keywords: recognized so the error is clear */
  "auto", "double", "extern",
  "float", "goto", "long", "register", "short", "signed",
  "union", "unsigned",
  NULL
};

/* Read an identifier or keyword into tok_text, and set tok to say
 * which it is. */
static void lex_word(void)
{
  int n = 0, i;
  while (is_alpha(ch) || is_digit(ch)) {
    if (n < MAX_NAME)
      tok_text[n++] = (char)ch;
    getch();
  }
  tok_text[n] = 0;
  tok = T_ID;
  for (i = 0; keywords[i]; i++)
    if (!strcmp(keywords[i], tok_text)) {
      tok = i < T_KEYWORD - T_INT ? T_INT + i : T_KEYWORD;
      break;
    }
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

/* Read the next token into tok (and tok_val, tok_text, tok_len),
 * skipping blanks and comments. */
void next(void)
{
  int c;
  skip_blank();
  if (ch == EOF) {
    tok = T_EOF;
    return;
  }
  if (ch == '#' && at_line_start)
    error(13 /* "preprocessor directives are not supported yet" */, NULL);
  at_line_start = 0;
  if (is_digit(ch)) {
    lex_number();
    return;
  }
  if (is_alpha(ch)) {
    lex_word();
    return;
  }
  if (ch == '"') {
    lex_string();
    return;
  }
  if (ch == '\'') {
    getch();
    if (ch == '\'')
      error(14 /* "empty character constant" */, NULL);
    tok_val = literal_char();
    if (ch != '\'')
      error(15 /* "multi-character constants are not supported" */,
            NULL);
    getch();
    tok = T_NUM;
    return;
  }
  c = ch;
  getch();
  switch (c) {
  case '=': tok = accept('=') ? T_EQ : '='; return;
  case '!': tok = accept('=') ? T_NE : '!'; return;
  case '*': tok = accept('=') ? T_MULEQ : '*'; return;
  case '/': tok = accept('=') ? T_DIVEQ : '/'; return;
  case '^': tok = accept('=') ? T_XOREQ : '^'; return;
  case '+': tok = accept('+') ? T_INC : accept('=') ? T_ADDEQ : '+'; return;
  case '&': tok = accept('&') ? T_ANDAND : accept('=') ? T_ANDEQ : '&'; return;
  case '|': tok = accept('|') ? T_OROR : accept('=') ? T_OREQ : '|'; return;
  case '-':
    tok = accept('-') ? T_DEC : accept('=') ? T_SUBEQ
        : accept('>') ? T_ARROW : '-';
    return;
  case '<':
    if (accept('<'))
      tok = accept('=') ? T_SHLEQ : T_SHL;
    else if (accept('='))
      tok = T_LE;
    else if (accept('%'))
      tok = '{';          /* digraph <% */
    else if (accept(':'))
      tok = '[';          /* digraph <: */
    else
      tok = '<';
    return;
  case '>':
    if (accept('>'))
      tok = accept('=') ? T_SHREQ : T_SHR;
    else
      tok = accept('=') ? T_GE : '>';
    return;
  case '%':
    if (accept('>'))
      tok = '}';          /* digraph %> */
    else if (accept(':'))
      error(13 /* "preprocessor directives are not supported yet" */,
            NULL);
    else
      tok = accept('=') ? T_MODEQ : '%';
    return;
  case ':':
    tok = accept('>') ? ']' : ':';  /* digraph :> */
    return;
  case '.':
    if (accept('.')) {
      if (!accept('.'))
        error(16 /* "unexpected '..'" */, NULL);
      tok = T_ELLIPSIS;
    } else {
      tok = '.';
    }
    return;
  case '(': case ')': case '{': case '}': case '[': case ']':
  case ',': case ';': case '?': case '~':
    tok = c;
    return;
  }
  error(17 /* "unexpected character in source" */, NULL);
}

/* Is the token after the current one a ']' (or the digraph :>) on the
 * same line? Only spaces and tabs are skipped, so that the line number
 * stays right for messages about the current token; anything else
 * answers no. The parser uses this to see whether an index is simple. */
int next_is_rbracket(void)
{
  int c2;
  while (ch == ' ' || ch == '\t')
    getch();
  if (ch == ']')
    return 1;
  if (ch != ':')
    return 0;
  c2 = bgetc(in);
  if (c2 != EOF)
    bungetc(in, c2);
  return c2 == '>';
}

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
  if (tok == T_ID || (tok >= T_INT && tok <= T_KEYWORD)) {
    strcpy(buf, "'");
    strcat(buf, tok_text);
    strcat(buf, "'");
    return buf;
  }
  if (tok >= T_EQ) {
    strcpy(buf, "'");
    strcat(buf, ops[tok - T_EQ]);
    strcat(buf, "'");
    return buf;
  }
  buf[0] = '\'';
  buf[1] = (char)tok;
  buf[2] = '\'';
  buf[3] = 0;
  return buf;
}
