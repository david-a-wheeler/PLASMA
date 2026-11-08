/*
 * cccomp's front end, the expression grammar (approach.md,
 * "parse.c split design"): primaries, the unary and postfix
 * operators, the binary operators with their type checks and
 * constant folding, the comparisons, the logical and conditional
 * operators, the assignments (with the struct copies), calls, and
 * parse_expr, the entry the statements and initializers use.
 */

#ifdef CCISH_STATS
#include <stdio.h>
#endif
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "fixed.h"
#include "tokin.h"
#include "obj.h"
#include "ir.h"
#include "name.h"
#include "type.h"
#include "parse.h"
#include "irout.h"
#include "pstmt.h"
#include "pexpr.h"
static void parse_unary(Operand *o);
static void parse_postfix(Operand *o);
static void parse_sizeof(Operand *o);
static void binary(Operand *o, Operand *r, int op, int lstart, int mid);
static void offset_place(Operand *o, int off);

/* Do pointer types a and b point to the same type, apart from that
 * type's own qualifiers (const char * and char *)? */
static int same_target(int a, int b)
{
  return unqual(type_sub(a)) == unqual(type_sub(b));
}

/* Is pointer type t a pointer to void (with any qualifiers)? */
static int is_void_ptr(int t)
{
  return unqual(type_sub(t)) == TY_VOID;
}

/* Can pointer types a and b be mixed (in ==, != or ?:)? They can if
 * they point to the same type, whatever its qualifiers, or if either
 * is a void *. */
static int compatible(int a, int b)
{
  return same_target(a, b) || is_void_ptr(a) || is_void_ptr(b);
}

#ifdef CCISH_DIAG
/* The argument parse_args() has check_assign() check: its number and
 * its function, spelled ("1 of 'f'") only for a message, since the
 * spelling comes from the name file, which a correct compile never
 * reads (CCPP.md, "The stamp"). */
static int arg_no, arg_fn;

/* The detail of a message from check_assign(): its own, or for an
 * argument (where "argument", with none), "N of 'f'". */
static const char *assign_detail(int where, const char *detail)
{
  static char buf[MAX_NAME + 16];
  if (detail || where != 200 /* "argument" */)
    return detail;
  strcpy(buf, num_text(arg_no));
  strcat(buf, " of ");
  strcat(buf, quote(fn_name[arg_fn]));
  return buf;
}
#else
#define assign_detail(where, detail) (detail)
#endif

/* Check that o can be assigned (or passed, or returned) to type to, as
 * C requires: integers and pointers don't mix without a cast, except
 * the constant 0, and pointer types must be compatible. A pointer may
 * gain qualifiers on what it points to (char * to const char *), but
 * losing one is a warning, as in gcc: C requires a diagnostic, and the
 * program may still be right. A struct needs the same struct type.
 * where is a message part saying where ("assignment", or "argument"
 * with a detail such as "1 of 'f'"). */
void check_assign(int to, Operand *o, int where,
                         const char *detail)
{
  int from = value_type(o->type);
  if (o->kind == K_VOID)
    error(38 /* "void value used in an expression" */, NULL);
  if (is_struct(to) || is_struct(from)) {
    if (unqual(to) != unqual(from))
      error_in(46 /* "incompatible types in" */, where,
               assign_detail(where, detail));
    return;
  }
  if (is_pointer(to)) {
    if (!is_pointer(from)) {
      if (!is_null(o))
        error_in(47 /* "pointer from integer without a cast in" */,
                 where, assign_detail(where, detail));
    } else if (!compatible(from, to)) {
      error_in(48 /* "incompatible pointer types in" */, where,
               assign_detail(where, detail));
    } else {
#ifdef CCISH_DIAG
      int lost = type_sub(from) & ~type_sub(to) & Q_ALL;
      if (lost & Q_CONST)
        warning_in(49 /* "'const' qualifier discarded from pointer "
                      "target type in" */, where,
                   assign_detail(where, detail));
      if (lost & Q_VOLATILE)
        warning_in(50 /* "'volatile' qualifier discarded from pointer "
                      "target type in" */, where,
                   assign_detail(where, detail));
#endif
    }
  } else if (is_pointer(from)) {
    error_in(51 /* "integer from pointer without a cast in" */,
             where, assign_detail(where, detail));
  }
}

/* Make o's value C's truth value: 1 if it is nonzero, else 0. A
 * constant stays pending; anything else ends up on the stack. */
static void to_bool(Operand *o)
{
  if (o->kind == K_CONST) {
    o->val = o->val != 0;
    o->type = TY_INT;
    return;
  }
  rvalue(o);
  if (!o->is_bool) {
    put(IR_CONST, 0, -1);
    put(IR_NE, 0, -1);
    o->is_bool = 1;
  }
  o->type = TY_INT;
}

/* Convert o to char, as C does for a cast or a char function's return
 * value: keep the low byte. A constant stays pending; anything else
 * ends up on the stack. A char, or a 1 or 0, needs no code. */
void to_char(Operand *o)
{
  if (o->kind == K_CONST) {
    o->val &= 0xFF;
  } else {
    rvalue(o);
    if (o->type != TY_CHAR && !o->is_bool) {
      put(IR_CONST, 0xFF, -1);
      put(IR_AND, 0, -1);
    }
  }
  o->type = TY_CHAR;
}

/* Apply the cast (type) to o. A cast's result is never an lvalue, so
 * the type's own qualifiers don't matter ((const int)x is an int). To
 * void, the value is discarded. Between integers and pointers, the
 * value is unchanged (both are 16 bits). */
static void cast(Operand *o, int type)
{
  type = unqual(type);
  if (is_struct(type))
    error(52 /* "a cast to a struct type isn't allowed" */, NULL);
  if (type == TY_VOID) {
    discard(o);
    o->kind = K_VOID;
  } else if (type == TY_CHAR) {
    to_char(o);
  } else if (is_pointer(type) && (addr_const(o) || o->kind == K_FUNC)) {
    ;                           /* still an address constant */
  } else if (o->kind != K_CONST) {
    rvalue(o);
  }
  o->type = type;
}

/* Parse an expression whose code can never run (such as the x in
 * 0 && x): check it, but emit nothing. */
void parse_skipped(int min_prec, Operand *o)
{
  int saved = nheld;
  suppress++;
  parse_expr(min_prec, o);
  suppress--;
  nheld = saved;      /* drop anything held inside it */
}

/*
 * Binary operators: precedence and IR op. Returns 0 if tok is not a
 * binary operator. Sets *supported to 0 for C operators we don't
 * support yet; they still get their real precedence, so that, say, a
 * ',' ending a function argument isn't mistaken for an error.
 */
static int binary_prec(int *ir_op, int *supported)
{
  *supported = 1;
  switch (tok) {
  case '*':   *ir_op = IR_MUL; return PREC_MUL;
  case '/':   *ir_op = IR_DIV; return PREC_MUL;
  case '%':   *ir_op = IR_MOD; return PREC_MUL;
  case '+':   *ir_op = IR_ADD; return PREC_ADD;
  case '-':   *ir_op = IR_SUB; return PREC_ADD;
  case T_SHL: *ir_op = IR_SHL; return PREC_SHIFT;
  case T_SHR: *ir_op = IR_SHR; return PREC_SHIFT;
  case '&':   *ir_op = IR_AND; return PREC_AND;
  case '^':   *ir_op = IR_XOR; return PREC_XOR;
  case '|':   *ir_op = IR_OR;  return PREC_OR;
  case '<':   *ir_op = IR_LT;  return PREC_RELATIONAL;
  case '>':   *ir_op = IR_GT;  return PREC_RELATIONAL;
  case T_LE:  *ir_op = IR_LE;  return PREC_RELATIONAL;
  case T_GE:  *ir_op = IR_GE;  return PREC_RELATIONAL;
  case T_EQ:  *ir_op = IR_EQ;  return PREC_EQUALITY;
  case T_NE:  *ir_op = IR_NE;  return PREC_EQUALITY;
  /* These three are parsed specially, not with an IR op. */
  case T_ANDAND: *ir_op = -1;  return PREC_ANDAND;
  case T_OROR:   *ir_op = -1;  return PREC_OROR;
  case '?':      *ir_op = -1;  return PREC_COND;
  }
  *supported = 0;
  if (tok == ',')
    return PREC_COMMA;
  return 0;
}

