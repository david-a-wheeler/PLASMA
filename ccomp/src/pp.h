/*
 * ccpp's driver (pp.c): preprocess one source file into its token
 * stream and name file (../CCPP.md). Include after ccish.h.
 */
#ifndef PP_H
#define PP_H

extern int pp_cur_file;             /* the file being read: its number */

void pp_include_dir(const char *dir);             /* -I dir */
void pp_option(int kind, const char *text);       /* -D or -U ('D', 'U') */
int  pp_arg(int argc, char **argv, int i);        /* -I, -D, -U: its last
                                                   * argument, or -1 */
void pp_file(const char *src, const char *base);  /* NAME.i, NAME.nam */
void pp_abort(void);                              /* after an error */

#endif
