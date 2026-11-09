// ccpp's macros (approach.md, "ccpp design"): the table of #defines, and
// their expansion. A macro's body is kept as tokens in the token
// stream's own encoding (../CCPP.md), with ccpp's codes for its
// parameters, # and ## (pplex.h); expanding one replays bytes.
//
// Expansions are read through an input stack: each entry is a text in
// sbuf[] being read (a macro's replacement, or an argument being
// expanded) and the macro it came from, which isn't expanded again
// while it is read (C11 6.10.3.4). When the stack is empty, tokens come
// from the lexer. A function-like macro's arguments are collected into
// sbuf[] too; each one used is first expanded by itself (C11 6.10.3.1),
// read behind a barrier entry that ends it, into obuf[]; then the
// replacement is built in sbuf[], and read in place of the call, with
// what follows it. sbuf[] and obuf[] are used as stacks, and sbuf[]'s
// dead texts are reclaimed whenever one is added. Nothing is allocated.
//
// The table is parallel arrays: macro m has name mac_id[m], mac_np1[m]
// parameters plus 1 (0 for an object-like macro; the last parameter is
// __VA_ARGS__ if mac_va[m]), and its body is mac_len[m] bytes of body[]
// from mac_at[m]; bodies are in the table's order, with no gaps.
#include <stdio.h>
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diag.src.h"
#include "bio.h"
#include "token.h"
#include "fixed.h"
#include "ppname.h"
#include "pplex.h"
#include "ppmac.h"

#define MAX_MACROS   142    // macros defined at once (129, ptop.c,
                            // about 20 of them include guards)
#define MAX_MACRO_BYTES 1284 // their bodies, as tokens (1166, pexpr.c)
#define MAX_EXPAND   16     // macro expansions inside one another (2)
#define MAX_EXP_BYTES 256   // bytes of the expansions and arguments
                            // being read, and of the arguments' own
                            // expansions (two buffers; 15 and 3): room
                            // for macros longer than ccish's
#define MAX_ARG_SLOTS 32    // arguments of the calls being expanded (1)

int pp_from_lexer;

static unsigned char body[MAX_MACRO_BYTES];
static int body_used;
static int mac_id[MAX_MACROS];
static int mac_at[MAX_MACROS];
static int mac_len[MAX_MACROS];
static char mac_np1[MAX_MACROS];
static char mac_va[MAX_MACROS];
static char mac_on[MAX_MACROS];     // being expanded now
static int nmacs;

// The macro being defined: where its body starts, its parameters, and
// whether the last token was a '#'.
static int def_start;
static int def_params[MAX_PP_PARAMS];
static int def_np;
static int def_va;
static int def_hash;

// The input stack: entry k reads sbuf[] from in_at[k] to in_end[k], for
// macro in_mac[k] (or -1); a barrier (in_bar[k]) ends an argument.
static unsigned char sbuf[MAX_EXP_BYTES];
static int stop;                    // sbuf[]'s first free byte
static int sfloor;                  // the arguments in use end here
static int in_at[MAX_EXPAND];
static int in_end[MAX_EXPAND];
static int in_mac[MAX_EXPAND];
static char in_bar[MAX_EXPAND];
static int nin;

// Arguments' expansions, while a replacement is built.
static unsigned char obuf[MAX_EXP_BYTES];
static int otop;

// The calls being expanded: each argument's text in sbuf[] (a_at to
// a_end), and its expansion in obuf[] (e_at to e_end; e_at -1 until
// made).
static int a_at[MAX_ARG_SLOTS];
static int a_end[MAX_ARG_SLOTS];
static int e_at[MAX_ARG_SLOTS];
static int e_end[MAX_ARG_SLOTS];
static int nslots;

// A token read ahead (after a function-like macro's name, to see
// whether a '(' follows), to be read again: its code (-1 for none),
// value, and where it came from. pt_text and pt_len stay its.
static int pend_code = -1;
static int pend_val;
static char pend_first;
static char pend_lex;
static char pend_macro;