/* Assignment operators: returns 1 if tok is one. *ir_op is the
 * arithmetic for a compound assignment, or -1 for plain '='. */
static int assign_op(int *ir_op)
{
  switch (tok) {
  case '=':     *ir_op = -1;     return 1;
  case T_ADDEQ: *ir_op = IR_ADD; return 1;
  case T_SUBEQ: *ir_op = IR_SUB; return 1;
  case T_MULEQ: *ir_op = IR_MUL; return 1;
  case T_DIVEQ: *ir_op = IR_DIV; return 1;
  case T_MODEQ: *ir_op = IR_MOD; return 1;
  case T_ANDEQ: *ir_op = IR_AND; return 1;
  case T_OREQ:  *ir_op = IR_OR;  return 1;
  case T_XOREQ: *ir_op = IR_XOR; return 1;
  case T_SHLEQ: *ir_op = IR_SHL; return 1;
  case T_SHREQ: *ir_op = IR_SHR; return 1;
  }
  return 0;
}

/*
 * Push the struct o by value (approach.md, "M6 design"): the words of
 * its bytes, first word deepest; a last odd byte alone, as a word. As
 * parameters, ENTER stores them in consecutive frame words, so they
 * land in the struct's own layout: the parameter is an ordinary local
 * struct, with no copy. A struct that is already on the stack (a call's
 * result) needs nothing. One reached through an address on the stack
 * is spilled first, unless it is a single word.
 */
void push_struct_words(Operand *o)
{
  Operand v;
  int n = type_size(o->type), k;
  if (o->kind == K_VALUE)
    return;
  if (o->kind == K_DEREF && n > 2)
    spill(o);
  for (k = 0; k < n; k += 2) {
    v = *o;
    offset_place(&v, k);
    v.type = n - k == 1 ? TY_CHAR : TY_INT;
    rvalue(&v);
  }
  o->kind = K_VALUE;
}

/* Store the struct on the stack (o, a K_VALUE) into frame temporaries,
 * last word first, so that it can be used as an object: o becomes a
 * variable, but not an lvalue (f().x is readable, not assignable). */
static void materialize(Operand *o)
{
  int k, w = struct_words(o->type), t = new_temp();
  for (k = 1; k < w; k++)
    new_temp();                 /* the next words, right after t */
  for (k = w - 1; k >= 0; k--) {
    put(IR_SETLOCAL, t + 2 * k, -1);
    put(IR_DROP, 0, -1);
  }
  o->kind = K_VAR;
  o->off = t;
  o->global = 0;
  o->assigned = 1;
}

/* Push zero for a function of return type t that ends without a value:
 * as many words as it returns, so that the stack stays balanced. */
void push_zero_result(int t)
{
  int k, w = result_words(t);
  for (k = 0; k < w; k++)
    put(IR_CONST, 0, -1);
}

/* Report an error if t is a struct too big to pass or return by value
 * (MAX_BYVAL bytes), or an incomplete one; is_param says which. */
void check_byval(int t, int is_param)
{
  if (!is_struct(t))
    return;
  need_complete(t, is_param
                   ? 210 /* "a parameter can't have the incomplete type" */
                   : 211 /* "a return value can't have the incomplete"
                            " type" */);
  if (type_size(t) > MAX_BYVAL)
    error(is_param ? 212 /* "a struct over 8 bytes can't be passed by"
                            " value: use a pointer to it" */
                   : 213 /* "a struct over 8 bytes can't be returned by"
                            " value: return it through a pointer"
                            " parameter" */, NULL);
}

/* Calls emitted so far. A compound assignment compares it before and
 * after its right side, to see whether that has a call. */
static int ncalls;

/* Parse a call's arguments, starting at '(', and emit them; f is the
 * function called, or -1 for a call through a pointer, whose parameter
 * types aren't known (approach.md, "Function pointer design"). Checks
 * their types if f's are known. Returns their count. */
static int parse_args(int f)
{
  Operand a;
  int nargs = 0;
  next();                         /* '(' */
  if (tok != ')') {
    for (;;) {
      parse_expr(PREC_ASSIGN, &a);
      if (f >= 0 && fn_ptypes[f] >= 0 && nargs < nparams_of(f)) {
#ifdef CCISH_DIAG
        arg_no = nargs + 1;
        arg_fn = f;
#endif
        check_assign(param_types[fn_ptypes[f] + nargs], &a,
                     200 /* "argument" */, NULL);
      }
      if (is_struct(a.type)) {
        if (f >= 0 && fn_ptypes[f] < 0)
          error(53 /* "passing a struct needs a prototype for" */,
                quote(fn_name[f]));
        push_struct_words(&a);
      } else {
        rvalue(&a);
      }
      nargs++;
      if (tok != ',')
        break;
      next();
    }
  }
  expect(')', 54 /* "expected ')' after arguments, not" */);
  if (!suppress)
    ncalls++;
  return nargs;
}

/* Parse a call to fn, starting at '(': emit the arguments, check their
 * count (and their types, if fn's parameter types are known), then
 * emit the call. */
static void parse_call(int f, Operand *o)
{
  int nargs = parse_args(f);
  if (nparams_of(f) < 0)
    set_nparams(f, nargs);      /* first use fixes the count */
  else if (nparams_of(f) != nargs)
    error(55 /* "wrong number of arguments in call to" */,
          quote(fn_name[f]));
  fn_flags[f] |= FN_CALLED;
  put(IR_CALL, nargs, f);
  o->kind = fn_type[f] != TY_VOID ? K_VALUE : K_VOID;
  o->type = fn_type[f];       /* a struct: its words are on the stack */
}

/* Can a value of type t be called: is it a function, or a pointer to
 * one? */
static int callable(int t)
{
  return is_func(t) || (is_pointer(t) && is_func(type_sub(t)));
}

/* Parse a call of o, starting at '(': o is a function, or a pointer to
 * one (approach.md, "Function pointer design"). A named function (f,
 * *f, &f) is called directly. Otherwise the call is ICAL, which takes
 * the address on top of the arguments: a callee whose value needs no
 * code yet (a variable, an element, a constant) is loaded after them,
 * and any other is kept in a temporary meanwhile. Nothing is known of
 * the parameters, so the arguments are passed as they are. */
static void parse_icall(Operand *o)
{
  int t = o->type, nargs, tmp = -1, r;
  if (o->kind == K_FUNC) {
    parse_call(o->val, o);
    return;
  }
  if (is_pointer(t))
    t = type_sub(t);
  if (o->kind != K_VAR && o->kind != K_ELEM && o->kind != K_CONST
      && o->kind != K_ADDR) {
    rvalue(o);
    tmp = new_temp();
    put(IR_SETLOCAL, tmp, -1);
    put(IR_DROP, 0, -1);
  }
  nargs = parse_args(-1);
  if (tmp >= 0)
    put(IR_LOCAL, tmp, -1);
  else
    rvalue(o);
  r = unqual(type_sub(t));
  put(IR_ICALL, nargs, result_words(r));
  o->kind = r != TY_VOID ? K_VALUE : K_VOID;
  o->type = r;                /* a struct: its words are on the stack */
  o->quals = 0;
  o->is_bool = 0;
  o->assigned = 0;
}

/* pstr("literal"): the address of the literal's PLASMA (length-prefixed)
 * form, which is the byte just before its first character. */
