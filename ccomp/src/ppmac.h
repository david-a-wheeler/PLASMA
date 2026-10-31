/*
 * ccpp's macros (ppmac.c): the #define table, and their expansion.
 * Include after token.h.
 */
#ifndef PPMAC_H
#define PPMAC_H

#define MAX_PP_PARAMS 16    /* parameters of one macro */

extern int pp_from_lexer;           /* pp_next()'s token is the lexer's */

void pp_mac_init(void);
int  pp_mac_find(int id);           /* the macro named id, or -1 */
void pp_def_begin(int np, const int *params, int va);  /* a body: its */
void pp_def_token(int code, int val, const unsigned char *s, int n);
void pp_def_end(int id);            /* tokens, then its name */
void pp_undef(int id);
int  pp_next(void);                 /* the next token, macros expanded */
int  pp_next_raw(void);             /* or not */
#ifdef CCISH_STATS
int  pp_mac_peak(int k);            /* how full a table got */
#endif

#endif