#ifdef CCISH_STATS
// The most each table held (CCISH_STATS): macros, their bytes, sbuf[],
// obuf[], the input stack, and argument slots.
static int peak[6];

// Record how full the tables are now.
static void note(void)
{
  int now[6], i;
  now[0] = nmacs;
  now[1] = body_used;
  now[2] = stop;
  now[3] = otop;
  now[4] = nin;
  now[5] = nslots;
  for (i = 0; i < 6; i++)
    if (now[i] > peak[i])
      peak[i] = now[i];
}

// The most table k held (note()'s order).
int pp_mac_peak(int k)
{
  return peak[k];
}
#define NOTE() note()
#else
#define NOTE()
#endif

// Forget every macro and expansion (for each source file).
void pp_mac_init(void)
{
  body_used = nmacs = 0;
  stop = sfloor = nin = otop = nslots = 0;
  pend_code = -1;
}

// The macro named id, or -1.
int pp_mac_find(int id)
{
  int m;
  for (m = 0; m < nmacs; m++)
    if (mac_id[m] == id)
      return m;
  return -1;
}

// Expansion's buffers are full: stop.
static void too_long(void)
{
  fatal0(256 /* "macro expansion too long" */);
}

// The length of the encoded token at p.
static int token_len(const unsigned char *p)
{
  switch (p[0]) {
  case TC_ID: case TC_NUM: case TC_UNUM: case TC_KEYWORD:
    return 3;
  case TC_STR:
    return 2 + p[1];
  case TC_PARAM: case TC_PARAM_RAW: case TC_HASH_PARAM:
    return 2;
  }
  return 1;
}

// Write token code (value val, or n bytes at s) encoded at p; return
// its length.
static int encode(unsigned char *p, int code, int val,
                  const unsigned char *s, int n)
{
  int i;
  p[0] = (unsigned char)code;
  if (TC_IS_WORDVAL(code)) {
    p[1] = (unsigned char)(val & 0xFF);
    p[2] = (unsigned char)((val >> 8) & 0xFF);
    return 3;
  }
  if (code == TC_STR) {
    p[1] = (unsigned char)n;
    for (i = 0; i < n; i++)
      p[2 + i] = s[i];
    return 2 + n;
  }
  return 1;
}

// Read the encoded token at p into pt_val, pt_text and pt_len; return
// its code.
static int decode(const unsigned char *p)
{
  int i;
  if (TC_IS_WORDVAL(p[0])) {
    pt_val = p[1] | (p[2] << 8);
  } else if (p[0] == TC_STR) {
    pt_len = p[1];
    for (i = 0; i < pt_len; i++)
      pt_text[i] = p[2 + i];
  }
  return p[0];
}

// Start a macro's body, at the end of body[]: np parameters (-1 for an
// object-like macro) with ids params, the last __VA_ARGS__ if va.
void pp_def_begin(int np, const int *params, int va)
{
  int i;
  def_start = body_used;
  def_np = np;
  def_va = va;
  def_hash = 0;
  for (i = 0; i < np; i++)
    def_params[i] = params[i];
}

// Append a byte to the body being defined.
static void def_byte(int c)
{
  if (body_used >= MAX_MACRO_BYTES)
    fatal0(244 /* "too many macros, or macros too long" */);
  body[body_used++] = (unsigned char)c;
}

// The index of the parameter named id of the macro being defined, or
// -1.
static int param_of(int id)
{
  int i;
  for (i = 0; i < def_np; i++)
    if (def_params[i] == id)
      return i;
  return -1;
}

