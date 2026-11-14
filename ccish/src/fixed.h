// The fixed ids (../CCPP.md, "The fixed ids"): the names every name
// file starts with, in this order, which ccpp gives first (ppname.c) and
// cccomp knows by number (parse.c). Changing them changes NAM_VERSION.
// Ids 1 to CMDSYS_IDS are the CMDSYS block: the NCMDSYS functions, then
// spare ids, so that a function a later PLASMA adds takes the next one
// and no other id moves (doc/linking.md, 3.1a).
#ifndef FIXED_H
#define FIXED_H

#define NCMDSYS      25       // the CMDSYS functions: ids 1 to NCMDSYS
#define CMDSYS_IDS   40       // the CMDSYS block, NCMDSYS and 15 spare
#define NFIXED       48       // the CMDSYS block, main, pstr, ccpp's
                              // own names, and the two imports use
                              // (approach.md issue 22)
// A CMDSYS function's C name is CMD_PREFIX and its PLASMA name
// (__cmd_puts for PUTS): a name C reserves to the implementation
// (C11 7.1.3), so no program can define one, and the standard names
// (puts, memset, ...) stay free for a C library with C's meanings.
// ccpp knows the names by id (ppname.c); cclink drops the prefix in
// the import (link.c).
#define CMD_PREFIX   "__cmd_"
#define CMD_PREFIX_LEN 6
#define ID_MEMSET    16
#define ID_MEMCPY    17
#define ID_MAIN      41
#define ID_PSTR      42
#define ID_CCISH     43       // __CCISH__, a predefined macro
#define ID_FILE      44       // __FILE__
#define ID_LINE      45       // __LINE__
#define ID_DEFINED   46       // defined, in #if
#define ID_ASM       47       // __asm__, an asm label: __asm__("PERR")
#define ID_PIMPORT   48       // __plasma_import, what ccpp makes of
                              // #pragma plasma import NAME

#endif
