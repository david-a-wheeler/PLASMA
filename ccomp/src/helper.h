/*
 * ccgen's helper functions (helper.c). Include after obj.h.
 */
#ifndef HELPER_H
#define HELPER_H

/* Helper functions (approach.md, "unsigned and helper functions
 * design"): code ccgen writes into each object that uses it, as an H
 * record, of which ccrel keeps one copy. A call to helper k is a CALL
 * with relocation REL_HELPER, target k. In ccgen, helper k is function
 * number HELPER_FN(k) while it is lowered (helper.c). */
#define HELPER_FN(k) (MAX_FUNCS + (k))
extern int helper_used;             /* bit k: helper k is called */
void gen_helpers(void);             /* write the helpers used */
const char *helper_name(int k);

#endif
