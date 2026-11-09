// The reader of the name file, NAME.nam (nam.c, ../CCPP.md), for cccomp
// and ccgen. nam_bf is declared only after bio.h, its type's
// header, which parse.c, say, doesn't need.
#ifndef NAM_H
#define NAM_H

extern int nam_ids;                 // its stamp: the id count
extern int nam_sum;                 // and the checksum

#ifdef BIO_H
extern Bfile *nam_bf;               // the buffer to read it through,
                                    // which the caller lends
#endif
void nam_open(const char *base);    // check it, and keep its stamp
#ifdef CCISH_DIAG
void nam_base(const char *base);    // or open it when first used
#endif
const char *nam_name(int name);     // an id's spelling
const char *nam_file(int k);        // source file k's name
void nam_stale(void);               // stop: stale or damaged
void nam_close(void);

#endif
