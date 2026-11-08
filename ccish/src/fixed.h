/*
 * The fixed ids (../CCPP.md, "The fixed ids"): the names every name
 * file starts with, in this order, which ccpp gives first (ppname.c) and
 * cccomp knows by number (parse.c). Changing them changes NAM_VERSION.
 */
#ifndef FIXED_H
#define FIXED_H

#define NFIXED       33       /* the CMDSYS functions, main, pstr,
                               * ccpp's own names, and the two imports
                               * use (approach.md issue 22) */
#define NCMDSYS      25       /* the CMDSYS functions: ids 1 to NCMDSYS */
#define ID_MEMSET    16
#define ID_MEMCPY    17
#define ID_MAIN      26
#define ID_PSTR      27
#define ID_CCISH     28       /* __CCISH__, a predefined macro */
#define ID_FILE      29       /* __FILE__ */
#define ID_LINE      30       /* __LINE__ */
#define ID_DEFINED   31       /* defined, in #if */
#define ID_ASM       32       /* __asm__, an asm label: __asm__("PERR") */
#define ID_PIMPORT   33       /* __plasma_import, what ccpp makes of
                               * #pragma plasma import NAME */

#endif
