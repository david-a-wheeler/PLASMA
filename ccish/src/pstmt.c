// cccomp's front end, statements and declarations (approach.md,
// "parse.c split design"): the declarators and array sizes, the
// struct/enum/typedef definitions, local declarations, if, the
// loops (with their markers), switch (with the case table), break
// and continue, return, blocks, and parse_params.

#ifdef CCISH_STATS
#include <stdio.h>
#endif
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "tokin.h"
#include "obj.h"
#include "ir.h"
#include "name.h"
#include "name.tab.h"
#include "type.h"
#include "type.tag.h"
#include "parse.h"
#include "irout.h"
#include "ptop.h"
#include "pstmt.h"
#include "pexpr.h"
#define MAX_PARAMS   16     // limit function: parameters of one function

#define MAX_CASES    36     // limit function: case labels in all the switches
                            // open at once; at most 255, a SEL table's limit

// ---------- Statements ----------


// Declarators (approach.md, "Function pointer design"). A declarator
// is read in C's own order, inside out: in int *(*p)[3], p is a
// pointer to an array of 3 pointers to int. As it is read, each
// derivation (a pointer, an array of n, a function) goes on a small
// stack, in the order it applies to the base type, and then the type
// is built from them. A nested declarator's own derivations come after
// the suffixes that follow it, since they apply last; the parse puts
// them there by rotating the stack. Nothing is rewound, so the token
// stream is still read once, in order.
//
// A function's own parameter list, the one right after the name of a
// function declared at file scope (D_PARAMS), is parsed by
// begin_function() (ptop.c), which declares the parameters. Every
// other parameter list (in a pointer to a function, a typedef, a cast,
// a parameter's own declarator, a block-scope declaration) is skipped
// as balanced parentheses: a function type is only its return type
// (type.h), so ccish accepts any list a C compiler would and checks
// none of it.
enum { DS_PTR, DS_ARRAY, DS_FUNC };
#define MAX_STEPS    16     // limit other: pointers, arrays and functions in
                            // the declarators being read
static char step_kind[MAX_STEPS];
static int step_val[MAX_STEPS];    // a pointer's qualifiers, or an
                                   // array's count (0: left out)
static int nsteps;

// The parameter count begin_function() gave for the D_PARAMS
// declarator just read, or -2 if its name had no parameter list right
// after it.
int decl_nparams;

// Push derivation kind k with value v.
static void add_step(int k, int v)
{
  if (nsteps >= MAX_STEPS)
    fatal0(29 /* "too many parts in a declarator" */);
  step_kind[nsteps] = (char)k;
  step_val[nsteps++] = v;
}

// Reverse steps from to to - 1.
static void reverse_steps(int from, int to)
{
  int k, v;
  while (from < --to) {
    k = step_kind[from];
    v = step_val[from];
    step_kind[from] = step_kind[to];
    step_val[from] = step_val[to];
    step_kind[to] = (char)k;
    step_val[to] = v;
    from++;
  }
}

// Skip a parameter list, its '(' already read, to its matching ')'.
static void skip_params(void)
{
  int open = 1;
  while (open) {
    if (tok == T_EOF)
      error0(219 /* "missing ')' at end of file" */);
    if (tok == '(')
      open++;
    else if (tok == ')')
      open--;
    next();
  }
}

// Parse one array size after its '[', through the ']': a positive
// constant, or 0 if it is left out.
static int parse_dim(void)
{
  int n = 0;
  next();                         // '['
  if (tok != ']') {
    n = parse_const(88 /* "array size must be a constant" */,
                    202 /* "(an array size must be one)" */);
    if (n <= 0)
      error0(89 /* "array size must be positive" */);
  }
  expect(']', 90 /* "expected ']' after an array size, not" */);
  return n;
}

// Parse a declarator's suffixes (array sizes and parameter lists) and
// push them, applying from the last: int m[3][4] is an array of 3
// arrays of 4. open is set if the first is a parameter list whose '('
// is already read; own_name is the declared name if its own parameter
// list may come first (D_PARAMS), else -1.
static void parse_suffixes(int open, int own_name)
{
  int from = nsteps;
  if (open) {
    skip_params();
    add_step(DS_FUNC, 0);
  }
  for (;;) {
    if (tok == '[') {
      add_step(DS_ARRAY, parse_dim());
    } else if (tok == '(') {
      if (own_name >= 0) {
        decl_nparams = begin_function(own_name);
      } else {
        next();
        skip_params();
      }
      add_step(DS_FUNC, 0);
    } else {
      break;
    }
    own_name = -1;
  }
  reverse_steps(from, nsteps);
}

