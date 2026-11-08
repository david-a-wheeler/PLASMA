/*
 * The reader of the name file, NAME.nam (nam.c, ../CCPP.md), for cccomp
 * and ccgen.
 */
#ifndef NAM_H
#define NAM_H

extern int nam_ids;                 /* its stamp: the id count */
extern int nam_sum;                 /* and the checksum */

void nam_open(const char *base);    /* check it, and keep its stamp */
void nam_base(const char *base);    /* or open it when first used */
int  nam_checked(void);             /* has it been opened and checked? */
const char *nam_name(int name);     /* an id's spelling */
const char *nam_file(int k);        /* source file k's name */
void nam_stale(void);               /* stop: stale or damaged */
void nam_close(void);

#endif