static void parse_pstr(void)
{
  int off;
  next();                         /* '(' */
  if (tok != T_STR)
    error(56 /* "pstr() needs a string literal, not" */, tok_desc());
  off = suppress ? 1 : data_string(tok_text, tok_len);
  next();
  expect(')', 58 /* "expected ')' after pstr argument, not" */);
  put(IR_STRADDR, off - 1, -1);
}

/* Set while parsing the operand of prefix &, ++ or --, which must stay a
 * place: the next primary expression that isn't in parentheses clears
 * it. Then a const variable isn't folded to its value. */
static int want_place;

/* Parse a primary expression: a constant, string, variable, call, or
 * expression in parentheses. Constants and variables stay pending. A
 * cast is parsed here too, since it also starts with '('.
 *
 * Reading a const variable with a constant initializer gives its value
 * at compile time (V_KNOWN), a K_CONST, so const int N = 10; makes
 * x < N use CB 10. Not when it is a place: assigned to (an error, made
 * by need_lvalue()), incremented, or the operand of &, so &N works. */
static void parse_primary(Operand *o)
{
  int name, c, f, v, place = want_place;

  if (tok != '(')
    want_place = 0;
  o->kind = K_VALUE;
  o->type = TY_INT;
  o->quals = 0;
  o->nonice = 0;
  o->is_bool = 0;
  o->assigned = 0;
  switch (tok) {
  case T_NUM:
    o->kind = K_CONST;          /* emit it later, if needed */
    o->val = wrap16((unsigned)tok_val);
    if (tok_unsigned)
      o->type = TY_UINT;
    next();
    return;
  case T_STR:
    if (in_sizeof) {
      /* In C a literal is a char array, so sizeof "hi" is 3: here
       * it is that array, at an address on the stack. Elsewhere
       * it is just a char *, so that literals don't each add a
       * type (an array of their length) to the type table. */
      o->kind = K_DEREF;
      o->type = array_of(TY_CHAR, tok_len + 1);
      next();
      return;
    }
    /* An address constant. A literal in code that can never run
     * takes no data space, unless it is in a global's initializer. */
    o->kind = K_ADDR;
    o->global = G_STR;
    o->off = o->disp = suppress && !want_addr
                       ? 0 : data_string(tok_text, tok_len);
    o->val = o->off + tok_len + 1;
    o->type = pointer_to(TY_CHAR);
    next();
    return;
  case '(':
    enter_nest();
    next();
    if (is_type_start()) {
      /* A cast: (type) unary-expression. */
      c = parse_type_name();
      expect(')', 59 /* "expected ')' after the type in a cast, not" */);
      parse_unary(o);
      cast(o, c);
      nest--;
      return;
    }
    /* The result may still be a pending variable: (x) = 1 is valid. */
    parse_expr(PREC_COMMA, o);
    expect(')', 60 /* "expected ')', not" */);
    nest--;
    return;
  case T_ID:
    name = tok_val;
    next();
    c = find_id(name);
    if (c >= 0) {
      if (id_kind(c) == ID_TYPEDEF)
        error(61 /* "a type name can't be used as a value:" */,
              quote(name));
      if (tok == '(')
        error(62 /* "called object is not a function:" */,
              quote(name));
      o->kind = K_CONST;      /* an enum constant */
      o->val = id_val(c);
      return;
    }
    v = find_var(name);
    if (tok == '(' && v >= 0 && !callable(var_type[v]))
      error(62 /* "called object is not a function:" */, quote(name));
    if (tok == '(' && v < 0) {
      if (name == ID_PSTR) {
        parse_pstr();
        o->type = pointer_to(TY_CHAR);
        return;
      }
      f = find_func(name);
      if (f < 0) {
        /* No implicit declarations (approach.md, decision 20):
         * only the CMDSYS table declares a function by itself. */
        c = find_cmdsys(name);
        if (c < 0)
          error(215 /* "function called without a declaration:" */,
                quote(name));
        f = add_func(name);
        declare_cmdsys(f, c);
      }
      parse_call(f, o);
      return;
    }
    if (v >= 0 && (var_flags[v] & V_KNOWN) && !place && !assign_op(&c)
        && tok != T_INC && tok != T_DEC) {
      o->kind = K_CONST;      /* a const variable's known value */
      o->type = unqual(var_type[v]);
      o->val = var_val[v];
      o->nonice = 1;
      return;
    }
    if (v >= 0) {
      o->kind = K_VAR;        /* load it later, if needed */
      o->type = unqual(var_type[v]);
      o->quals = var_type[v] & Q_ALL;
      /* A zero-filled global is reached through its symbol. */
      o->off = var_link[v] & VL_BSS ? 0 : var_off[v];
      o->global = var_where(v);
      return;
    }
    f = find_func(name);
    if (f < 0 && (c = find_cmdsys(name)) >= 0) {
      f = add_func(name);       /* fp = __puti: declared by use */
      declare_cmdsys(f, c);
    }
    if (f >= 0) {
      o->kind = K_FUNC;         /* a function designator */
      o->val = f;
      o->type = func_of(fn_type[f]);
      return;
    }
    error(64 /* "undeclared identifier" */, quote(name));
    return;
  case T_KEYWORD:
    not_yet();
    return;
  }
  error(65 /* "expected an expression, not" */, tok_desc());
}

/* Check that lvalue o can be the operand of ++ or --, and get it ready:
 * push its value, leaving o itself ready to be stored into. Returns
 * the amount to add: the element size for a pointer, else 1. */
static int start_incdec(Operand *o)
{
  Operand v;
  need_lvalue(o, 195 /* "operand of ++ or --" */);
  spill(o);
  v = *o;
  rvalue(&v);
  return is_pointer(o->type) ? elem_size(o->type) : 1;
}

/* ++x / --x: x = x + 1, and the value is the new x. */
static void pre_incdec(Operand *o, int ir_op)
{
  put(IR_CONST, start_incdec(o), -1);
  put(ir_op, 0, -1);
  assign_to(o);
  o->is_bool = 0;
}

/* x++ / x--: the value is the old x. Keep a copy of it below the
 * update; when the value is unused, the peephole stage removes the
 * copy. */
static void post_incdec(Operand *o, int ir_op)
{
  int step = start_incdec(o);
  put(IR_DUP, 0, -1);
  put(IR_CONST, step, -1);
  put(ir_op, 0, -1);
  assign_to(o);
  discard(o);                     /* the new value */
  o->kind = K_VALUE;
  o->is_bool = 0;
  o->assigned = 0;
}

/* Make the place o an object of type t (a pointer's target, or an
 * array's element), which may be qualified: o's type is t without its
 * qualifiers, and o->quals keeps them (*p is const if p is a const
 * char *). */
static void set_object(Operand *o, int t)
{
  o->type = unqual(t);
  o->quals = t & Q_ALL;
}

/* Apply unary * to o: the result is the object o points to. */
static void deref(Operand *o)
{
  int t = o->type;
  if (o->kind == K_VOID)
    error(38 /* "void value used in an expression" */, NULL);
  if (is_func(t))
    return;                     /* *f is f */
  if (callable(t)) {
    /* The function fp points to, whose address is fp's value: still
     * pending, or on the stack (parse.h). */
    if (o->kind == K_STORE)
      rvalue(o);
    o->type = type_sub(t);
    o->quals = 0;
    return;
  }
  if (is_array(t) && is_place(o)) {
    /* *a is a[0]: the same address, with the element's type. */
    set_object(o, type_sub(t));
    return;
  }
  if (!is_pointer(t))
    error(66 /* "unary '*' needs a pointer operand" */, NULL);
  if (is_void_ptr(t))
    error(67 /* "dereferencing a 'void *'" */, NULL);
  if (o->kind == K_VAR) {
    make_elem(o, o->off, o->global, 0);     /* *p: the address is in p */
  } else {
    rvalue(o);
    o->kind = K_DEREF;
    o->is_bool = 0;
    o->assigned = 0;
  }
  set_object(o, type_sub(t));
}