// Parse a declarator, pushing its derivations, and store its name's id
// in *name (-1 if it has none, which flags must allow: D_ABSTRACT).
static void parse_steps(int flags, int *name)
{
  int from, inner;
  while (tok == '*') {
    next();
    add_step(DS_PTR, parse_quals());
  }
  if (tok == '(') {
    next();
    if (tok == '*' || tok == '(' || (tok == T_ID && !is_type_start())) {
      // A nested declarator, (*p): its derivations apply after the
      // suffixes that follow it.
      enter_nest();
      from = nsteps;
      parse_steps(flags, name);
      nest--;
      expect(')', 92 /* "expected ')' in a declarator, not" */);
      inner = nsteps;
      parse_suffixes(0, -1);
      reverse_steps(from, inner);         // rotate the suffixes
      reverse_steps(inner, nsteps);       // ahead of the nested
      reverse_steps(from, nsteps);        // declarator's steps
      return;
    }
    *name = -1;                 // an unnamed function: int (int)
    if (!(flags & D_ABSTRACT))
      error(93 /* "expected a name, not" */, tok_desc());
    parse_suffixes(1, -1);
    return;
  }
  *name = -1;
  if (tok == T_ID && !(flags & D_NONAME)) {
    *name = tok_val;
    next();
  } else if (!(flags & D_ABSTRACT)) {
    error(93 /* "expected a name, not" */, tok_desc());
  }
  parse_suffixes(0, (flags & D_PARAMS) && *name >= 0 ? *name : -1);
}

// Parse a declarator after the base type base, storing its name's id
// in *name (-1 for none), and return the declared type, with flags
// D_ABSTRACT (the name may be left out), D_NONAME (there's none: a
// type name) and D_PARAMS (a file-scope function's own parameter list
// is parsed by begin_function(); decl_nparams says if it was). The
// declared object's own (outermost) array size may be left out (count
// 0): the caller checks that where it isn't allowed. A function's
// return type loses its qualifiers.
int parse_declarator(int base, int flags, int *name)
{
  int from = nsteps, t = base, i, k;
  if (flags & D_PARAMS)
    decl_nparams = -2;
  parse_steps(flags, name);
  for (i = from; i < nsteps; i++) {
    k = step_kind[i];
    if (k == DS_PTR) {
      t = pointer_to(t) | step_val[i];
      continue;
    }
    if (is_func(t) || (k == DS_FUNC && is_array(t)))
      error(k == DS_FUNC
            ? 112 /* "a function can't return an array or a function" */
            : 114 /* "a function type isn't allowed here (use a pointer"
                     " to a function)" */, NULL);
    if (k == DS_FUNC) {
      t = func_of(t);
      continue;
    }
#ifdef CCISH_LINT
    need_complete(t, 209 /* "an array element can't have the incomplete"
                            " type" */);
#endif
    if (type_size(t) == 0)
      error0(91 /* "array elements can't have type void" */);
    if (step_val[i] == 0 && i != nsteps - 1)
      error0(87 /* "array size missing" */);
    t = array_of(t, step_val[i]);
  }
  nsteps = from;
  return t;
}

// Parse a type name, as in a cast or sizeof: a base type and an
// abstract declarator (int (*)(int)). It can't be a function or an
// array of unknown size.
int parse_type_name(void)
{
  int name, t = parse_declarator(parse_base_type(), D_ABSTRACT | D_NONAME,
                                 &name);
  if (is_func(t))
    error0(114 /* "a function type isn't allowed here (use a pointer"
                  " to a function)" */);
  if (is_array(t) && type_count(t) == 0)
    error0(87 /* "array size missing" */);
  return t;
}

// Check a variable's declared type, which can't be void or an
// incomplete struct, and return it. If may_omit is clear, an array's
// size can't be left out.
int parse_var_type(int type, int may_omit)
{
#ifdef CCISH_LINT
  if (UNQUAL(type) == TY_VOID)
    error0(94 /* "a variable can't have type void" */);
#endif
  if (!may_omit && is_array(type) && type_count(type) == 0)
    error0(87 /* "array size missing" */);
  need_complete(type, 207 /* "a variable can't have the incomplete type" */);
  return type;
}

// Parse a struct's or union's member list, from '{' to '}', and
// complete tag's aggregate: a struct's members are at the next byte,
// with no padding, since the 6502 has no alignment rules (approach.md,
// "M6 design"), while a union's all start at byte 0, with the union's
// size the largest member's (is_union). While the list is parsed, the
// tag's size is -2, so a nested redefinition of the same tag is
// caught.
static void parse_members(int tag, int is_union)
{
  int name, off = 0, base, t;

  enter_nest();
  tg_size[tag] = -2;
  next();                         // '{'
  if (tok == '}')
    error0(95 /* "a struct or union needs at least one member" */);
  while (tok != '}') {
    if (tok == T_KEYWORD)
      not_yet();
    if (!is_type_start())
      error(96 /* "expected a struct or union member, not" */,
            tok_desc());
    base = parse_base_type();
    for (;;) {
      t = parse_declarator(base, 0, &name);
#ifdef CCISH_LINT
      if (is_func(t))
        error(97 /* "a struct or union member can't be a function:" */,
              quote(name));
#endif
      if (is_array(t) && type_count(t) == 0)
        error0(87 /* "array size missing" */);
#ifdef CCISH_LINT
      if (UNQUAL(t) == TY_VOID)
        error0(98 /* "a struct or union member can't have type void" */);
#endif
#ifdef CCISH_LINT
      need_complete(t, 208 /* "a struct or union member can't have the"
                              " incomplete type" */);
#endif
#ifdef CCISH_LINT
      if (member_find(tag, name) >= 0)
        error(99 /* "duplicate struct or union member" */, quote(name));
#endif
      if (off > MAX_OBJECT - type_size(t))
        error0(100 /* "struct or union too large (over 32767 bytes)" */);
      member_add(tag, name, t, is_union ? 0 : off);
      off = is_union
          ? (type_size(t) > off ? type_size(t) : off) : off + type_size(t);
      if (tok != ',')
        break;
      next();
    }
    expect(';', 101 /* "expected ';' after a struct or union member, "
                       "not" */);
  }
  next();
  tg_size[tag] = off;
  nest--;
}