// Append a token to the body being defined: code, with its value val,
// or for a string its n bytes at s. In a function-like macro, a
// parameter becomes TC_PARAM, and # and the parameter after it
// TC_HASH_PARAM.
void pp_def_token(int code, int val, const unsigned char *s, int n)
{
  static unsigned char tok[2 + MAX_STR];
  int i, len, p = code == TC_ID || code == TC_KEYWORD ? param_of(val) : -1;
  if (def_hash) {
    if (p < 0)
      error0(252 /* "'#' must be followed by a macro parameter" */);
    def_byte(TC_HASH_PARAM);
    def_byte(p);
    def_hash = 0;
    return;
  }
  if (code == '#' && def_np >= 0) {
    def_hash = 1;
    return;
  }
  if (p >= 0) {
    def_byte(TC_PARAM);
    def_byte(p);
    return;
  }
  len = encode(tok, code, val, s, n);
  for (i = 0; i < len; i++)
    def_byte(tok[i]);
}

// Finish the body being defined, as macro id's. A parameter next to ##
// is used as it is, not expanded first (C11 6.10.3.1); ## can't be at
// either end. A macro may be defined again only with the same
// parameters and body (C11 6.10.3), which is then dropped.
void pp_def_end(int id)
{
  int m = pp_mac_find(id), n = body_used - def_start, i;
  unsigned char *p, *last = body, *end = body + body_used;
  if (def_hash)
    error0(252 /* "'#' must be followed by a macro parameter" */);
  for (p = body + def_start; p != end; p += token_len(p)) {
    if (*p == TC_PASTE) {
      if (p == body + def_start || p + 1 == end)
        error0(253 /* "'##' can't be at either end of a macro" */);
      if (*last == TC_PARAM)
        *last = TC_PARAM_RAW;
      if (p[1] == TC_PARAM)
        p[1] = TC_PARAM_RAW;
    }
    last = p;
  }
  if (m >= 0) {
    if (mac_len[m] != n || mac_np1[m] != def_np + 1 || mac_va[m] != def_va)
      error(239 /* "macro redefined differently:" */, pp_id_spelling(id));
    for (i = 0; i < n; i++)
      if (body[mac_at[m] + i] != body[def_start + i])
        error(239 /* "macro redefined differently:" */, pp_id_spelling(id));
    body_used = def_start;
    return;
  }
  if (nmacs >= MAX_MACROS)
    fatal0(244 /* "too many macros, or macros too long" */);
  mac_id[nmacs] = id;
  mac_at[nmacs] = def_start;
  mac_len[nmacs] = n;
  mac_np1[nmacs] = (char)(def_np + 1);
  mac_va[nmacs] = (char)def_va;
  mac_on[nmacs] = 0;
  nmacs++;
  NOTE();
  pp_set_macro(id, 1);
}

// #undef id: forget the macro, if there is one, closing up its body's
// space.
void pp_undef(int id)
{
  int m = pp_mac_find(id), n, i;
  if (m < 0)
    return;
  n = mac_len[m];
  memmove(body + mac_at[m], body + mac_at[m] + n,
          body_used - mac_at[m] - n);
  body_used -= n;
  for (i = m; i + 1 < nmacs; i++) {
    mac_id[i] = mac_id[i + 1];
    mac_at[i] = mac_at[i + 1] - n;
    mac_len[i] = mac_len[i + 1];
    mac_np1[i] = mac_np1[i + 1];
    mac_va[i] = mac_va[i + 1];
    mac_on[i] = mac_on[i + 1];
  }
  nmacs--;
  pp_set_macro(id, 0);
}

// The first free byte of sbuf[] above everything still in use: the
// arguments of the calls being expanded, and the texts being read.
static int sbuf_low(void)
{
  int low = sfloor;
  if (nin > 0 && in_end[nin - 1] > low)
    low = in_end[nin - 1];
  return low;
}

// Push an entry that reads the n bytes at p (which may be in sbuf[]
// above sbuf_low()), copied down to sbuf_low(), for macro m (or -1);
// bar makes it a barrier.
static void push_text(const unsigned char *p, int n, int m, int bar)
{
  int low = sbuf_low();
  if (nin >= MAX_EXPAND)
    fatal0(245 /* "macro expansion nested too deeply" */);
  if (low + n > MAX_EXP_BYTES)
    too_long();
  memmove(sbuf + low, p, n);
  in_at[nin] = low;
  in_end[nin] = low + n;
  in_mac[nin] = m;
  in_bar[nin] = (char)bar;
  nin++;
  stop = low + n;
  if (m >= 0)
    mac_on[m] = 1;
  NOTE();
}

