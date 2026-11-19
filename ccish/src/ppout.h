// ccpp's output (ppout.c): the token stream, NAME.i, and at the end the
// name file, NAME.nam (../CCPP.md). Include after <stdio.h>.
#ifndef PPOUT_H
#define PPOUT_H

#include "bio.h"

#ifdef CCISH_TEXT
extern int pp_opt_text;         // -E: write text, not NAME.i
#endif

void pp_out_open(const char *base);
void pp_out(int code, int val, const unsigned char *s, int n);
// The end, the stamp, then NAME.nam, written into a Bfile the caller
// has closed and lent (the source's: everything is read by then, so
// its buffer is the one that can be reused). The parameter is unnamed
// here: a name would enter every includer's name pool.
void pp_out_finish(Bfile *);
void pp_out_abort(void);        // after an error

#endif
