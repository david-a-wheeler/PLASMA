// ccpp's lexer (approach.md, "ccpp design"). Reads the source a line at
// a time into a fixed buffer, joining a line that ends in a backslash to
// the next (C's phase 2), and splits it into tokens: their codes in the
// token stream (../CCPP.md), with numbers converted, escapes decoded,
// and identifiers given their ids. Accepts LF, CR and CRLF line endings,
// and the C95 digraphs.
//
// It reads in one of three modes (pp_mode), which the driver (pp.c)
// sets: PM_TEXT, ordinary source, where newlines are blanks; PM_DIRECTIVE,
// a directive's line, which ends at its newline (TC_NL), not passed
// until the mode changes, so that an #include can switch files right at
// the end of its line; and PM_SKIP, a group that #ifdef and the rest
// leave out, where only a '#' that starts a line counts, and the rest is
// passed over without being split into tokens (so "don't" isn't an
// unterminated literal there).
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diag.src.h"
#include "bio.h"
#include "token.h"
#include "ppname.h"
#include "pplex.h"

int pt_val;
unsigned char pt_text[MAX_STR + 1];
int pt_len;
int pt_first;
int pt_space;
int pt_macro;
int pp_mode;

static Bfile *in;           // the file being read, or NULL for a string
static unsigned char line[MAX_LINE + 1];  // the current line, no newline
static int line_len;
static int line_nl;         // it ended with a newline (not the file's end)
static int line_extra;      // lines joined to it by backslashes
static int pos;             // the next character in line[]
static int ch;              // current character, '\n' at the line's end,
                            // or EOF
static int at_line_start;   // no token yet on this line
static int spaced;          // blanks or a comment were skipped

// What an #include saves of the file it is in: where its lexer was, at
// the newline that ends the #include line.
static Bfile *saved_in[MAX_INCLUDE];
static int saved_ch[MAX_INCLUDE];
static int saved_line[MAX_INCLUDE];
static char saved_extra[MAX_INCLUDE];
static int nsaved;

// Read the next line into line[], joining any that end in a backslash;
// 0 at the end of the file.
static int next_line(void)
{
  int c;
  line_len = 0;
  line_nl = 0;
  line_extra = 0;
  pos = 0;
  if (!in)
    return 0;
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
      if (line_len > 0 && line[line_len - 1] == '\\') {
        line_len--;             // a backslash-newline joins the lines
        line_extra++;
        continue;
      }
      line_nl = 1;
      return 1;
    }
    if (line_len >= MAX_LINE)
      error(233 /* "line too long (over 255 characters)" */, NULL);
    line[line_len++] = (unsigned char)c;
  }
}

