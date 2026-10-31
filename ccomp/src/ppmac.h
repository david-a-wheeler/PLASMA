/*
 * ccpp's macros (ppmac.c): the #define table, and the expansions being
 * read. Include after ccish.h.
 */
#ifndef PPMAC_H
#define PPMAC_H

void pp_mac_init(void);
int  pp_mac_find(int id);           /* the macro named id, or -1 */
void pp_def_begin(void);            /* a body: its tokens, then its name */
void pp_def_token(int code, int val, const unsigned char *s, int n);
void pp_def_end(int id);
void pp_undef(int id);
int  pp_mac_active(int m);          /* being expanded now */
void pp_expand(int m);              /* read macro m's body next */
int  pp_exp_next(void);             /* its next token, or -1 at the end */

#endif
