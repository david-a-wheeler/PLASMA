/*
 * ccish's objects (../OBJECT.md), NAME.obj and NAME.syms, which ccgen
 * writes (and cccomp, whose IR file carries their data records and
 * summary) and ccrel reads: the limits on one object that they all
 * share, and the file names. The format itself is in obj.fmt.h and
 * obj.sym.h, apart, so that the parser's files spend none of cccomp's
 * identifier table on names they don't use.
 */
#ifndef OBJ_H
#define OBJ_H

#define MAX_FUNCS    242    /* functions (220, pexpr.c) */
#define MAX_VARS     79     /* globals, plus the parameters and locals
                             * visible at once (74, link.c) */
#define MAX_FUNC_CODE 1280  /* bytecode in one function (1,053) */
#define MAX_FUNC_RELOCS 128 /* relocations in one function (112) */

/* Stream file names: the output name plus these suffixes. The back end
 * writes one stream of tagged records, then the symbol summary. */
#define SUFFIX_OBJ  ".obj"
#define SUFFIX_SYMS ".syms"
#define BSS_MIN     256     /* bytes: a global with no initializer this big
                             * or bigger may move to the heap (approach.md,
                             * "Large zero-filled data on the heap") */

#endif
