// ccish's objects (../OBJECT.md), NAME.obj and NAME.syms, which ccgen
// writes (and cccomp, whose IR file carries their data records and
// summary) and cclink reads: the limits on one object that they all
// share, and the file names. The format itself is in obj.fmt.h and
// obj.sym.h, apart, so that the parser's files spend none of cccomp's
// identifier table on names they don't use.
#ifndef OBJ_H
#define OBJ_H

#define MAX_FUNCS    242    // limit file: functions declared, defined or
                            // called
#ifndef CCISH_BIG_LIMITS
#define MAX_VARS     79     // limit file: globals, plus the parameters and
                            // locals visible at once
#else
#define MAX_VARS     128    // the full build's: its own link.c needs 85
#endif
#define MAX_FUNC_CODE 1280  // limit function: bytes of bytecode in one
                            // function
#define MAX_FUNC_RELOCS 128 // limit function: calls and data references in
                            // one function

// Stream file names: the output name plus these suffixes. The back end
// writes one stream of tagged records, then the symbol summary.
#define SUFFIX_OBJ  ".obj"
#define SUFFIX_SYMS ".syms"
#define BSS_MIN     256     // bytes: a global with no initializer this big
                            // or bigger may move to the heap (approach.md,
                            // "Large zero-filled data on the heap");
                            // smaller ones stay in the module

#endif
