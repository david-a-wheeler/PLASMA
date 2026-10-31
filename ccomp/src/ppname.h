/*
 * ccpp's name pool (ppname.c): every identifier ccpp meets, and every
 * source file's name, in the order first met; an identifier's id is
 * its place among the identifiers, from 1. Written out as NAME.nam
 * (../CCPP.md). Include after ccish.h.
 */
#ifndef PPNAME_H
#define PPNAME_H

extern int pp_macro_flag;                 /* pp_intern()'s: a macro name */

void pp_names_init(void);                 /* the fixed ids, 1-NFIXED */
int  pp_intern(const char *s, int n);     /* the id of s (n chars) */
void pp_set_macro(int id, int on);        /* id names a macro, or not */
int  pp_is_macro(int id);
int  pp_add_file(const char *path);       /* a file's number, from 1 */
void pp_file_path(int k, char *buf);      /* file k's name */
int  pp_nids(void);                       /* ids given so far */
#ifdef CCISH_STATS
int  pp_pool_used(void);
#endif
int  pp_name_hash(int id, int *mul);      /* a tag's hash, for TC_TAG */
const char *pp_id_spelling(int id);             /* id's spelling, for messages */
int  pp_names_write(Bfile *f);            /* NAME.nam; the checksum */

#endif
