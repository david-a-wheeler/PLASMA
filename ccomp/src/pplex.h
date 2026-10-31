/*
 * ccpp's lexer (pplex.c): the source, a line at a time, as tokens with
 * the codes of the token stream (token.h, ../CCPP.md). Include after
 * ccish.h.
 */
#ifndef PPLEX_H
#define PPLEX_H

extern int pt_val;                  /* TC_NUM, TC_UNUM value; TC_ID,
                                     * TC_KEYWORD id */
extern unsigned char pt_text[MAX_STR + 1];  /* TC_STR bytes */
extern int pt_len;                  /* TC_STR length */
extern int pt_first;                /* the token is its line's first */

void pp_lex_open(Bfile *f);
int  pp_lex(void);                  /* the next token's code */

#endif