// The next token, as pp_lex() gives one: the token read ahead, or the
// innermost text's next one, or the lexer's (pp_from_lexer says which);
// TC_END at a barrier.
static int next_tok(void)
{
  int k, c, at;
  if (pend_code >= 0) {
    c = pend_code;
    pend_code = -1;
    pt_val = pend_val;
    pt_first = pend_first;
    pp_from_lexer = pend_lex;
    pt_macro = pend_macro;
    return c;
  }
  while (nin > 0) {
    k = nin - 1;
    at = in_at[k];
    if (at < in_end[k]) {
      if (sbuf[at] == TC_SPACE) {
        in_at[k] = at + 1;
        continue;
      }
      c = decode(sbuf + at);
      in_at[k] = at + token_len(sbuf + at);
      pp_from_lexer = 0;
      pt_first = pt_space = 0;
      if (c == TC_ID || c == TC_KEYWORD)
        pt_macro = pp_is_macro(pt_val);
      return c;
    }
    if (in_bar[k])
      return TC_END;
    if (in_mac[k] >= 0)
      mac_on[in_mac[k]] = 0;    // this expansion has ended
    nin--;
  }
  pp_from_lexer = 1;
  return pp_lex();
}

// Append a byte to sbuf[].
static void sput(int c)
{
  if (stop >= MAX_EXP_BYTES)
    too_long();
  sbuf[stop++] = (unsigned char)c;
}

// Append the current token (code, pt_val, pt_text, pt_len) to sbuf[].
static void sput_token(int code)
{
  if (stop + 2 + pt_len > MAX_EXP_BYTES)
    too_long();
  stop += encode(sbuf + stop, code, pt_val, pt_text, pt_len);
}

// Collect the arguments of a call of macro m, whose '(' has been read,
// into sbuf[], as slots from nslots on; return how many.
static int collect(int m)
{
  int base = nslots, n = 0, depth = 0, c, np = mac_np1[m] - 1;
  stop = sbuf_low();
  if (base + np + 1 > MAX_ARG_SLOTS)
    too_long();
  a_at[base] = stop;
  for (;;) {
    c = next_tok();
    if (c == TC_END || c == TC_NL)
      error(249 /* "unterminated call of macro" */,
            pp_id_spelling(mac_id[m]));
    if (c == '#' && pp_from_lexer && pt_first)
      error0(250 /* "a directive inside a macro's arguments" */);
    if (depth == 0
        && (c == ')' || (c == ',' && !(mac_va[m] && n == np - 1)))) {
      a_end[base + n] = stop;
      n++;
      if (c == ')')
        break;
      if (base + n + 1 > MAX_ARG_SLOTS)
        too_long();
      a_at[base + n] = stop;
      continue;
    }
    if (c == '(')
      depth++;
    else if (c == ')')
      depth--;
    if (pt_space && stop > a_at[base + n])
      sput(TC_SPACE);
    sput_token(c);
  }
  if (np == 0 && n == 1 && a_at[base] == a_end[base])
    n = 0;                      // F(): no arguments
  if (mac_va[m] && n == np - 1) {
    a_at[base + n] = a_end[base + n] = stop;  // no variable arguments
    n++;
  }
  NOTE();
  return n;
}

// Expand argument slot k by itself (C11 6.10.3.1), into obuf[].
static void expand_arg(int k)
{
  int c;
  push_text(sbuf + a_at[k], a_end[k] - a_at[k], -1, 1);
  e_at[k] = otop;
  while ((c = pp_next()) != TC_END) {
    if (otop + 2 + pt_len > MAX_EXP_BYTES)
      too_long();
    otop += encode(obuf + otop, c, pt_val, pt_text, pt_len);
  }
  e_end[k] = otop;
  NOTE();
  nin--;                        // the barrier
}

