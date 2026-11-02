/*
 * cccomp's expressions (pexpr.c): what the parser's other files
 * need of it (approach.md, "parse.c split design"). Include after
 * parse.h, whose Operand it uses.
 */
#ifndef PEXPR_H
#define PEXPR_H

/* Precedence climbing: parse an expression whose operators all have
 * precedence min_prec or higher. The result is left in *o, possibly
 * still pending (a bare variable). */
void parse_expr(int min_prec, Operand *o);

/* As parse_expr, but for code that can never run (the x in 0 && x):
 * check it, but emit nothing. */
void parse_skipped(int min_prec, Operand *o);

/* Check a value *o against an assignment to, or an argument of, type
 * to, as C requires (the rules in pexpr.c's check_assign). where is
 * a message part saying where: "assignment", or "argument" with a
 * detail. */
void check_assign(int to, Operand *o, int where, const char *detail);

/* Convert the value *o to char: keep the low byte. A constant stays
 * pending; anything else ends up on the stack. */
void to_char(Operand *o);

/* Copy the struct r into the place o (the how in pexpr.c's
 * copy_struct). */
void copy_struct(Operand *o, Operand *r, int start);

/* Push the struct o by value: the words of its bytes, first word
 * deepest (the how in pexpr.c's push_struct_words). */
void push_struct_words(Operand *o);

/* Push zero for a function of return type t that ends without a
 * value: as many words as it returns, so that the stack stays
 * balanced. */
void push_zero_result(int t);

/* Report an error if t is a struct too big to pass or return by
 * value (MAX_BYVAL bytes), or an incomplete one; is_param says
 * which. */
void check_byval(int t, int is_param);

#endif