// Parse a struct type after "struct", or a union type after "union"
// (is_union), and return it: a tag, a member list, or both. As in C:
//   - struct s { ... } defines s in the current block (or at file
//     scope), completing an earlier "struct s;" there;
//   - struct s; alone declares a new s in the current block, hiding any
//     outer one;
//   - struct s elsewhere refers to the innermost visible s, or declares
//     a new, incomplete one, so that struct node *next; works before
//     struct node is complete.
// A tag first seen in a parameter list is visible only inside that
// function, which is rarely what was meant; gcc warns, and so do we.
// A union has the union layout: every member starts at byte 0, and the
// union is the largest member (see parse_members()). It is stored like
// a struct: the same TK_STRUCT type, with its tag marked a union's
// (type.h), so every mechanism that works on a struct's members and
// size works on a union's. Struct and union tags share a name space,
// so a name can't be both, which the standard C allows (a known
// difference, as with enum). One function for both, since they differ
// only in that.
int parse_struct(int is_union)
{
  int name = -1, tag = -1, hsum = 0, hmul = 1;

  next();                         // struct or union
  if (tok == T_ID) {
    name = tok_val;
    hsum = tok_hsum;
    hmul = tok_hmul;
    next();
    tag = tag_find(name, tok == '{' || tok == ';' ? depth : -1);
    if (tag >= 0 && tg_union[tag] != is_union)
      error(104 /* "redefinition of" */, tag_desc(tag));
  } else if (tok != '{') {
    error(is_union
          ? 318 /* "expected a union name or '{' after 'union', not" */
          : 102 /* "expected a struct name or '{' after 'struct', not" */,
          tok_desc());
  }
  if (tag < 0) {
    tag = tag_new(name, depth, hsum, hmul);
#ifdef CCISH_DIAG
    if (in_params && name >= 0)
      warning(103 /* "declared in a parameter list, so not visible "
                     "outside it:" */, tag_desc(tag));
#endif
  }
  tg_union[tag] = (char)is_union; // members all at 0; the largest's size
  if (tok == '{') {
    if (tg_size[tag] != -1)
      error(104 /* "redefinition of" */, tag_desc(tag));
    parse_members(tag, is_union);
  }
  return struct_type(tag);
}

// Parse an enum type after "enum", and return it: an enum type is int,
// which C allows (approach.md, "enum and typedef design"). A list
// declares its constants in the current block: each is the given value
// (an integer constant expression, which may use earlier constants), or
// one more than the one before, starting at 0, and must fit in an int.
// The tag isn't recorded, so enum e isn't checked against a definition
// (a known difference from C: ccish accepts some wrong programs).
int parse_enum(void)
{
  int v = 0, full = 0, name;

  next();                         // enum
  if (tok == T_ID)
    next();                     // the tag
  else if (tok != '{')
    error(105 /* "expected an enum name or '{' after 'enum', not" */,
          tok_desc());
  if (tok != '{')
    return TY_INT;
  next();
  if (tok == '}')
    error0(106 /* "an enum needs at least one constant" */);
  while (tok != '}') {
    if (tok != T_ID)
      error(107 /* "expected an enum constant, not" */, tok_desc());
    check_not_var(tok_val);
    check_not_id(tok_val);
    name = tok_val;
    next();
    if (tok == '=') {
      next();
      v = parse_const(108 /* "an enum value must be an integer"
                             " constant" */,
                      203 /* "(an enum value must be one)" */);
      full = 0;
    } else if (full) {
      error0(109 /* "enum value too large for an int (over 32767)" */);
    }
    id_add(name, ID_CONST, v, depth);
    full = v == 0x7FFF;
    v = wrap16((unsigned)v + 1u);
    if (tok != ',')
      break;
    next();
  }
  expect('}', 110 /* "expected ',' or '}' in an enum, not" */);
  return TY_INT;
}