/* Can o take an index (a variable one if var is set, else a constant)
 * and stay pending? It can if it is an array or pointer variable (local
 * or global), or a K_ELEM of array type (a row of a multi-dimensional
 * array) that has no variable index yet, since a K_ELEM holds only one. */
static int pending_base(Operand *o, int var)
{
  if (o->kind == K_VAR)
    return is_array(o->type) || is_pointer(o->type);
  return o->kind == K_ELEM && is_array(o->type) && !(var && o->idx >= 0);
}

/* Parse "[index]" after o: o becomes the element o[index], which C
 * defines as *(o + index). When o is a pending base and the index is a
 * plain variable or a number, nothing is emitted: the element is a
 * K_ELEM (or, for a constant index on an array variable, a K_VAR at a
 * fixed offset). Whether the index is that simple is decided before it
 * is parsed, by looking for ']' after its first token, since otherwise
 * the base's address must be emitted first, below the index. */
static void parse_index(Operand *o)
{
  Operand r;
  int t = o->type, simple, size, d, start = ir_here(), h = nheld, mid;

  next();                         /* '[' */
  simple = (tok == T_NUM || tok == T_ID) && pending_base(o, tok == T_ID)
           && next_is_rbracket();
  if (!simple)
    rvalue(o);
  start = code_start(start, h);   /* o's code, for i[p] (binary()) */
  mid = ir_here();
  parse_expr(PREC_COMMA, &r);
  expect(']', 68 /* "expected ']' after an array index, not" */);
  if (!simple) {
    if (!is_pointer(o->type) && !is_pointer(value_type(r.type)))
      error(69 /* "subscripted value is neither array nor pointer" */,
            NULL);
    if (is_pointer(o->type) && is_pointer(value_type(r.type)))
      error(70 /* "array subscript is not an integer" */, NULL);
    binary(o, &r, IR_ADD, start, mid);
    deref(o);
    return;
  }
  if (is_pointer(value_type(r.type)))
    error(70 /* "array subscript is not an integer" */, NULL);
  if (r.kind != K_CONST && r.kind != K_VAR)
    fatal(71 /* "internal error: unexpected array index" */, NULL);
  size = is_pointer(t) ? elem_size(t) : type_size(type_sub(t));
  if (r.kind == K_CONST) {
    d = wrap16((unsigned)r.val * (unsigned)size);
    if (o->kind == K_VAR && is_array(t) && o->off + d >= 0
        && o->off + d <= max_offset(o->global)) {
      o->off += d;            /* a[2]: a variable at a fixed offset */
    } else {
      if (o->kind == K_VAR)
        make_elem(o, o->off, o->global, is_array(t));
      o->disp = wrap16((unsigned)o->disp + (unsigned)d);
    }
  } else {
    if (o->kind == K_VAR)
      make_elem(o, o->off, o->global, is_array(t));
    o->idx = r.off;
    o->idx_global = r.global;
    o->idx_char = r.type == TY_CHAR;
    o->scale = size;
  }
  set_object(o, type_sub(t));
}

/* Apply unary & to o: the result is its address. */
static void address_of(Operand *o)
{
  if (is_func(o->type)) {
    /* &f, &*fp: the function's address, which is its value. */
    if (o->kind == K_FUNC)
      o->type = pointer_to(o->type);
    else
      rvalue(o);
    return;
  }
  if (o->assigned)
    error_in(41 /* "lvalue required as" */,
             196 /* "unary '&' operand" */, NULL);
  if (o->kind == K_VAR && o->global)
    o->kind = K_ADDR;               /* &g, &a[2], &s.x: a constant */
  else if (o->kind == K_VAR)
    push_var_addr(o->off, o->global);
  else if (o->kind == K_ELEM)
    push_address(o);                /* &*p is p, &a[i] is a + i */
  else if (o->kind != K_DEREF)        /* K_DEREF: already on the stack */
    error_in(41 /* "lvalue required as" */,
             196 /* "unary '&' operand" */, NULL);
  if (o->kind != K_ADDR)
    o->kind = K_VALUE;
  o->type = pointer_to(o->type | o->quals);   /* &N is a const int * */
  o->quals = 0;
  o->is_bool = 0;
}

/* Move the place o off bytes further on, for a member or a part of a
 * struct: a variable stays a variable at a new offset if it can, a
 * K_ELEM adds to its displacement, and a K_DEREF adds to its address.
 * Its type is unchanged; the caller sets it. */
static void offset_place(Operand *o, int off)
{
  int assigned = o->assigned;
  if (o->kind == K_VAR && o->off + off <= max_offset(o->global)) {
    o->off += off;
  } else if (o->kind == K_VAR) {
    make_elem(o, o->off, o->global, 1);
    o->disp = off;
    o->assigned = assigned;
  } else if (o->kind == K_ELEM) {
    o->disp = wrap16((unsigned)o->disp + (unsigned)off);
  } else if (off != 0) {          /* K_DEREF: the address is on the stack */
    put(IR_CONST, off, -1);
    put(IR_ADD, 0, -1);
  }
}

/* Parse ".member" or "->member" (arrow set) after o: o becomes the
 * member, a place at a constant offset in the struct (approach.md, "M6
 * design"). A member of a variable is a variable at a fixed offset
 * (s.m: LLW s+off), and one reached through a pointer adds the offset
 * as a displacement (p->m: LLW p CB off ADD LW). A member of a const
 * struct is const. For a constant pointer, such as a memory-mapped
 * I/O block, the offset is added at compile time. */
static void parse_member(Operand *o, int arrow)
{
#ifdef CCISH_DIAG
  char msg[2 * MAX_NAME + 16];
#endif
  int t = arrow ? value_type(o->type) : o->type, tag, m, off;

  next();                         /* '.' or '->' */
  if (arrow && !(is_pointer(t) && is_struct(type_sub(t))))
    error(72 /* "'->' needs a pointer to a struct or union" */, NULL);
  if (!arrow && !is_struct(t))
    error(73 /* "'.' needs a struct or union" */, NULL);
  if (!arrow && o->kind == K_VALUE)
    materialize(o);             /* f().x */
  tag = struct_tag(arrow ? type_sub(t) : t);
  if (tok != T_ID)
    error(74 /* "expected a member name, not" */, tok_desc());
  if (tag_size(tag) < 0)
    error(75 /* "member access into the incomplete type" */,
          tag_desc(tag));
  m = member_find(tag, tok_val);
  if (m < 0) {
#ifdef CCISH_DIAG
    strcpy(msg, quote(tok_val));
    strcat(msg, " in ");
    strcat(msg, tag_desc(tag));
    error(214 /* "no member named" */, msg);
#else
    error(214 /* "no member named" */, NULL);
#endif
  }
  next();
  off = member_off(m);
  if (arrow && o->kind == K_CONST) {
    o->val = wrap16((unsigned)o->val + (unsigned)off);
    off = 0;
  }
  if (arrow)
    deref(o);
  offset_place(o, off);
  set_object(o, qualify(member_type(m), o->quals));
}

/* Parse a unary expression: prefix operators, then a primary
 * expression, then postfix ++ and --. */
