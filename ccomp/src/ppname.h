/*
 * ccpp's name pool (ppname.c): every identifier ccpp meets, and every
 * source file's name, in the order first met; an identifier's id is
 * its place among the identifiers, from 1. Written out as NAME.nam
 * (../CCPP.md). Include after ccish.h.
 */
#ifndef PPNAME_H
#define PPNAME_H

void pp_names_init(void);                 /* the fixed ids, 1-NFIXED */
int  pp_intern(const char *s, int n);     /* the id of s (n chars) */
int  pp_add_file(const char *path);       /* a file's number, from 1 */
int  pp_nids(void);                       /* ids given so far */
int  pp_names_write(Bfile *f);            /* NAME.nam; the checksum */

#endif