// typedef type name, ...; declares each name (with its own '*'s and
// array sizes) as a name for its type, in the current block. A typedef
// may be repeated with the same type, as C11 allows. C23's constexpr
// type name = value, ...; shares it: each name is a named constant, as
// an enum constant is, with no storage, whose kind is its type
// (name.h). So, unlike C, a constexpr has no address, and must be a
// char, int or unsigned; its value isn't checked to fit the type.
void parse_typedef(void)
{
  int name, base, t, i, kw = tok, kind = ID_TYPEDEF;

  next();                         // typedef or constexpr
  if (!is_type_start())
    error(111 /* "expected a type after 'typedef' or 'constexpr', not" */,
          tok_desc());
  base = parse_base_type();
  for (;;) {
    t = parse_declarator(base, 0, &name);
    if (kw == T_CONSTEXPR) {
      kind = UNQUAL(t);
      if (!IS_INT_TYPE(kind))
        not_yet();
      expect('=', 346 /* "expected '=' after a constexpr's name, not" */);
      t = parse_const(347 /* "a constexpr's value must be an integer"
                             " constant" */,
                      348 /* "(a constexpr's value must be one)" */);
    } else if (is_array(t) && type_count(t) == 0) {
      error0(87 /* "array size missing" */);
    }
    i = id_find(name);
    if (i >= 0 && ID_DEPTH(i) == depth) {
#ifdef CCISH_LINT
      if (id_k[i] != kind || id_v[i] != t)
        error(27 /* "redeclaration of" */, quote(name));
#endif
    } else {
      check_not_var(name);
      id_add(name, kind, t, depth);
    }
    if (tok != ',')
      break;
    next();
  }
  expect(';', 113 /* "expected ';' after a typedef or constexpr, not" */);
}

// Initialize the local struct s from a struct expression (after the
// '='), as C allows for a local: struct pt b = a; It is a struct
// copy into s.
static void init_struct_copy(int s)
{
  Operand v, e;
  int mark, h;
  begin_expr();
  v.kind = K_VAR;
  v.type = UNQUAL(var_type[s]);
  v.quals = 0;
  v.off = var_off[s];
  v.global = 0;
  v.assigned = 0;
  v.is_bool = 0;
  mark = ir_here();
  h = nheld;
  parse_expr(PREC_ASSIGN, &e);
  check_assign(v.type, &e, 198 /* "initialization" */, NULL);
  copy_struct(&v, &e, code_start(mark, h));
  end_expr();
}

// Record that the variable s has the known value v, if reading it may
// give v at compile time: it is a const scalar (an int, char or
// pointer, not an array or struct) and not volatile. A char keeps the
// low byte, as stored.
void set_known(int s, int v)
{
  if ((var_type[s] & Q_ALL) != Q_CONST || is_array(var_type[s])
      || is_struct(var_type[s]))
    return;
  var_flags[s] |= V_KNOWN;
  var_val[s] = UNQUAL(var_type[s]) == TY_CHAR ? v & 0xFF : v;
}

// Add a static local (parse.c's push_var()): in block scope, like any
// local, but in the data segment like a global (V_DATA, at the next
// data offset), so it keeps its value between calls and takes no frame
// space. The caller then writes its bytes to the data stream: its
// initial value, or zeros. A macro, not a function: this is its one
// user.
#define add_static_local(name, type) \
  push_var(name, type, data_offset(), V_DATA)

// A declaration: a base type, then declarators, each a name with any
// '*'s before it, any array sizes after it, and an optional
// initializer: int a = 1, *p = &a, v[10]; A struct or enum type may
// have no declarators (struct pt { int x, y; };). is_static is set
// after "static": the variables are static locals, in the data
// segment, with constant initializers applied once, when the module
// loads. A "register" that starts it is read here: each variable is an
// ordinary local, which ccgen is told no pointer can reach
// (IR_REGLOCAL), since C forbids taking its address. A function
// declared here (int g(int);) is declared as at file scope, from here
// on: C would hide it after the block, and ccish doesn't.
static void parse_declaration(int is_static)
{
  int reg = tok == T_REGISTER, base;
  if (reg)
    next();
  base = parse_base_type();
  if (tok == ';' && base_tagged) {
    next();                     // struct pt { ... }; declares only
    return;
  }
  for (;;) {
    Operand v, e;
    int s, name;
    int type = parse_declarator(base, 0, &name), braced;
    if (is_func(type)) {
      note_import(declare_function(name, type, 0), parse_asm_label());
      if (tok != ',')
        break;
      next();
      continue;
    }
    type = parse_var_type(type, 1);
    if (is_array(type) && type_count(type) == 0 && tok != '=')
      error0(87 /* "array size missing" */);
    if (is_static) {
      // In C, the name is in scope in its own initializer.
      s = add_static_local(name, type);
      if (tok == '=') {
        next();
        var_type[s] = parse_global_init(type, name);
        if (init_known)
          set_known(s, init_val);
      } else {
        data_zero(type_size(type));
      }
      if (tok != ',')
        break;
      next();
      continue;
    }
    if (is_array(type) && frame_next + type_size(type) > 255)
      error(115 /* "local array doesn't fit in the 255-byte frame "
                   "(make it static or global):" */, quote(name));
    if (is_struct(type) && frame_next + type_size(type) > 255)
      error(116 /* "local struct doesn't fit in the 255-byte frame "
                   "(make it static or global):" */, quote(name));
    // In C, the name is in scope in its own initializer. (An array
    // whose size comes from its initializer grows as it is parsed:
    // see reserve().)
    s = add_local(name, type, type_size(type));
    if (reg) {
#ifdef CCISH_LINT
      var_link[s] = VL_REGISTER;    // & of it is an error (pexpr.c)
#endif
      put(IR_REGLOCAL, var_off[s], -1);
    }
    if (tok == '=' && (is_array(type) || is_struct(type))) {
      next();
      if (is_struct(type) && tok != '{')
        init_struct_copy(s);    // struct pt b = a;
      else
        var_type[s] = parse_local_init(type, name, var_off[s]);
    } else if (tok == '=') {
      next();
      braced = tok == '{';        // int x = {5}; is valid C
      if (braced)
        next();
      begin_expr();
      parse_expr(PREC_ASSIGN, &e);
      check_assign(type, &e, 198 /* "initialization" */, NULL);
      if (e.kind == K_CONST)
        set_known(s, e.val);
      rvalue(&e);
      v.kind = K_VAR;
      v.type = UNQUAL(type);
      v.quals = type & Q_ALL;     // a volatile x = e; stores volatile
      v.off = var_off[s];
      v.global = 0;
      store_var(&v);
      put_op(IR_DROP);
      end_expr();
      if (braced) {
        if (tok == ',')
          next();
        expect('}', 117 /* "expected '}' after the initializer,"
                           " not" */);
      }
    }
    if (tok != ',')
      break;
    next();
  }
  expect(';', 118 /* "expected ';' after declaration, not" */);
}

