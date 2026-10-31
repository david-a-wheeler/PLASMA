/*
 * ccish - a compiler for "C-ish", a growing subset of C, that emits
 * PLASMA VM modules. See ../approach.md for the design and
 * ../../doc/Bytecodes.md for the target format.
 *
 * First the preprocessor and lexer, the program ccpp (pp*.c), turns the
 * source into a token stream, NAME.i, and a name file, NAME.nam
 * (../CCPP.md). Then the compiler is a pipeline of four stages that
 * pass a stream of ops along, with no syntax tree. Stage 1 is the
 * front end, the program cccomp; stages 2 to 4 are the back end, the
 * program ccgen; the IR goes between them in a file, NAME.ir
 * (approach.md 18.7; formats in ../OBJECT.md):
 *
 *   tokin.c  the reader of NAME.i, a token at a time
 *   parse.c  stage 1: tokens -> IR ops          (ir_put)
 *   irout.c  the IR buffer, and the writer of NAME.ir: IR ops, data,
 *            strings, and the symbol summary at the end
 *   irin.c   the reader of NAME.ir                (lower_op)
 *   lower.c  stage 2: IR ops -> machine ops     (peep_put)
 *   peep.c   stage 3: peephole optimization     (emit_put)
 *   emit.c   stage 4: encode bytes, one function at a time, and write
 *            them to the stream file, NAME.obj; then NAME.syms
 *
 * Then a separate final pass, link.c (the program ccrel), reads only
 * the stream files and summaries and writes the REL module (approach.md
 * 18.1). It uses none of the compiler's tables.
 *
 * Fixed-size tables are used on purpose: the compiler is meant to run
 * on a 64K machine eventually, so its limits should be visible.
 *
 * Each .c file has a header for what it defines (parse.c, parse.h),
 * and what several files must agree on has a header of its own: this
 * one (the builds), cclimits.h, token.h and fixed.h (ccpp and cccomp),
 * ir.h (cccomp and ccgen), obj.h (cccomp, ccgen and ccrel) and mop.h
 * (ccgen's stages). Headers don't include headers: each .c includes
 * what it uses, in order, each after what it needs (approach.md
 * decision 25). This one comes first: the builds' feature macros.
 */
#ifndef BUILD_H
#define BUILD_H

/* ---------- Builds ----------
 * The full build (the default) has everything. The small build, for
 * the 64K Apple II, is made with -DCCISH_SMALL, and leaves out what a
 * valid program doesn't need (approach.md decision 21 and 18.6 C1,
 * "Two builds: the plan"). Code tests these feature macros, never
 * CCISH_SMALL itself:
 *   CCISH_LISTING  -l (ccrel's listing), and what only it uses. ccgen's
 *                  -i is in both builds (approach.md decision 22).
 *   CCISH_CHECKS   checks that only a compiler bug or a damaged file
 *                  would fail: so far, ccgen's check of each IR_SWAP's
 *                  and unsigned comparison's blocks (irin.c). Both
 *                  builds write the same files.
 *   CCISH_TEXT     ccpp -E, the token stream as text (ppout.c): for
 *                  debugging, so in the full build only for now; it can
 *                  be put in the small build if ccpp has room.
 * And ones that neither build has: tools/measure.py builds a ccpp with
 *   CCISH_STATS    report how full ccpp's tables got, for sizing them.
 * and
 *   CCISH_FAST     faster code that is bigger when ccish compiles it,
 *                  kept for when size matters less (so far cccomp's
 *                  find_func(), parse.c). Either way, the output is the
 *                  same.
 */
#ifndef CCISH_SMALL
#define CCISH_LISTING
#define CCISH_CHECKS
#define CCISH_TEXT
#endif

#endif
