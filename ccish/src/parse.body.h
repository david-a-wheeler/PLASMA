// The parser's state for function bodies, in parse.c, for the files
// that parse expressions and statements (parse.c, pexpr.c, pstmt.c).
// Part of the parser's header, with parse.h: apart, so that ptop.c,
// which doesn't use these, needn't see them (every variable a file can
// see counts against cccomp's MAX_VARS when ccish compiles itself).
// Include after ir.h and parse.h.
#ifndef PARSE_BODY_H
#define PARSE_BODY_H

extern char var_flags[MAX_VARS];  // the depth, plus the V_* bits
#define MAX_NEST     32     // limit function: nesting of parentheses, blocks
                            // and loops
extern int nest;            // nesting depth, to limit recursion
#define MAX_HELD      8     // left operands held back at once
extern int nheld;          // left operands held back, so far
extern int suppress;       // parsing code that can never run
extern char label_used[MAX_LABELS];  // some branch to it so far

#endif