// Parse a full expression used as a statement (or a for clause), and
// discard its value.
static void parse_expr_statement(void)
{
  Operand o;
  begin_expr();
  parse_expr(PREC_COMMA, &o);
  discard(&o);
  end_expr();
}


// Parse a full expression as a condition, and branch to label n as
// cond_branch does.
static void parse_cond(int n, int on_true)
{
  Operand o;
  begin_expr();
  parse_expr(PREC_COMMA, &o);
  cond_branch(&o, n, on_true);
  end_expr();
}

// Parse "( condition )" and branch to label n as cond_branch does.
static void parse_paren_cond(int n, int on_true)
{
  expect('(', 119 /* "expected '(' before condition, not" */);
  parse_cond(n, on_true);
  expect(')', 120 /* "expected ')' after condition, not" */);
}

// The innermost loops' and switches' labels, for break and continue.
// A switch's cont_label is its enclosing loop's (-1 if none), since
// continue in a switch goes to the loop around it.
static int brk_label[MAX_NEST], cont_label[MAX_NEST];
static int nloops;

static void parse_statement(void);

// Parse a loop's (or switch's) body, with break going to label brk and
// continue to label cont.
static void loop_body(int brk, int cont)
{
  if (nloops >= MAX_NEST)
    fatal0(121 /* "loops nested too deeply" */);
  brk_label[nloops] = brk;
  cont_label[nloops] = cont;
  nloops++;
  parse_statement();
  nloops--;
}

// Loops (approach.md, "Loop design"). The parser writes a loop's parts
// in source order between loop markers, which name its labels, and
// ccgen lays it out with the test at the bottom:
//   BRNCH Ltest  Lbody: s  Lcont: step  Ltest: c BRTRU Lbody  Lbrk:
// So the test is a true-sense branch to the body, and a while loop has
// an empty step. ccgen writes the jumps and labels.

// Write the marker after a loop's step, and parse its body, with
// labels from loop_labels() (body, test, continue, break); then its
// end marker.
static void loop_rest(int *l)
{
  put(IR_LOOPBODY, l[0], l[1]);
  loop_body(l[3], l[2]);
  put(IR_LOOPEND, l[2], l[3]);
}

// Make a loop's labels (body, test, continue, break), and write its
// first marker.
static void loop_labels(int *l)
{
  int i;
  for (i = 0; i < 4; i++)
    l[i] = new_label();
  put(IR_LOOP, l[1], l[3]);
}

// while (c) s
static void parse_while(void)
{
  int l[4];
  next();                         /* "while" */
  loop_labels(l);
  parse_paren_cond(l[0], 1);
  put(IR_LOOPSTEP, l[2], l[3]);
  loop_rest(l);
}

// do s while (c);  Ltop: s  Lcont: c BRTRU Ltop  Lbrk:
static void parse_do(void)
{
  int top = new_label(), cont = new_label(), brk = new_label();
  next();                         /* "do" */
  place_label(top);
  loop_body(brk, cont);
  expect(T_WHILE, 122 /* "expected 'while' after the body of 'do', not" */);
  place_forward(cont);
  parse_paren_cond(top, 1);
  expect(';', 123 /* "expected ';' after 'do ... while (...)', not" */);
  place_forward(brk);
}