// Append n bytes at p (in sbuf[] or obuf[]), tokens without blanks, to
// sbuf[]; return where the last token starts, or -1 if there are
// none.
static int put_tokens(const unsigned char *p, int n)
{
  int i, k, last = -1;
  for (i = 0; i < n; i += k) {
    k = token_len(p + i);
    if (p[i] == TC_SPACE)
      continue;
    if (stop + k > MAX_EXP_BYTES)
      too_long();
    last = stop;
    memmove(sbuf + stop, p + i, k);
    stop += k;
  }
  return last;
}

// Make argument slot k's text a string (#): its tokens spelled, with
// one space where blanks were between them (C11 6.10.3.2).
static void put_string(int k)
{
  static unsigned char str[MAX_STR + 1];
  int n = 0, len;
  const unsigned char *p, *end = sbuf + a_end[k];
  for (p = sbuf + a_at[k]; p != end; p += token_len(p)) {
    if (*p == TC_SPACE) {
      if (n >= MAX_STR)
        error0(12 /* "string literal too long" */);
      str[n++] = ' ';
      continue;
    }
    decode(p);
    len = pp_spell_token(*p, pt_val, pt_text, pt_len,
                         (char *)str + n, MAX_STR + 1 - n);
    if (len < 0)
      error0(12 /* "string literal too long" */);
    n += len;
  }
  pt_len = n;
  memcpy(pt_text, str, n);
  sput_token(TC_STR);
}

// Is c a letter, a digit or '_'?
static int is_word_char(int c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
         || (c >= '0' && c <= '9') || c == '_';
}

// Paste (##) the token at sbuf[lhs] and the one right after it, at
// sbuf[rhs], into one, in their place (C11 6.10.3.3): their spellings,
// joined, must spell a word, a decimal number or an operator.
static void paste(int lhs, int rhs)
{
  static char text[2 * MAX_NAME + 8];
  int n, k, code, rest = rhs + token_len(sbuf + rhs), uns = 0;
  unsigned v = 0;
  if (sbuf[lhs] == TC_STR || sbuf[rhs] == TC_STR)
    error(254 /* "pasting doesn't give a valid token:" */, "a string");
  decode(sbuf + lhs);
  n = pp_spell_token(sbuf[lhs], pt_val, pt_text, pt_len, text,
                     sizeof(text));
  decode(sbuf + rhs);
  k = n < 0 ? -1 : pp_spell_token(sbuf[rhs], pt_val, pt_text, pt_len,
                                  text + n, sizeof(text) - n);
  if (k < 0)
    error0(254 /* "pasting doesn't give a valid token:" */);
  n += k;
  if (is_word_char(text[0]) && !(text[0] >= '0' && text[0] <= '9')) {
    for (k = 0; k < n; k++)
      if (!is_word_char(text[k]))
        error(254 /* "pasting doesn't give a valid token:" */, text);
    if (n > MAX_NAME)
      n = MAX_NAME;
    text[n] = 0;
    code = pp_word(text, n);
  } else if (text[0] >= '0' && text[0] <= '9') {
    for (k = 0; k < n && text[k] >= '0' && text[k] <= '9'; k++) {
      if (v > (0xFFFFu - (unsigned)(text[k] - '0')) / 10u)
        error0(10 /* "number does not fit in 16 bits" */);
      v = v * 10u + (unsigned)(text[k] - '0');
    }
    if (k < n && (text[k] == 'u' || text[k] == 'U')) {
      uns = 1;
      k++;
    }
    if (k < n)
      error(254 /* "pasting doesn't give a valid token:" */, text);
    pt_val = (int)v;
    code = uns ? TC_UNUM : TC_NUM;
  } else {
    code = pp_op(text);
    if (code < 0)
      error(254 /* "pasting doesn't give a valid token:" */, text);
  }
  // The result, no longer than the two, then what followed them.
  k = encode(sbuf + lhs, code, pt_val, pt_text, 0);
  n = stop - rest;
  memmove(sbuf + lhs + k, sbuf + rest, n);
  stop = lhs + k + n;
}

