/*
 * ccish's object format (../OBJECT.md): NAME.obj and NAME.syms, which
 * ccgen writes (and cccomp, whose IR file carries their data records
 * and summary) and ccrel reads; and the limits on one object that they
 * all share. Changing the format changes OBJ_VERSION.
 */
#ifndef OBJ_H
#define OBJ_H

#define MAX_FUNCS    302    /* functions (274) */
#define MAX_VARS     82     /* globals, plus the parameters and locals
                             * visible at once (74) */
#define MAX_FUNC_CODE 1280  /* bytecode in one function (1,053) */
#define MAX_FUNC_RELOCS 128 /* relocations in one function (112) */

/* Stream file names: the output name plus these suffixes. The back end
 * writes one stream of tagged records, then the symbol summary. */
#define SUFFIX_OBJ  ".obj"
#define SUFFIX_SYMS ".syms"
#define OBJ_VERSION 5       /* the format version, in the symbol summary */

/* Record tags in the stream (see ../OBJECT.md), as letters so that a
 * dump of the file is easy to read. */
enum { REC_DATA = 'D', REC_DREL = 'R', REC_STR = 'S', REC_FUNC = 'F',
       REC_HELPER = 'H' };

/* Relocation kinds and symbol flags. A REL_DATA target is a data
 * offset, a REL_STR target an offset in the string pool, a REL_FUNC
 * target a function symbol index, and a REL_DSYM target a data symbol
 * index, with an addend. SYM_CALLED is "used" for a data symbol;
 * SYM_RETURNS, SYM_CMDSYS and SYM_NOTCMDSYS (a CMDSYS name declared
 * differently) are for functions. */
enum { REL_DATA = 1, REL_FUNC = 2, REL_STR = 3, REL_DSYM = 4,
       REL_HELPER = 5 };
enum { SYM_DEFINED = 1, SYM_CALLED = 2, SYM_RETURNS = 4, SYM_LOCAL = 8,
       SYM_CMDSYS = 16, SYM_NOTCMDSYS = 32 };

/* The helper functions an object may hold (approach.md, "unsigned and
 * helper functions design"), by number. */
enum { HELP_UDIV, HELP_UMOD, HELP_USHR, NHELPERS };

#endif