// for (init; c; step) s: the init, then a loop (above); with no
// condition, the test is a jump to the body. The init may declare
// variables (as in C99); they are in scope only in the loop.
static void parse_for(void)
{
  int saved_locals = nvars - nglobals, saved_next = frame_next;
  int l[4];

  next();                         /* "for" */
  expect('(', 124 /* "expected '(' after 'for', not" */);
  depth++;
  if (tok == T_STATIC || tok == T_EXTERN)
    error0(125 /* "a 'for' loop can't declare a static or extern"
                  " variable" */);
  if (tok == T_REGISTER || is_type_start()) {
    parse_declaration(0);       // includes the ';'
  } else {
    if (tok != ';')
      parse_expr_statement();
    expect(';', 126 /* "expected ';' after 'for' initialization, not" */);
  }
  loop_labels(l);
  if (tok != ';')
    parse_cond(l[0], 1);
  else
    branch(IR_JUMP, l[0]);
  expect(';', 127 /* "expected ';' after 'for' condition, not" */);
  put(IR_LOOPSTEP, l[2], l[3]);
  if (tok != ')')
    parse_expr_statement();
  expect(')', 128 /* "expected ')' after 'for' clauses, not" */);
  loop_rest(l);
  depth--;
  nvars = nglobals + saved_locals;
  frame_next = saved_next;
}

// break; and continue;: jump to the innermost loop's labels (or, for
// break, the innermost switch's).
static void parse_break_continue(void)
{
  int is_break = tok == T_BREAK;
  next();
  if (nloops == 0 && is_break)
    error0(129 /* "'break' is not inside a loop or switch" */);
  if (!is_break && (nloops == 0 || cont_label[nloops - 1] < 0))
    error0(130 /* "'continue' is not inside a loop" */);
  branch(IR_JUMP, is_break ? brk_label[nloops - 1]
                           : cont_label[nloops - 1]);
  expect(';', is_break ? 131 /* "expected ';' after 'break', not" */
                       : 132 /* "expected ';' after 'continue', not" */);
}

// label: statement. The label is an identifier (its own namespace, so
// it may spell a variable or typedef too), defined here and targeted
// only by gotos. In C a label's only use is as a goto's target, so the
// front end gives it a goto label (ir.h), and ccgen keeps any loop
// whose body holds it uncounted. A label is followed by a statement,
// which may be another label.
// A macro, not a function: it has one caller.
#define parse_label() do { int l_ = goto_label(tok_val, 1); \
  next(); expect(':', 315 /* "expected ':' after a label, not" */); \
  place_label(l_); parse_statement(); } while (0)

// goto label;: an unconditional jump to the label (reserved if it comes
// after its ':', defined if before). It is a plain branch to a goto
// label; ccgen keeps such loops uncounted, so it always runs on a clean
// stack.
static void parse_goto(void)
{
  int l;
  next();                         // 'goto'
  if (tok != T_ID)
    error(316 /* "expected a label after 'goto', not" */, tok_desc());
  l = goto_label(tok_val, 0);
  next();                         // the label
  branch(IR_JUMP, l);
  expect(';', 317 /* "expected ';' after 'goto', not" */);
}

// The cases of the switches being parsed, innermost last (approach.md,
// "M7 design"). As parallel arrays: case_val[i] is a case's value, and
// case_label[i] the label of its code. The innermost switch's cases
// start at index sw_first (-1 outside any switch), and sw_default is
// its default's label (-1 if none yet).
static int case_val[MAX_CASES];
static char case_label[MAX_CASES];
static int ncases;
static int sw_first = -1;
static int sw_default = -1;

// Parse the labels before a statement in a switch ("case n:" and
// "default:", any number), and place them, recording each in the
// innermost switch's cases. A case value must be an integer constant
// expression, not already used in this switch.
static void parse_case_labels(void)
{
#ifdef CCISH_LINT
  char buf[8];                      // the duplicate check's
  int i;
#endif
  int l, v;
  while (T_IS_CASE_LABEL(tok)) {
    if (sw_first < 0)
      error(tok == T_CASE ? 133 /* "'case' is not inside a switch" */
                          : 134 /* "'default' is not inside a switch" */,
NULL);
    l = new_label();
    if (tok == T_DEFAULT) {
      next();
#ifdef CCISH_LINT
      if (sw_default >= 0)
        error0(135 /* "more than one 'default' in a switch" */);
#endif
      sw_default = l;
    } else {
      next();
      v = parse_const(136 /* "a case label must be an integer constant" */,
                      204 /* "(a case label must be one)" */);
#ifdef CCISH_LINT
      for (i = sw_first; i < ncases; i++)
        if (case_val[i] == v) {
          // A copy: reporting uses num_text() too.
          strcpy(buf, num_text(v));
          error(137 /* "duplicate case value" */, buf);
        }
#endif
      if (ncases >= MAX_CASES)
        fatal0(138 /* "too many case labels in the switches open at "
                      "once" */);
      case_val[ncases] = v;
      case_label[ncases++] = l;
#ifdef CCISH_STATS
      if (ncases > stat_cases)
        stat_cases = ncases;
#endif
    }
    expect(':', 139 /* "expected ':' after a case label, not" */);
    place_label(l);
  }
}