// Where the last token starts of those from sbuf[from] to stop.
static int last_token(int from)
{
  int n;
  while (from + (n = token_len(sbuf + from)) < stop)
    from += n;
  return from;
}

// Expand a call of function-like macro m, whose '(' has been read: its
// replacement is read next, in place of the call.
static void call(int m)
{
  int base = nslots, save_floor = sfloor, otop0 = otop, np = mac_np1[m] - 1;
  int rep, k, s, c, last = -1, lhs = -1, pasting = 0, start;
  const unsigned char *b, *end;
  if (collect(m) != np)
    error(251 /* "wrong number of arguments for macro" */,
          pp_id_spelling(mac_id[m]));
  nslots = base + np;
  sfloor = stop;                // keep the arguments
  for (k = 0; k < np; k++)
    e_at[base + k] = -1;
  end = body + mac_at[m] + mac_len[m];
  for (b = body + mac_at[m]; b != end; b += token_len(b))
    if (*b == TC_PARAM && e_at[base + b[1]] < 0)
      expand_arg(base + b[1]);
  nslots = base + np;
  NOTE();
  stop = rep = sfloor;
  for (b = body + mac_at[m]; b != end; b += token_len(b)) {
    c = *b;
    if (c == TC_PASTE) {
      lhs = last;
      pasting = 1;
      continue;
    }
    start = stop;
    s = TC_IS_PARAM(c) ? base + b[1] : 0;
    if (c == TC_PARAM) {
      k = put_tokens(obuf + e_at[s], e_end[s] - e_at[s]);
    } else if (c == TC_PARAM_RAW) {
      k = put_tokens(sbuf + a_at[s], a_end[s] - a_at[s]);
    } else if (c == TC_HASH_PARAM) {
      k = stop;
      put_string(s);
    } else {
      k = put_tokens(b, token_len(b));
    }
    if (pasting && lhs >= 0 && k >= 0) {
      paste(lhs, start);
      k = last_token(lhs);
    } else if (pasting && k < 0) {
      k = lhs;                  // an empty right operand
    }
    pasting = 0;
    last = k;
  }
  NOTE();
  otop = otop0;
  nslots = base;
  sfloor = save_floor;
  push_text(sbuf + rep, stop - rep, m, 0);
}

// The next token without macros expanded (for defined, in #if).
int pp_next_raw(void)
{
  return next_tok();
}

// The next token, with macros expanded (pp_lex()'s, from the lexer or
// an expansion; pp_from_lexer says which); TC_END at the end of the
// file, or of an argument being expanded. __LINE__ is the current line,
// and __FILE__ the current file's name.
int pp_next(void)
{
  int c, m, id, first, lex, code;
  for (;;) {
    code = next_tok();
    if (code == TC_ID && !pt_macro && pt_val == ID_LINE) {
      pt_val = src_line;
      return TC_NUM;
    }
    if (code == TC_ID && !pt_macro && pt_val == ID_FILE) {
      pt_len = (int)strlen(src_name);
      memcpy(pt_text, src_name, pt_len);
      return TC_STR;
    }
    if ((code != TC_ID && code != TC_KEYWORD) || !pt_macro)
      return code;
    m = pp_mac_find(pt_val);
    if (mac_on[m])
      return code;              // inside its own expansion
    if (mac_np1[m] == 0) {
      push_text(body + mac_at[m], mac_len[m], m, 0);
      continue;
    }
    id = pt_val;
    first = pt_first;
    lex = pp_from_lexer;
    c = next_tok();
    if (c != '(') {
      // Not a call: the name, then that token, as they are.
      pend_code = c;
      pend_val = pt_val;
      pend_first = (char)pt_first;
      pend_lex = (char)pp_from_lexer;
      pend_macro = (char)pt_macro;
      pt_val = id;
      pt_first = first;
      pp_from_lexer = lex;
      return code;
    }
    call(m);
  }
}