// Advance to the next character. The line count goes up when we move
// past a newline, not when we first see one, so an error found at the
// end of a line reports that line.
static void getch(void)
{
  if (ch == '\n') {
    src_line += 1 + line_extra;
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

// The character after ch, without moving past ch.
static int peek(void)
{
  if (pos < line_len)
    return line[pos];
  return line_nl ? '\n' : EOF;
}

// Start reading source from f, at its first line.
void pp_lex_open(Bfile *f)
{
  in = f;
  src_line = 1;
  at_line_start = 1;
  next_line();
  ch = 0;
  getch();
}

// Read the line s (from the command line: -D, -U) as a directive's, up
// to its end.
void pp_lex_text(const char *s)
{
  int n = (int)strlen(s);
  if (n > MAX_LINE)
    error(233 /* "line too long (over 255 characters)" */, NULL);
  in = NULL;
  memcpy(line, s, n);
  line_len = n;
  line_nl = 1;
  line_extra = 0;
  pos = 0;
  ch = 0;
  getch();
  pp_mode = PM_DIRECTIVE;
}

// At the end of an #include line: keep the place in this file, and start
// reading f.
void pp_lex_push(Bfile *f)
{
  saved_in[nsaved] = in;
  saved_ch[nsaved] = ch;
  saved_line[nsaved] = src_line;
  saved_extra[nsaved] = (char)line_extra;
  nsaved++;
  pp_lex_open(f);
}

// At the end of an included file: go back to the end of the #include
// line that included it.
void pp_lex_pop(void)
{
  nsaved--;
  in = saved_in[nsaved];
  ch = saved_ch[nsaved];
  src_line = saved_line[nsaved];
  line_extra = saved_extra[nsaved];
  line_len = pos = 0;
  line_nl = 1;
  at_line_start = 0;
}

// Is c a decimal digit?
static int is_digit(int c) { return c >= '0' && c <= '9'; }

// Can c start an identifier (a letter or '_')?
static int is_alpha(int c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
// Is c a hexadecimal digit?
static int is_hex(int c)
{
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
// The value (0-15) of hexadecimal digit c.
static int hex_val(int c)
{
  if (is_digit(c))
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return c - 'A' + 10;
}

// Skip blanks and comments: in a directive's line, not its newline.
static void skip_blank(void)
{
  for (;;) {
    if (ch == '\n' && pp_mode == PM_DIRECTIVE) {
      return;
    } else if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\f'
               || ch == '\v') {
      getch();
      spaced = 1;
    } else if (ch == '/' && peek() == '/') {
      while (ch != '\n' && ch != EOF)
        getch();
      spaced = 1;
    } else if (ch == '/' && peek() == '*') {
      int start = src_line;
      spaced = 1;
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

// In a group being left out: pass over the rest of the line, up to its
// newline or a comment, with no errors. A quoted string or character
// ends at its closing quote or the line's end.
static void skip_rest(void)
{
  int q;
  while (ch != '\n' && ch != EOF && !(ch == '/' && peek() == '*')) {
    if (ch == '"' || ch == '\'') {
      q = ch;
      getch();
      while (ch != q && ch != '\n' && ch != EOF) {
        if (ch == '\\')
          getch();
        if (ch != '\n' && ch != EOF)
          getch();
      }
      if (ch == q)
        getch();
    } else {
      getch();
    }
  }
}

// In a directive's line: skip blanks, and read an identifier's
// characters as they are (a directive's name: "if" is not the keyword
// here) into buf (size bytes); return their number, 0 if there is none.
int pp_lex_word(char *buf, int size)
{
  int n = 0;
  skip_blank();
  while (is_alpha(ch) || is_digit(ch)) {
    if (n < size - 1)
      buf[n++] = (char)ch;
    getch();
  }
  buf[n] = 0;
  return n;
}

// In an #include line: skip blanks, and read a file's name, "name" or
// <name>, into buf (size bytes); return its length, or -1 if there is
// none. *angle says which form it was.
int pp_lex_header(char *buf, int size, int *angle)
{
  int n = 0, close;
  skip_blank();
  if (ch != '"' && ch != '<')
    return -1;
  *angle = ch == '<';
  close = *angle ? '>' : '"';
  getch();
  while (ch != close) {
    if (ch == '\n' || ch == EOF || n >= size - 1)
      return -1;
    buf[n++] = (char)ch;
    getch();
  }
  getch();
  buf[n] = 0;
  return n;
}

// In a directive's line: the rest of it, as it is, after any blanks
// (#error's text). The line is then at its end.
const char *pp_lex_rest(void)
{
  int start;
  skip_blank();
  if (ch == '\n' || ch == EOF)
    return "";
  start = pos - 1;
  line[line_len] = 0;
  pos = line_len;
  ch = line_nl ? '\n' : EOF;
  return (const char *)line + start;
}

// #line n: make the next line number n.
void pp_lex_set_line(int n)
{
  src_line = n - 1 - line_extra;
}

// The character right after the last token: '(' just after a macro's
// name, with no blank, makes it function-like.
int pp_lex_next_char(void)
{
  return ch;
}

// Read one character of a character or string literal, handling escapes.
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

// Read a decimal, octal (leading 0) or hex (0x) integer constant into
// pt_val, and return its code. It must fit in 16 bits. It is unsigned
// (TC_UNUM) with a u or U suffix, or if it is hex or octal and doesn't
// fit in an int, as C says. (A decimal one that doesn't fit would be a
// long in C; here it is the int with those bits, a documented
// difference.)
static int lex_number(void)
{
  unsigned v = 0;
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
    // Checked before, so that a 16-bit unsigned can't overflow.
    if (v > (0xFFFFu - (unsigned)d) / (unsigned)base)
      error(10 /* "number does not fit in 16 bits" */, NULL);
    v = v * (unsigned)base + (unsigned)d;
    getch();
  }
  uns = base != 10 && v > 0x7FFFu;
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

// Read one string literal into pt_text and pt_len, with escapes
// decoded. (The output joins adjacent ones.)
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

// The spelling of an UNUSED keyword or operator code (token.h): one
// that no word or operator is, shared, as one string.
static const char spare[] = " ";

// The C keywords: the supported ones first, in the order of their
// codes.
static const char *const keywords[] = {
  // supported: in the order of TC_INT ... (token.h, CCPP.md) and T_INT
  // ..., with spare at each UNUSED code
  "int", "char", "signed", "unsigned", "const", "volatile", spare, spare,
  "struct", "union", "enum", spare, "void", spare, spare, spare,
  "static", "extern", spare, spare, "typedef", "constexpr", "case",
  "default",
  "return", "if", "else", "while", "do", "for", "break", "continue",
  "sizeof", "switch", "goto", "_Static_assert", spare, spare, spare, spare,
  // the rest of C89's keywords: recognized so the error is clear
  "auto", "double",
  "float", "long", "register",
  NULL
};
// pp_word() takes the first NKEYWORDS as the supported ones, the rest
// as the 5 unsupported, then the NULL.
_Static_assert(sizeof(keywords) / sizeof(keywords[0]) == NKEYWORDS + 5 + 1,
               "keywords[] doesn't have NKEYWORDS supported keywords");

// The code of the word (an identifier or keyword) of n characters at
// word, NUL-terminated: a supported keyword's, or TC_ID or TC_KEYWORD
// with its id in pt_val (and in pt_macro whether it names a macro).
// `short` is a synonym for `int`: ccish has one word type, and the
// standard C distinction between the two is not worth implementing in
// the memory ccish runs in, so the word gets int's code, and the
// compiler never sees a short.
int pp_word(const char *word, int n)
{
  int i;
  if (!strcmp(word, "short"))
    return TC_INT;
  for (i = 0; keywords[i]; i++)
    if (!strcmp(keywords[i], word)) {
      if (i < NKEYWORDS)
        return TC_KEYWORDS + i;
      pt_val = pp_intern(word, n);
      pt_macro = pp_macro_flag;
      return TC_KEYWORD;
    }
  pt_val = pp_intern(word, n);
  pt_macro = pp_macro_flag;
  return TC_ID;
}

// Read an identifier or keyword, and return its code (pp_word()).
static int lex_word(void)
{
  char word[MAX_NAME + 1];
  int n = 0;
  while (is_alpha(ch) || is_digit(ch)) {
    if (n < MAX_NAME)
      word[n++] = (char)ch;
    getch();
  }
  word[n] = 0;
  return pp_word(word, n);
}

// The multi-character operators' spellings, in the order of their codes
// (TC_OPS ...), with spare at each UNUSED code.
static const char *const ops[] = {
  "+=", "-=", "*=", "&=", "|=", "^=", spare, spare,
  "/=", "%=", "<<=", ">>=", spare, spare, spare, spare,
  "++", "--", "&&", "||", "==", "!=", "<=", ">=",
  "<<", ">>", "->", "...", spare, spare, spare, spare
};
_Static_assert(sizeof(ops) / sizeof(ops[0]) == NOPS,
               "ops[] doesn't have NOPS operators");

// The one-character tokens.
static const char singles[] = "!%&()*+,-./:;<=>?[]^{|}~";

// The code of the operator spelled s, or -1 if it isn't one.
int pp_op(const char *s)
{
  int i;
  if (s[0] && !s[1] && strchr(singles, s[0]))
    return s[0];
  for (i = 0; i < NOPS; i++)
    if (!strcmp(ops[i], s))
      return TC_OPS + i;
  if (!strcmp(s, "#"))
    return '#';
  if (!strcmp(s, "##"))
    return TC_PASTE;
  return -1;
}

// Add character c to the spelling being made in buf (size bytes, at
// *n): 0 if there is no room.
static int spell_c(char *buf, int size, int *n, int c)
{
  if (*n >= size - 1)
    return 0;
  buf[(*n)++] = (char)c;
  return 1;
}

// Spell the token code (with its value val, or for a string its n
// bytes at s) as C source, into buf (size bytes, with a NUL), for # and
// ##: a number in decimal (so 0x10 becomes 16, and 'A' 65), a string
// with its quotes, and \n, \t, \r, \" and \\ escaped, any other byte
// outside ' ' to '~' in octal. Return its length, or -1 if it doesn't
// fit.
int pp_spell_token(int code, int val, const unsigned char *s, int n,
                   char *buf, int size)
{
  const char *w = NULL;
  char num[8];
  int len = 0, i, c, k;
  if (code == TC_ID || code == TC_KEYWORD) {
    w = pp_id_spelling(val);
  } else if (code >= TC_KEYWORDS && code < TC_KEYWORDS + NKEYWORDS) {
    w = keywords[code - TC_KEYWORDS];
  } else if (code >= TC_OPS && code < TC_OPS + NOPS) {
    w = ops[code - TC_OPS];
  } else if (code == TC_PASTE) {
    w = "##";
  } else if (code == TC_NUM || code == TC_UNUM) {
    k = sizeof(num) - 1;
    num[k] = 0;
    val &= 0xFFFF;
    do {
      num[--k] = (char)('0' + (unsigned)val % 10);
      val = (int)((unsigned)val / 10);
    } while (val);
    if (code == TC_UNUM)
      strcat(num + k, "u");
    w = num + k;
  } else if (code == TC_STR) {
    if (!spell_c(buf, size, &len, '"'))
      return -1;
    for (i = 0; i < n; i++) {
      c = s[i];
      k = c == '\n' ? 'n' : c == '\t' ? 't' : c == '\r' ? 'r'
          : c == '"' || c == '\\' ? c : 0;
      if (k) {
        if (!spell_c(buf, size, &len, '\\') || !spell_c(buf, size, &len, k))
          return -1;
      } else if (c >= ' ' && c <= '~') {
        if (!spell_c(buf, size, &len, c))
          return -1;
      } else if (!spell_c(buf, size, &len, '\\')
                 || !spell_c(buf, size, &len, '0' + ((c >> 6) & 7))
                 || !spell_c(buf, size, &len, '0' + ((c >> 3) & 7))
                 || !spell_c(buf, size, &len, '0' + (c & 7))) {
        return -1;
      }
    }
    if (!spell_c(buf, size, &len, '"'))
      return -1;
    buf[len] = 0;
    return len;
  } else {
    num[0] = (char)code;        // a one-character token
    num[1] = 0;
    w = num;
  }
  while (*w)
    if (!spell_c(buf, size, &len, *w++))
      return -1;
  buf[len] = 0;
  return len;
}

// Consume ch if it equals c.
static int accept(int c)
{
  if (ch == c) {
    getch();
    return 1;
  }
  return 0;
}

static int lex_op(void);

// The next token: its code, with its value in pt_val (a number, or an
// id) or its bytes in pt_text and pt_len (a string); TC_NL at the end
// of a directive's line, and TC_END at the end of the file. '#' is a
// token, for the caller to treat as a directive if pt_first (it is the
// line's first). In a group being left out (PM_SKIP), only such a '#',
// and TC_END, are returned.
int pp_lex(void)
{
  spaced = 0;
  for (;;) {
    skip_blank();
    if (pp_mode == PM_DIRECTIVE && (ch == '\n' || ch == EOF))
      return TC_NL;     // the end of a directive's line
    if (ch == EOF)
      return TC_END;
    if (pp_mode != PM_SKIP)
      break;
    if (at_line_start
        && (ch == '#' || (ch == '%' && peek() == ':'))) {
      if (ch == '%')
        getch();
      getch();
      at_line_start = 0;
      pt_first = 1;
      return '#';
    }
    at_line_start = 0;
    skip_rest();
  }
  pt_first = at_line_start;
  pt_space = spaced || pt_first;
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
  return lex_op();
}

// Read an operator or punctuator, and return its code. (Apart from
// pp_lex(), so that neither has too many returns for ccish.)
static int lex_op(void)
{
  int c = ch;
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
      return '{';         // digraph <%
    if (accept(':'))
      return '[';         // digraph <:
    return '<';
  case '>':
    if (accept('>'))
      return accept('=') ? TC_SHREQ : TC_SHR;
    return accept('=') ? TC_GE : '>';
  case '%':
    if (accept('>'))
      return '}';         // digraph %>
    if (accept(':')) {
      if (ch == '%' && peek() == ':') {
        getch();
        getch();
        return TC_PASTE;  // digraph %:%:
      }
      return '#';         // digraph %:
    }
    return accept('=') ? TC_MODEQ : '%';
  case ':':
    return accept('>') ? ']' : ':';  // digraph :>
  case '.':
    if (accept('.')) {
      if (!accept('.'))
        error(16 /* "unexpected '..'" */, NULL);
      return TC_ELLIPSIS;
    }
    return '.';
  case '#':
    return accept('#') ? TC_PASTE : '#';
  case '(': case ')': case '{': case '}': case '[': case ']':
  case ',': case ';': case '?': case '~':
    return c;
  }
  error(17 /* "unexpected character in source" */, NULL);
  return 0;
}