// Sort the innermost switch's cases by value, as SEL needs on the 6502
// VM, and check that they span at most 32767, another of its limits
// (approach.md, "M7 design"). (Its other limit, 255 cases, is kept by
// MAX_CASES.) An insertion sort: switches are small.
static void sort_cases(void)
{
  int i, j, v, l;
  for (i = sw_first + 1; i < ncases; i++) {
    v = case_val[i];
    l = case_label[i];
    for (j = i; j > sw_first && case_val[j - 1] > v; j--) {
      case_val[j] = case_val[j - 1];
      case_label[j] = case_label[j - 1];
    }
    case_val[j] = v;
    case_label[j] = l;
  }
  // Written so that it can't overflow with a 16-bit int.
  if (ncases > sw_first && case_val[sw_first] < 0
      && case_val[ncases - 1] > case_val[sw_first] + 32767)
    error0(140 /* "case values too far apart (more than 32767)" */);
}

// One entry of a case table: value v, leading to label l.
static void case_entry(int v, int l)
{
  if (!suppress)
    label_used[l] = 1;
  put(IR_CASEVAL, v, -1);
  put(IR_CASETO, l, -1);
}

// switch (e) s, with PLASMA's SEL and a case table after the body,
// since the cases aren't known until it is parsed (approach.md, "M7
// design"):
//   e SEL Ltab  s  BRNCH Lbrk  Ltab: count (value label)...  [BRNCH Ldef]
//   Lbrk:
// SEL takes the value off the stack, and goes to a case's label, or
// past the table if none matches: to the default, or out. A switch with
// no cases gets one entry, leading where no match goes, since a table
// can't be empty.
static void parse_switch(void)
{
  Operand o;
  int tab = new_label(), brk = new_label(), first = sw_first;
  int def = sw_default, i;

  next();                         /* "switch" */
  expect('(', 141 /* "expected '(' after 'switch', not" */);
  begin_expr();
  parse_expr(PREC_COMMA, &o);
  if (o.kind == K_VOID || is_struct(o.type)
      || is_pointer_value(o.type))
    error0(142 /* "a switch needs an integer value" */);
  rvalue(&o);
  branch(IR_SEL, tab);
  end_expr();
  expect(')', 143 /* "expected ')' after the switch value, not" */);
  sw_first = ncases;
  sw_default = -1;
  loop_body(brk, nloops > 0 ? cont_label[nloops - 1] : -1);
  branch(IR_JUMP, brk);           // the last case doesn't run the table
  place_label(tab);
  sort_cases();
  put(IR_CASES, ncases > sw_first ? ncases - sw_first : 1, -1);
  for (i = sw_first; i < ncases; i++)
    case_entry(case_val[i], case_label[i]);
  if (ncases == sw_first)
    case_entry(0, sw_default >= 0 ? sw_default : brk);
  if (sw_default >= 0)
    branch(IR_JUMP, sw_default);
  place_forward(brk);
  ncases = sw_first;
  sw_first = first;
  sw_default = def;
}

