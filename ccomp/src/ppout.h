/*
 * ccpp's output (ppout.c): the token stream, NAME.i, and at the end the
 * name file, NAME.nam (../CCPP.md). Include after ccish.h.
 */
#ifndef PPOUT_H
#define PPOUT_H

#ifdef CCISH_TEXT
extern int pp_opt_text;         /* -E: write text, not NAME.i */
#endif

void pp_out_open(const char *base);
void pp_out(int code, int val, const unsigned char *s, int n);
void pp_out_finish(void);       /* the end, the stamp, then NAME.nam */
void pp_out_abort(void);        /* after an error */

#endif
