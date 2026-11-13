// The command-line options diag.c holds for the programs that read
// them: their main()s (main.c, ccpp.c, cccomp.c, ccgen.c, ccrel.c) and
// the passes that act on one (irin.c, emit.c, link.c). Part of diag.c's
// exports, with diag.h and diag.src.h: apart, because every variable a
// file can see counts against cccomp's MAX_VARS when ccish compiles
// itself. Include after diag.h.
#ifndef DIAG_OPT_H
#define DIAG_OPT_H

#ifdef CCISH_LISTING
extern int opt_list;        // -v: print a listing of the generated code
#else
#define opt_list 0          // no listing in the small build
#endif
extern int opt_ir;          // -r: print the IR as ccgen reads it
#ifdef CCISH_LISTING
extern int opt_dump;        // -m: print the machine ops after peephole
                            // (full build only)
#endif
extern int opt_keep;        // -k: keep the stream files

// Is arg option -c (c lowercase), in either case? No two options of
// one program differ only in case (doc/linking.md, section 2), but
// CMD uppercases the command line on an Apple II.
int is_opt(const char *arg, int c);

#endif