// Parse one statement (or declaration) and emit its code.
static void parse_statement(void)
{
  Operand o;
  int else_l, end_l;
  if (tok == T_ID && next_is_colon()) {
    parse_label();              /* label: (before the typedef check, as
                                  a label may spell a typedef) */
    return;
  }
  if (is_type_start()) {
    parse_declaration(0);       // int x; or T x; with a typedef name T
    return;
  }
  switch (tok) {
  case T_IF:
    // if (c) s1 [else s2]:  c BRFLS Lelse  s1  [BRNCH Lend]  Lelse:
    // [s2  Lend:]. Here, not in a function of its own, and an else-if
    // chain is a loop that shares one Lend, not a recursion through
    // this function: the parser's recursion costs the 6502 stack 4
    // bytes a call, and nothing else bounds a chain's length.
    end_l = -1;
    for (;;) {
      else_l = new_label();
      next();                     /* "if" */
      parse_paren_cond(else_l, 0);
      parse_statement();
      if (tok != T_ELSE) {
        place_forward(else_l);
        break;
      }
      next();                     /* "else" */
      if (end_l < 0)
        end_l = new_label();
      branch(IR_JUMP, end_l);
      place_forward(else_l);
      if (tok != T_IF) {
        parse_statement();
        break;
      }
    }
    if (end_l >= 0)
      place_forward(end_l);
    return;
  case T_WHILE:
    parse_while();
    return;
  case T_DO:
    parse_do();
    return;
  case T_FOR:
    parse_for();
    return;
  case T_SWITCH:
    parse_switch();
    return;
  case T_CASE:
  case T_DEFAULT:
    parse_case_labels();
    parse_statement();          // the statement they label
    return;
  case T_BREAK:
  case T_CONTINUE:
    parse_break_continue();
    return;
  case T_GOTO:
    parse_goto();
    return;
  case T_ELSE:
    error0(144 /* "'else' without a matching 'if'" */);
    return;
  case '{':
    parse_block(1);
    return;
  case ';':
    next();
    return;
  case T_RETURN:
    next();
    if (tok == ';') {
      if (fn_type[cur_fn] != TY_VOID)
        push_zero_result(fn_type[cur_fn]);  // C89 allows this
    } else {
#ifdef CCISH_LINT
      if (fn_type[cur_fn] == TY_VOID)
        error(145 /* "void function returns a value:" */,
              quote(fn_name[cur_fn]));
#endif
      begin_expr();
      parse_expr(PREC_COMMA, &o);
      check_assign(fn_type[cur_fn], &o, 199 /* "return" */, NULL);
      if (fn_type[cur_fn] == TY_CHAR)
        to_char(&o);        // a char function returns 0 to 255
      if (is_struct(fn_type[cur_fn]))
        push_struct_words(&o);  // returned as its words
      else
        rvalue(&o);
      end_expr();
    }
    expect(';', 146 /* "expected ';' after return value, not" */);
    put(IR_RETURN, result_words(fn_type[cur_fn]), -1);
    return;
  case T_TYPEDEF:
  case T_CONSTEXPR:
    parse_typedef();
    return;
  case T_STATIC_ASSERT:
    parse_static_assert();
    return;
  case T_EXTERN:
    next();
    if (!is_type_start())
      error(218 /* "expected a type after 'extern', not" */, tok_desc());
    parse_block_extern();
    return;
  case T_STATIC:
    next();
    if (!is_type_start())
      error(147 /* "expected a type after 'static', not" */, tok_desc());
    parse_declaration(1);
    return;
  case T_REGISTER:
    parse_declaration(0);       // which reads the "register"
    return;
  case T_KEYWORD:
    not_yet();
    return;
  }
  parse_expr_statement();
  expect(';', 148 /* "expected ';' after expression, not" */);
}

// A { } block. new_scope is 0 for a function body, whose outermost
// block shares the parameters' scope, as in C.
void parse_block(int new_scope)
{
  int saved_locals = nvars - nglobals, saved_next = frame_next;
  enter_nest();
  if (new_scope)
    depth++;
  next();                         // '{'
  while (tok != '}') {
    if (tok == T_EOF)
      error0(149 /* "missing '}' at end of file" */);
    parse_statement();
  }
  next();
  if (new_scope) {
    // The block's variables and struct tags go out of scope, and
    // the variables' frame slots can be reused by later blocks.
    depth--;
    nvars = nglobals + saved_locals;
    frame_next = saved_next;
    tag_hide(depth);
    id_hide(depth);
  }
  nest--;
}

// ---------- Declarations ----------

// Parse a parameter list starting at '('. Returns the parameter count,
// or -1 for "()" (unspecified in a prototype, none in a definition).
// They are pushed as variables above the globals; an unnamed one
// (allowed in a prototype) has no name.
int parse_params(void)
{
  int n = 0;
  nvars = nglobals;
  frame_next = frame_max = 0;
  depth = 1;
  next();                         // '('
  if (tok == ')') {
    next();
    return -1;
  }
  for (;;) {
    int name, type;
    if (tok == T_ELLIPSIS)
      error0(150 /* "variable argument lists are not supported yet" */);
    if (tok == T_KEYWORD)
      not_yet();
    if (!is_type_start())
      error(151 /* "expected a parameter type, not" */, tok_desc());
    // A prototype may leave the name out (name -1).
    type = parse_declarator(parse_base_type(), D_ABSTRACT, &name);
    if (UNQUAL(type) == TY_VOID) {
      if (type == TY_VOID && n == 0 && name < 0 && tok == ')') {
        next();
        return 0;           // (void): no parameters
      }
#ifdef CCISH_LINT
      error0(152 /* "a parameter can't have type void" */);
#endif
    }
    if (n >= MAX_PARAMS)
      fatal0(153 /* "too many parameters" */);
    // An array parameter is a pointer to its first element, as in
    // C: int a[] and int a[10] are int *a, and int m[][4] is a
    // pointer to arrays of 4 ints. A function parameter is a pointer
    // to the function: int g(int) is int (*g)(int) (C11 6.7.6.3).
    if (is_array(type))
      type = pointer_to(type_sub(type));
    else if (is_func(type))
      type = pointer_to(type);
    check_byval(type, 1);
    // ENTER puts the parameters' words in consecutive frame words,
    // one each for a scalar (a char is then read as its low byte),
    // and a struct's words in its own layout. Parameters are the
    // first variables allocated, so add_local puts them there.
    add_local(name, type, is_struct(type) ? 2 * struct_words(type) : 2);
    n++;
    if (tok != ',')
      break;
    next();
  }
  expect(')', 154 /* "expected ')' after parameters, not" */);
  return n;
}