static void parse_unary(Operand *o)
{
  int t = tok;
  switch (t) {
  case '-': case '+': case '~': case '!':
    next();
    parse_unary(o);
    if (t != '!' && is_pointer(value_type(o->type)))
      error(76 /* "pointer operand to unary" */, t == '-' ? "'-'"
            : t == '+' ? "'+'" : "'~'");
    if (o->kind == K_CONST) {
      if (t == '-')
        o->val = wrap16(0u - (unsigned)o->val);
      else if (t == '~')
        o->val = wrap16(~(unsigned)o->val);
      else if (t == '!')
        o->val = !o->val;
      o->type = t == '!' ? TY_INT : promoted(o->type);
      return;
    }
    if (t == '!' && o->kind == K_JUMP) {
      /* Swap the outcomes: the true label (or a new one, G) becomes
       * the false label, reached also by BRNCH G where true fell
       * through; then the old false label, where true now falls
       * through. */
      int g = o->off >= 0 ? o->off : new_label();
      branch(IR_JUMP, g);
      place_forward(o->val);
      o->val = g;
      o->off = -1;
      return;
    }
    rvalue(o);
    /* A char operand is promoted; ! gives an int. */
    o->type = t == '!' ? TY_INT : promoted(o->type);
    if (t == '-')
      put(IR_NEG, 0, -1);
    else if (t == '~')
      put(IR_COMP, 0, -1);
    else if (t == '!')
      put(IR_NOT, 0, -1);
    if (t != '+')
      o->is_bool = t == '!';
    return;
  case T_INC: case T_DEC:
    next();
    want_place = 1;             /* ++N: an error, not ++10 */
    parse_unary(o);
    pre_incdec(o, t == T_INC ? IR_ADD : IR_SUB);
    return;
  case '*': case '&':
    next();
    want_place = t == '&';      /* &N: N's address, not 10's */
    parse_unary(o);
    if (t == '*')
      deref(o);
    else
      address_of(o);
    return;
  case T_SIZEOF:
    next();
    parse_sizeof(o);
    return;
  }
  parse_primary(o);
  parse_postfix(o);
}

/* Parse any postfix operators after the operand o: ++, --, [], ., ->
 * and a call's "(" (a named function's call is parsed with the name,
 * in parse_primary()). */
static void parse_postfix(Operand *o)
{
  int t;
  for (;;) {
    switch (tok) {
    case T_INC:
    case T_DEC:
      t = tok;
      next();
      post_incdec(o, t == T_INC ? IR_ADD : IR_SUB);
      continue;
    case '[':
      parse_index(o);
      continue;
    case '.': case T_ARROW:
      parse_member(o, tok == T_ARROW);
      continue;
    case '(':
      if (!callable(o->type))
        error(63 /* "called object is not a function or function"
                    " pointer" */, NULL);
      parse_icall(o);
      continue;
    }
    return;
  }
}

/* Parse the operand of sizeof, after "sizeof": a type name in
 * parentheses, or a unary expression, which is checked but not run (it
 * emits nothing). o becomes the size, an int constant. */
static void parse_sizeof(Operand *o)
{
  int t, saved = nheld;
  enter_nest();
  suppress++;
  in_sizeof++;
  if (tok == '(') {
    next();
    if (is_type_start()) {
      /* A type name: a base type and an abstract declarator. */
      t = parse_type_name();
      expect(')', 77 /* "expected ')' after the type in sizeof, not" */);
    } else {
      /* A parenthesized expression, which may be followed by
       * postfix operators: sizeof (a)[0] is the size of (a)[0]. */
      parse_expr(PREC_COMMA, o);
      expect(')', 60 /* "expected ')', not" */);
      parse_postfix(o);
      t = o->type;
    }
  } else {
    parse_unary(o);
    t = o->type;
  }
  suppress--;
  in_sizeof--;
  nheld = saved;              /* drop anything held inside it */
  nest--;
  if (unqual(t) == TY_VOID)
    error(78 /* "sizeof can't be applied to type void" */, NULL);
  if (is_struct(t) && type_size(t) == 0)
    error(79 /* "sizeof can't be applied to the incomplete type" */,
          tag_desc(struct_tag(t)));
  if (is_array(t) && type_count(t) == 0)    /* extern int a[]; */
    error(79 /* "sizeof can't be applied to the incomplete type" */,
          NULL);
  o->kind = K_CONST;
  o->val = type_size(t);
  o->type = TY_UINT;          /* C's size_t */
  o->nonice = 0;              /* sizeof N is a constant expression */
  o->is_bool = 0;
}

/*
 * Parse the right side of && or || (the current token), with *o as the
 * left side, which binds at precedence prec. The result is a K_JUMP
 * with false label F (approach.md C4, "Conditions as jumps"):
 *   a && b:  a BRFALSE F  b BRFALSE F
 *   a || b:  a BRTRUE T   b BRFALSE F       (true label T, not placed)
 * A side that is itself a K_JUMP brings its own false label, which
 * becomes F (for && with both, a's is joined to b's); and maybe a true
 * label, placed where its true outcome goes on, or joined to T. A
 * constant left side folds.
 */
static void parse_logical(Operand *o, int prec)
{
  int is_and = tok == T_ANDAND, end, t, f;
  Operand r;

  next();
  if (o->kind == K_CONST) {
    if ((o->val != 0) != is_and) {
      /* 0 && x is 0, and 1 || x is 1: x never runs. */
      parse_skipped(prec + 1, &r);
      o->val = !is_and;
      o->type = TY_INT;
      o->nonice |= r.nonice;
      return;
    }
    /* 1 && x and 0 || x are x, as 1 or 0 (or as a condition). */
    end = o->nonice;
    parse_expr(prec + 1, &r);
    if (r.kind != K_JUMP)
      to_bool(&r);
    *o = r;
    o->nonice |= end;
    return;
  }
  if (is_and) {
    if (o->kind == K_JUMP) {
      f = o->val;
      place_true(o);                /* b runs when a is true */
    } else {
      f = new_label();
      cond_branch(o, f, 0);
    }
    parse_expr(prec + 1, &r);
    t = -1;
    if (r.kind == K_JUMP) {
      /* Keep b's false label, the one its last branch names (so that
       * a BRNCH and that label can still meet in the peephole stage),
       * and join a's to it. */
      join_label(f, r.val);
      f = r.val;
      t = r.off;
    } else {
      cond_branch(&r, f, 0);
    }
  } else {
    t = new_label();
    cond_branch(o, t, 1);
    parse_expr(prec + 1, &r);
    if (r.kind == K_JUMP) {
      f = r.val;
      join_label(r.off, t);
    } else {
      f = new_label();
      cond_branch(&r, f, 0);
    }
  }
  o->kind = K_JUMP;
  o->val = f;
  o->off = t;
  o->type = TY_INT;
  o->is_bool = 1;
}

/* Is o a void call (no value)? */
static int is_void(Operand *o)
{
  return o->kind == K_VOID;
}

/* The type of x ? b : c, given whether b and c are null pointer
 * constants. Integers give their common type, or if they differ, the
 * usual arithmetic conversions' (char and int: C promotes both). A pointer may meet the same type,
 * a void * (giving void *), or a null pointer constant. Two pointers
 * give a pointer to a type with the qualifiers of both targets: char *
 * and const char * give const char *. */
static int cond_type(Operand *b, int b_null, Operand *c, int c_null)
{
  int bt = value_type(b->type), ct = value_type(c->type), q;
  if (!is_pointer(bt) && !is_pointer(ct))
    return bt == ct ? bt : arith_type(bt, ct);
  if (bt == ct)
    return bt;
  if (is_pointer(bt) && is_pointer(ct) && compatible(bt, ct)) {
    q = (type_sub(bt) | type_sub(ct)) & Q_ALL;
    if (is_void_ptr(bt) || is_void_ptr(ct))
      return pointer_to(TY_VOID | q);
    return pointer_to(unqual(type_sub(bt)) | q);
  }
  if (is_pointer(bt) && c_null)
    return bt;
  if (is_pointer(ct) && b_null)
    return ct;
  error(80 /* "type mismatch in conditional expression" */, NULL);
  return TY_INT;
}

/*
 * Parse "? b : c" with *o as the condition. Code: "cond BRFLS Lelse b
 * BRNCH Lend Lelse: c Lend:". A constant condition picks b or c at
 * compile time, and the other is parsed but emits no code. Both sides
 * may be void calls, as in C, but not just one.
 */
