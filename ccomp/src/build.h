/*
 * ccish's builds (approach.md decision 21): the feature macros each
 * build has. Include first.
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
 * And one that neither build has: tools/measure.py builds a ccpp with
 *   CCISH_STATS    report how full ccpp's tables got, for sizing them.
 */
#ifndef CCISH_SMALL
#define CCISH_LISTING
#define CCISH_CHECKS
#define CCISH_TEXT
#endif

#endif
