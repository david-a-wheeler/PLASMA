/*
 * ccpp's driver (pp.c): preprocess one source file into its token
 * stream and name file (../CCPP.md). Include after ccish.h.
 */
#ifndef PP_H
#define PP_H

void pp_file(const char *src, const char *base);  /* NAME.i, NAME.nam */
void pp_abort(void);                              /* after an error */

#endif