static void parse_conditional(Operand *o)
{
  Operand b, c;
  int else_l, end_l, b_null, type;

  next();                         /* '?' */
  if (o->kind == K_CONST) {
    int take_b = o->val != 0;
    if (take_b)
      parse_expr(PREC_COMMA, &b);
    else
      parse_skipped(PREC_COMMA, &b);
    expect(':', 81 /* "expected ':' in conditional expression, not" */);
    if (take_b)
      parse_skipped(PREC_COND, &c);
    else
      parse_expr(PREC_COND, &c);
    if (is_void(&b) != is_void(&c))
      error(82 /* "only one side of '?:' is void" */, NULL);
    type = cond_type(&b, is_null(&b), &c, is_null(&c));
    b_null = o->nonice;
    if (take_b)
      *o = b;                   /* not a struct ?:, which ccish lacks */
    else
      *o = c;
    if (is_place(o))
      rvalue(o);              /* the result is not an lvalue */
    o->type = type;
    o->nonice |= b_null;        /* N ? 1 : 2 is no more constant */
    return;
  }
  else_l = new_label();
  end_l = new_label();
  cond_branch(o, else_l, 0);
  parse_expr(PREC_COMMA, &b);
  b_null = is_null(&b);
  if (!is_void(&b))
    rvalue(&b);
  branch(IR_JUMP, end_l);
  expect(':', 81 /* "expected ':' in conditional expression, not" */);
  place_forward(else_l);
  parse_expr(PREC_COND, &c);
  type = cond_type(&b, b_null, &c, is_null(&c));
  if (!is_void(&c))
    rvalue(&c);
  if (is_void(&b) != is_void(&c))
    error(82 /* "only one side of '?:' is void" */, NULL);
  place_forward(end_l);
  o->kind = b.kind;
  o->type = type;
  o->is_bool = b.is_bool && c.is_bool;
}

/* Is IR op one of < > <= >=? */
static int is_relational(int op)
{
  return IR_IS_RELATIONAL(op);
}

/* The C spelling of arithmetic IR op, for messages. */
static const char *op_name(int op)
{
  static const char *const names[] = {
    "'+'", "'-'", "'*'", "'/'", "'%'", "'&'", "'|'", "'^'", "'<<'",
    "'>>'"
  };
  return IR_IS_BINARY(op) ? names[op - IR_ADD] : "operator";
}

/* Push integer operand i scaled by the size of what pointer type p
 * points to, for p + i or p - i. */
static void push_scaled(Operand *i, int p)
{
  int n = elem_size(p);
  if (i->kind == K_CONST) {
    i->val = wrap16((unsigned)i->val * (unsigned)n);
    rvalue(i);
    return;
  }
  rvalue(i);
  if (n != 1) {
    put(IR_CONST, n, -1);
    put(IR_MUL, 0, -1);
  }
}

/* A comparison with a pointer operand: check the types (equality allows
 * a null pointer constant or a void *; < > <= >= need the same type),
 * then push r. Pointers are compared unsigned (binary()). */
static void compare_pointers(Operand *o, Operand *r, int op)
{
  int lt = o->type, rt = value_type(r->type);
  if (!is_pointer(lt) || !is_pointer(rt)) {
    if (is_relational(op) || !(is_null(o) || is_null(r)))
      error(83 /* "comparison between pointer and integer" */, NULL);
  } else if (is_relational(op) ? !same_target(lt, rt)
                               : !compatible(lt, rt)) {
    error(84 /* "comparison of incompatible pointer types" */, NULL);
  }
  rvalue(r);
}

/* Push r for binary op *op on integers o and r, and make *op the
 * unsigned op when the operation is unsigned: when either operand is
 * unsigned int (for a shift, the left one). < > <= >= become IR_ULT
 * and the rest, put here with the length of r's code (0 if not known:
 * no positions, or a constant left operand, written at r's start), so
 * that ccgen can flip both sign bits; / % >> become IR_UDIV, IR_UMOD,
 * IR_USHR, which the caller puts, with the a returned: 1 if r is a
 * constant (just written) that ccgen can do inline (a shift count, or a
 * power of 2 for / and %), else 0, for a call to a helper function
 * (approach.md, "unsigned and helper functions design"). */
static int unsigned_op(Operand *o, Operand *r, int *op, int mid)
{
  int shift = IR_IS_S_SHIFT(*op), k = r->kind, v = r->val;
  rvalue(r);
  if (!(shift ? is_uns(o->type)
              : is_uns(o->type) || is_uns(value_type(r->type))))
    return 0;
  if (is_relational(*op)) {
    *op += IR_ULT - IR_LT;
    put(*op, mid >= 0 && o->kind != K_CONST ? ir_here() - mid : 0, -1);
  } else if (*op == IR_DIV || *op == IR_MOD || *op == IR_SHR) {
    shift = *op == IR_SHR;
    *op = *op == IR_DIV ? IR_UDIV : *op == IR_MOD ? IR_UMOD : IR_USHR;
    v &= 0xFFFF;                /* 0x8000u is a power of 2 too */
    return k == K_CONST && (shift || (v != 0 && (v & (v - 1)) == 0));
  }
  return 0;
}

/*
 * Finish binary op: o's value is on the stack (or held, if a
 * constant), and r has been parsed. Check the operand types, scale
 * pointer arithmetic by the element size, and emit the op. The result
 * replaces *o. Pointer arithmetic is p + i, i + p, p - i and p - q.
 * o's code is from IR position lstart to mid (lstart is -1 if
 * unknown), and r's code follows it.
 */
static void binary(Operand *o, Operand *r, int op, int lstart, int mid)
{
  int lt = o->type, rt = value_type(r->type), n, a = 0;
  int result = TY_INT;

  if (r->kind == K_VOID)
    rvalue(r);                  /* an error */
  if (is_compare(op) && (is_pointer(lt) || is_pointer(rt))) {
    compare_pointers(o, r, op);
    /* < > <= >= between pointers are unsigned: IR_ULT and the rest,
     * with the length of r's code, which ccgen needs to flip both sign
     * bits (approach.md 18.7, C). */
    if (is_relational(op)) {
      put(op - IR_LT + IR_ULT, mid >= 0 ? ir_here() - mid : 0, -1);
      o->kind = K_VALUE;
      o->type = TY_INT;
      o->is_bool = 1;
      return;
    }
  } else if (!is_pointer(lt) && !is_pointer(rt)) {
    a = unsigned_op(o, r, &op, mid);
    if (IR_IS_U_COMPARE(op)) {     /* already put */
      o->kind = K_VALUE;
      o->type = TY_INT;
      o->is_bool = 1;
      return;
    }
    if (!is_compare(op))
      result = op == IR_SHL || op == IR_SHR || op == IR_USHR
               ? promoted(lt) : arith_type(lt, rt);
  } else if (is_pointer(lt) && !is_pointer(rt)
             && (op == IR_ADD || op == IR_SUB)) {
    push_scaled(r, lt);         /* p + i, p - i */
    result = lt;
  } else if (!is_pointer(lt) && op == IR_ADD) {
    /* i + p: i is already below p. The two are exchanged, so that i
     * is on top to be scaled ("p i IDXW"): by moving i's code after
     * p's, or, if i's start isn't known (a constant written late, or
     * no positions given), when the code runs (swap_blocks();
     * approach.md 4(b)). */
    n = elem_size(rt);
    rvalue(r);
    if (n != 1) {
      swap_blocks(lstart, mid, 0);
      put(IR_CONST, n, -1);
      put(IR_MUL, 0, -1);
    }
    result = rt;
  } else if (is_pointer(lt) && is_pointer(rt) && op == IR_SUB) {
    /* p - q: the difference in elements. */
    if (!same_target(lt, rt))
      error(48 /* "incompatible pointer types in" */, "subtraction");
    n = elem_size(lt);
    rvalue(r);
    put(IR_SUB, 0, -1);
    if (n == 2) {
      put(IR_CONST, 1, -1);
      put(IR_SHR, 0, -1);
    } else if (n != 1) {
      put(IR_CONST, n, -1);
      put(IR_DIV, 0, -1);
    }
    o->kind = K_VALUE;
    o->type = TY_INT;
    o->is_bool = 0;
    return;
  } else {
    error(85 /* "invalid operands to binary" */, op_name(op));
  }
  put(op, a, -1);
  o->kind = K_VALUE;
  o->type = result;
  o->is_bool = is_compare(op);
}

