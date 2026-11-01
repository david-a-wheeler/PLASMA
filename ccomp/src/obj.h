/*
 * ccish's object format (../OBJECT.md): NAME.obj and NAME.syms, which
 * ccgen writes (and cccomp, whose IR file carries their data records
 * and summary) and ccrel reads; and the limits on one object that they
 * all share. Changing the format changes OBJ_VERSION.
 */
#ifndef OBJ_H
#define OBJ_H

#define MAX_FUNCS    280    /* functions (254, parse.c) */
#define MAX_VARS     79     /* globals, plus the parameters and locals
                             * visible at once (71, parse.c) */
#define MAX_FUNC_CODE 1280  /* bytecode in one function (1,053) */
#define MAX_FUNC_RELOCS 128 /* relocations in one function (112) */

/* Stream file names: the output name plus these suffixes. The back end
 * writes one stream of tagged records, then the symbol summary. */
#define SUFFIX_OBJ  ".obj"
#define SUFFIX_SYMS ".syms"
#define OBJ_VERSION 6       /* the format version, in the symbol summary */
#define BSS_MIN     256     /* bytes: a global with no initializer this big
                             * or bigger may move to the heap (approach.md,
                             * "Large zero-filled data on the heap") */

/* Record tags in the stream (see ../OBJECT.md), as letters so that a
 * dump of the file is easy to read. */
enum { REC_DATA = 'D', REC_DREL = 'R', REC_STR = 'S', REC_FUNC = 'F',
       REC_HELPER = 'H', REC_ZERO = 'Z' };

/* A record the back end copies to the stream as it is (irin.c):
 * data, a relocation, a string, or zero-filled bytes. */
#define REC_IS_PASSTHROUGH(t)  ((t) == REC_DATA || (t) == REC_DREL \
                                || (t) == REC_STR || (t) == REC_ZERO)

/* Relocation kinds and symbol flags. A REL_DATA target is a data
 * offset, a REL_STR target an offset in the string pool, a REL_FUNC
 * target a function symbol index, and a REL_DSYM target a data symbol
 * index, with an addend. SYM_CALLED is "used" for a data symbol;
 * SYM_RETURNS, SYM_CMDSYS and SYM_NOTCMDSYS (a CMDSYS name declared
 * differently) are for functions. For a data symbol, SYM_BSS: it is
 * defined with no data bytes, at an offset in the object's zero-filled
 * space, for ccrel to place (approach.md, "Large zero-filled data on
 * the heap"); SYM_INDATA: this object's data holds its address, so it
 * can't move to the heap. */
enum { REL_DATA = 1, REL_FUNC = 2, REL_STR = 3, REL_DSYM = 4,
       REL_HELPER = 5 };
enum { SYM_DEFINED = 1, SYM_CALLED = 2, SYM_RETURNS = 4, SYM_LOCAL = 8,
       SYM_CMDSYS = 16, SYM_NOTCMDSYS = 32, SYM_BSS = 64, SYM_INDATA = 128 };

/* The helper functions an object may hold (approach.md, "unsigned and
 * helper functions design"), by number. */
enum { HELP_UDIV, HELP_UMOD, HELP_USHR, NHELPERS };

#endif
