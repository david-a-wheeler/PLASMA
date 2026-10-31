/*
 * ccpp's macros (approach.md, "ccpp design"): the table of #defines,
 * each a name's id and a body, kept as tokens in the token stream's own
 * encoding (../CCPP.md), so that expanding one replays its bytes; and
 * the stack of expansions being read. A macro being expanded is not
 * expanded again inside itself (C's rescanning rule, C11 6.10.3.4).
 *
 * The table is parallel arrays: macro m has name mac_id[m], and its body
 * is mac_len[m] bytes of body[] from mac_at[m]; bodies are in the
 * table's order, with no gaps.
 */
#include <string.h>
#include "ccish.h"
#include "token.h"
#include "ppname.h"
#include "pplex.h"
#include "ppmac.h"

static unsigned char body[MAX_MACRO_BYTES];
static int body_used;
static int mac_id[MAX_MACROS];
static int mac_at[MAX_MACROS];
static int mac_len[MAX_MACROS];
static char mac_on[MAX_MACROS];     /* being expanded now */
static int nmacs;
static int def_start;               /* where the body being defined starts */

/* The expansions being read, innermost last: each one's macro and the
 * offset of its next byte in body[]. */
static int exp_mac[MAX_EXPAND];
static int exp_at[MAX_EXPAND];
static int nexp;

/* Forget every macro (for each source file). */
void pp_mac_init(void)
{
  body_used = 0;
  nmacs = 0;
  nexp = 0;
}

/* The macro named id, or -1. */
int pp_mac_find(int id)
{
  int m;
  for (m = 0; m < nmacs; m++)
    if (mac_id[m] == id)
      return m;
  return -1;
}

/* Start a macro's body, at the end of body[]. */
void pp_def_begin(void)
{
  def_start = body_used;
}

/* Append a byte to the body being defined. */
static void def_byte(int c)
{
  if (body_used >= MAX_MACRO_BYTES)
    fatal(244 /* "too many macros, or macros too long" */, NULL);
  body[body_used++] = (unsigned char)c;
}

/* Append a token to the body being defined: code, with its value val,
 * or for a string its n bytes at s, encoded as in the token stream. */
void pp_def_token(int code, int val, const unsigned char *s, int n)
{
  int i;
  def_byte(code);
  if (code == TC_ID || code == TC_NUM || code == TC_UNUM
      || code == TC_KEYWORD) {
    def_byte(val & 0xFF);
    def_byte((val >> 8) & 0xFF);
  } else if (code == TC_STR) {
    def_byte(n);
    for (i = 0; i < n; i++)
      def_byte(s[i]);
  }
}

/* Finish the body being defined, as macro id's. A macro may be defined
 * again only with the same body (C11 6.10.3), which is then dropped. */
void pp_def_end(int id)
{
  int m = pp_mac_find(id), n = body_used - def_start, i;
  if (m >= 0) {
    if (mac_len[m] != n)
      error(239 /* "macro redefined differently:" */, pp_spell(id));
    for (i = 0; i < n; i++)
      if (body[mac_at[m] + i] != body[def_start + i])
        error(239 /* "macro redefined differently:" */, pp_spell(id));
    body_used = def_start;
    return;
  }
  if (nmacs >= MAX_MACROS)
    fatal(244 /* "too many macros, or macros too long" */, NULL);
  mac_id[nmacs] = id;
  mac_at[nmacs] = def_start;
  mac_len[nmacs] = n;
  mac_on[nmacs] = 0;
  nmacs++;
  pp_set_macro(id, 1);
}

/* #undef id: forget the macro, if there is one, closing up its body's
 * space. */
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
    mac_on[i] = mac_on[i + 1];
  }
  nmacs--;
  pp_set_macro(id, 0);
}

/* Is macro m being expanded now (so not to be expanded again)? */
int pp_mac_active(int m)
{
  return mac_on[m];
}

/* Start reading macro m's body as its expansion. */
void pp_expand(int m)
{
  if (nexp >= MAX_EXPAND)
    fatal(245 /* "macro expansion nested too deeply" */, NULL);
  exp_mac[nexp] = m;
  exp_at[nexp] = mac_at[m];
  nexp++;
  mac_on[m] = 1;
}

/* The next token of the expansions being read, as pp_lex() gives one
 * (pt_val, pt_text, pt_len); or -1 when they have all ended. */
int pp_exp_next(void)
{
  int m, at, code, i;
  while (nexp > 0) {
    m = exp_mac[nexp - 1];
    at = exp_at[nexp - 1];
    if (at < mac_at[m] + mac_len[m]) {
      code = body[at++];
      if (code == TC_ID || code == TC_NUM || code == TC_UNUM
          || code == TC_KEYWORD) {
        pt_val = body[at] | (body[at + 1] << 8);
        at += 2;
      } else if (code == TC_STR) {
        pt_len = body[at++];
        for (i = 0; i < pt_len; i++)
          pt_text[i] = body[at++];
      }
      exp_at[nexp - 1] = at;
      return code;
    }
    mac_on[m] = 0;      /* this expansion has ended */
    nexp--;
  }
  return -1;
}