/* Parse the right side of *f() = e, where o is the K_DEREF, its
 * address's code starting at IR position start. Its value and the
 * store wait (K_STORE) until the parser sees whether the value is
 * used; see finish_store(). */
static void start_store(Operand *o, int start)
{
  Operand r;
  int mid = ir_here();
  parse_expr(PREC_ASSIGN, &r);
  check_assign(o->type, &r, 197 /* "assignment" */, NULL);
  rvalue(&r);
  o->kind = K_STORE;
  o->m0 = start;
  o->m1 = mid;
  o->mend = ir_here();
  o->stype = o->type;
  o->vmask = o->type == TY_CHAR && r.type != TY_CHAR && !r.is_bool;
  o->is_bool = r.is_bool;
  o->assigned = 0;
}

/*
 * x op= e, for x a non-indexed pending element (*p, p[3], *gp), whose
 * address is computed twice: for the load and for the store. C says x
 * is evaluated once, and a call in e runs entirely before or entirely
 * after it (C11 6.5.2.2p10, 6.5.16.2p3), so if e calls a function that
 * changes p, the load and the store must still use the same object
 * (approach.md 4(d2)). So x isn't loaded until e is parsed:
 *   - if e emitted no code (a constant or a variable), the load comes
 *     first, as usual: "LLW p LW CN 1 ADD" becomes INCR;
 *   - otherwise the load comes after e's code, and the two values are
 *     exchanged (swap_blocks()). If e has a call, the load must stay
 *     after it, so both addresses follow the call: ccgen then applies
 *     the operator reversed (ADD as is, SUB then NEG, < as >, ...) or
 *     exchanges the values when the code runs. If not, e can't change p
 *     (except with undefined behavior), and ccgen may move the load
 *     ahead of e's code.
 * x is the element; r receives e; the result is left in x.
 */
static void compound_elem(Operand *x, Operand *r, int op)
{
  int mid = ir_here(), h = nheld, calls = ncalls, start, xm;
  parse_expr(PREC_ASSIGN, r);
  if (r->kind == K_CONST && !suppress)
    check_right_const(op, r->val);
  start = code_start(mid, h);
  if (ir_here() == mid) {
    rvalue(x);
    binary(x, r, op, -1, -1);
    return;
  }
  rvalue(r);
  if (is_pointer(value_type(r->type))
      || (is_pointer(x->type) && op != IR_ADD && op != IR_SUB)) {
    /* An error (int op= pointer, pointer *= ...): binary() says so. */
    rvalue(x);
    binary(x, r, op, -1, -1);
    return;
  }
  xm = ir_here();
  rvalue(x);
  swap_blocks(start, xm, ncalls != calls);
  binary(x, r, op, -1, -1);
}

/* Bytes of code that push the address of the place o (a K_VAR or
 * K_ELEM): an estimate, for choosing how to copy a struct. */
static int addr_cost(Operand *o)
{
  int c = o->global ? 3 : 2;
  if (o->kind == K_ELEM && o->idx >= 0)
    c += (o->idx_global ? 3 : 2) + (o->scale != 1 ? 2 : 0) + 1;
  if (o->kind == K_ELEM && o->disp != 0 && !o->base_arr)
    c += 2;
  return c;
}

/* Bytes of code that load or store the word at offset off in the place
 * o (a K_VAR or K_ELEM): an estimate, as for addr_cost(). */
static int access_cost(Operand *o, int off)
{
  if (o->kind == K_VAR)
    return o->global ? 3 : 2;
  return addr_cost(o) + (off != 0 ? 2 : 0) + 1;
}

/* Push the address of the place o. A K_DEREF's is already on the
 * stack. */
static void push_place_addr(Operand *o)
{
  if (o->kind == K_VAR)
    push_var_addr(o->off, o->global);
  else if (o->kind == K_ELEM)
    push_address(o);
}

/* The function a struct copy calls: __memcpy(dst, src, n), from CMDSYS
 * unless the program defines its own, which must take C's arguments in
 * C's order. A value it returns is dropped. */
static int copy_func(void)
{
  int f = find_func(ID_MEMCPY);
  if (f < 0) {
    f = add_func(ID_MEMCPY);
    declare_cmdsys(f, find_cmdsys(ID_MEMCPY));
  } else if (nparams_of(f) != 3) {
    error(86 /* "a struct copy calls memcpy(dst, src, n), but this "
                "program declares memcpy differently" */, NULL);
  }
  if (!suppress)
    fn_flags[f] |= FN_CALLED;
  return f;
}

/*
 * Copy the struct r into the place o (approach.md, "M6 design"). r's
 * code starts at IR position start (if it emitted any). Between two
 * places whose addresses need no code yet (K_VAR, K_ELEM), the words
 * are moved one at a time (LLW b SLW a ...) when that is no bigger
 * than calling memcpy; otherwise memcpy(dst, src, n) is called, with
 * dst's address first. If r's address was already emitted and o's
 * wasn't, the two are exchanged (swap_blocks()), as for a store
 * (finish_store()). A struct on the stack (a call's result) is stored
 * word by word, last first. o becomes the result: the object assigned
 * to, not an lvalue (for a K_DEREF, its address was kept with DUP).
 */
void copy_struct(Operand *o, Operand *r, int start)
{
  Operand d, v;
  int n = type_size(o->type), k, cost = 0, mid, fn;

  if (r->kind == K_VALUE && o->kind != K_DEREF) {
    /* Its words are on the stack: store them, last word first. */
    for (k = struct_words(o->type) - 1; k >= 0; k--) {
      d = *o;
      offset_place(&d, 2 * k);
      d.type = n - 2 * k == 1 ? TY_CHAR : TY_INT;
      if (d.kind == K_VAR) {
        store_var(&d);
        put(IR_DROP, 0, -1);
      } else {
        push_address(&d);
        put(store_op(d.type), 0, -1);
      }
    }
    o->assigned = 1;
    return;
  }
  if (r->kind == K_VALUE)
    materialize(r);             /* the address is below it: *f() = g() */
  if (o->kind != K_DEREF && r->kind != K_DEREF) {
    for (k = 0; k < n; k += 2)
      cost += access_cost(o, k) + access_cost(r, k);
    if (cost <= addr_cost(o) + addr_cost(r) + (n < 16 ? 1 : 2) + 3) {
      for (k = 0; k < n; k += 2) {
        v = *r;
        d = *o;
        offset_place(&v, k);
        offset_place(&d, k);
        v.type = d.type = n - k == 1 ? TY_CHAR : TY_INT;
        rvalue(&v);
        if (d.kind == K_VAR) {
          store_var(&d);
          put(IR_DROP, 0, -1);
        } else {
          push_address(&d);
          put(store_op(d.type), 0, -1);
        }
      }
      o->assigned = 1;
      return;
    }
  }
  fn = copy_func();
  if (o->kind == K_DEREF || r->kind != K_DEREF) {
    push_place_addr(o);         /* nothing for a K_DEREF */
    push_place_addr(r);
  } else {
    mid = ir_here();            /* r's address is already there */
    push_place_addr(o);
    swap_blocks(start, mid, 0);
  }
  put(IR_CONST, n, -1);
  put(IR_CALL, 3, fn);
  if (fn_type[fn] != TY_VOID)
    put(IR_DROP, 0, -1);
  if (o->kind != K_DEREF)
    o->assigned = 1;
}

