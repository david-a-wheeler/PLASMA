// ccish's object format (../OBJECT.md): the relocation kinds and symbol
// flags of NAME.syms (and of the IR file, which carries them), for the
// files that write or read them. Apart from obj.fmt.h so that ptop.c,
// which makes them, doesn't count obj.fmt.h's names against cccomp's
// MAX_IDS.
#ifndef OBJ_SYM_H
#define OBJ_SYM_H

// ENUM VALUES: assigned here; a set test or value manipulation on
// one is a #define with a _Static_assert (next to the values, or
// file-local in its only user), and the values may be reassigned
// freely as long as those checks hold. See ../doc/enums.md.

// Relocation kinds and symbol flags. A REL_DATA target is a data
// offset, a REL_STR target an offset in the string pool, a REL_FUNC
// target a function symbol index, and a REL_DSYM target a data symbol
// index, with an addend; a REL_ZERO target is an offset in the
// object's zero area (its zero-filled static locals). SYM_CALLED is "used" for a data symbol;
// SYM_RETURNS, SYM_CMDSYS and SYM_NOTCMDSYS (a CMDSYS name declared
// differently) are for functions. For a data symbol, SYM_BSS: it is
// defined with no data bytes, at an offset in the object's zero-filled
// space, for ccrel to place (approach.md, "Large zero-filled data on
// the heap"); SYM_INDATA: this object's data holds its address, so it
// can't move to the heap.
enum { REL_DATA = 1, REL_FUNC = 2, REL_STR = 3, REL_DSYM = 4,
       REL_HELPER = 5, REL_ZERO = 6 };
enum { SYM_DEFINED = 1, SYM_CALLED = 2, SYM_RETURNS = 4, SYM_LOCAL = 8,
       SYM_CMDSYS = 16, SYM_NOTCMDSYS = 32, SYM_BSS = 64, SYM_INDATA = 128 };
// A symbol that is a PLASMA import if no object defines it (approach.md
// issue 22): its entry then has its module and its PLASMA name. The
// bit differs, since functions and globals use different flags.
#define SYM_FIMPORT 64      // a function's
#define SYM_DIMPORT 16      // a global's

#endif
