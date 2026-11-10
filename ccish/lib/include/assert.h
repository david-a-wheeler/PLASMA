/* assert.h for ccish. assert(e) is always ((void)0), as the standard's
 * is with NDEBUG defined: no code, and e isn't evaluated. A checking
 * assert needs abort(), which ccish doesn't have yet (exit() isn't
 * implemented: stdlib.h). So an assert costs nothing here, and the
 * host builds of a program (gcc or clang, without NDEBUG) check it.
 * Like the standard's, it has no include guard: each inclusion
 * defines assert again. */
#undef assert
#define assert(e) ((void)0)