/* Parse the right side of a struct assignment, with o the struct
 * assigned to, and copy it. o becomes the result. */
static void struct_assign(Operand *o)
{
  Operand r;
  int mark, h;
  if (o->kind == K_DEREF)
    put(IR_DUP, 0, -1);       /* the address, kept for the result */
  mark = ir_here();
  h = nheld;
  parse_expr(PREC_ASSIGN, &r);
  check_assign(o->type, &r, 197 /* "assignment" */, NULL);
  copy_struct(o, &r, code_start(mark, h));
}

/* Parse an assignment after its operator (ir_op, -1 for '='), with o
 * the lvalue, whose parsing began at IR position m0 with h0 constants
 * held. o becomes the result. */
static void parse_assign(Operand *o, int ir_op, int m0, int h0)
{
  Operand r, x;

  if (is_struct(o->type) && ir_op < 0) {
    struct_assign(o);
    return;
  }
  if (ir_op < 0 && o->kind == K_DEREF) {
    start_store(o, code_start(m0, h0));
    return;
  }
  if (ir_op >= 0 && o->kind == K_ELEM && o->idx >= 0) {
    /* x op= e loads x, then stores into it after e. Saving an
     * indexed element's address (spill()) is 1 byte smaller than
     * computing it twice, and stays right if e changes the index
     * through a pointer. With a SWAP opcode, neither would be
     * needed: addr DUP LW e op SWAP SW (approach.md issue 21). */
    push_address(o);
    o->kind = K_DEREF;
  }
  if (ir_op >= 0 && o->kind == K_ELEM) {
    /* Its address is computed twice; see compound_elem(). */
    x = *o;
    compound_elem(&x, &r, ir_op);
    r = x;
  } else if (ir_op >= 0) {
    /* x op= e with x's address computed once (a variable, or a
     * spilled address): compute x op e, then store it as for
     * x = e. Loading x before e runs is one of the orders C
     * allows. */
    spill(o);
    x = *o;
    rvalue(&x);
    parse_expr(PREC_ASSIGN, &r);
    if (r.kind == K_CONST && !suppress)
      check_right_const(ir_op, r.val);
    binary(&x, &r, ir_op, -1, -1);
    r = x;
  } else {
    spill(o);
    parse_expr(PREC_ASSIGN, &r);
  }
  check_assign(o->type, &r, 197 /* "assignment" */, NULL);
  rvalue(&r);
  assign_to(o);
  o->is_bool = ir_op < 0 && r.is_bool && o->kind == K_VALUE;
}

/*
 * Precedence climbing: parse an expression whose operators all have
 * precedence min_prec or higher. The result is left in *o, possibly
 * still pending (a bare variable).
 */
void parse_expr(int min_prec, Operand *o)
{
  int ir_op, supported, prec, m0 = ir_here(), h0 = nheld, lstart, mid;
  Operand r;

  enter_nest();
  parse_unary(o);

  if (assign_op(&ir_op)) {
    /* Assignment is right-associative and binds loosest of all but
     * ','; it is only possible directly after a unary expression. */
    if (min_prec > PREC_ASSIGN)
      error_in(41 /* "lvalue required as" */,
               194 /* "left operand of assignment" */, NULL);
    need_lvalue(o, 194 /* "left operand of assignment" */);
    next();
    parse_assign(o, ir_op, m0, h0);
  }

  for (;;) {
    int mark = -1, is_addr = 0, a, f, op;
    Operand addr;
    lstart = -1;
    if (assign_op(&ir_op))
      error_in(41 /* "lvalue required as" */,
               194 /* "left operand of assignment" */, NULL);
    prec = binary_prec(&ir_op, &supported);
    if (prec == 0 || prec < min_prec)
      break;
    if (!supported)
      not_yet();
    if (tok == T_ANDAND || tok == T_OROR) {
      parse_logical(o, prec);
      continue;
    }
    if (tok == '?') {
      parse_conditional(o);
      continue;
    }
    if (o->kind == K_CONST && !is_pointer(o->type) && nheld < MAX_HELD) {
      /* Hold it back: it may fold with the right operand. */
      mark = hold(IR_CONST, o->val, -1);
    } else if ((ir_op == IR_ADD || ir_op == IR_SUB) && nheld < MAX_HELD
               && addr_const(o)
               && (o->off == 0 || o->global == G_STR
                   || !movable(o->global))) {
      /* An address constant, held back the same way: plus or minus a
       * constant folds into it. (Not for a comparison, whose right
       * operand's code ccgen must know the length of; nor one that may
       * move with an offset already, which takes more than one op.) */
      op = addr_op(o, &a, &f);
      addr = *o;
      is_addr = 1;
      mark = hold(op, a, f);
      o->kind = K_VALUE;        /* its value, once put() writes it */
    } else {
      rvalue(o);
      /* All the code since this call began is the left operand. */
      lstart = code_start(m0, h0);
    }
    mid = ir_here();
    next();
    parse_expr(prec + 1, &r);   /* all binary ops are left-assoc */
    if (r.kind == K_CONST && !suppress)
      check_right_const(ir_op, r.val);
    if (mark >= 0 && nheld > mark && is_addr) {
      /* The right operand emitted nothing, so the left address
       * constant is still held: a + c and a - c fold. */
      if (r.kind == K_CONST && !is_pointer(r.type)) {
        nheld--;
        *o = addr;
        a = wrap16((unsigned)r.val * (unsigned)elem_size(o->type));
        o->off = wrap16((unsigned)o->off
                        + (ir_op == IR_ADD ? (unsigned)a : 0u - (unsigned)a));
        continue;
      }
    } else if (mark >= 0 && nheld > mark) {
      /* The right operand emitted nothing, so the left constant
       * is still held. */
      if (is_pointer(value_type(r.type))) {
        if (ir_op == IR_ADD && r.kind != K_VOID) {
          /* c + p: emit p, then c scaled; c + an address constant
           * folds. */
          Operand c = *o;
          nheld--;
          if (addr_const(&r)) {
            a = wrap16((unsigned)c.val * (unsigned)elem_size(r.type));
            r.off = wrap16((unsigned)r.off + (unsigned)a);
            *o = r;
            continue;
          }
          rvalue(&r);
          push_scaled(&c, r.type);
          put(IR_ADD, 0, -1);
          *o = r;
          continue;
        }
      } else if (r.kind == K_CONST) {
        int shift = IR_IS_S_SHIFT(ir_op);
        nheld--;
        o->val = fold(ir_op, o->val, r.val,
                      shift ? is_uns(o->type)
                            : is_uns(o->type) || is_uns(r.type));
        o->type = is_compare(ir_op) ? TY_INT
                  : shift ? promoted(o->type) : arith_type(o->type, r.type);
        o->nonice |= r.nonice;
        continue;
      } else if (r.kind == K_VAR && swapped_op(ir_op) >= 0) {
        /* c op x: emit x c op' (op' is op with its operands
         * swapped), so that the peephole stage sees the
         * constant right before the op (1+x: INCR). An unsigned
         * comparison's right operand is then that one constant. */
        int op = swapped_op(ir_op);
        nheld--;
        rvalue(&r);
        put(IR_CONST, o->val, -1);
        if (is_relational(op)
            && (is_uns(o->type) || is_uns(value_type(r.type))))
          put(op + IR_ULT - IR_LT, 1, -1);
        else
          put(op, 0, -1);
        o->type = is_compare(ir_op) ? TY_INT : arith_type(o->type, r.type);
        o->kind = K_VALUE;
        o->is_bool = is_compare(ir_op);
        continue;
      }
    }
    /* Otherwise the left value is (or, via put(), will be) on the
     * stack below the right one. */
    binary(o, &r, ir_op, lstart, mid);
  }
  nest--;
}
