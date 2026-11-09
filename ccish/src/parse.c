/*
 * Stage 1, the machinery of the parser (approach.md, issue 5): the
 * functions and variables tables, the CMDSYS table, the type-name
 * parsing, put/hold with the pending constants and the folding, the
 * labels, and the place machinery (rvalue, the loads and stores,
 * push_address, temporaries, spill, swap_blocks, finish_store). The
 * expression grammar is in pexpr.c, the statements and declarations
 * in pstmt.c, and the top level and initializers in ptop.c, which
 * also lists the language the parser covers. It emits IR ops as it
 * goes (ir_put) and builds no syntax tree.
 *
 * A function must be declared before it is called (no implicit
 * declarations), except the CMDSYS functions in cmdsys_sigs[] below,
 * which their first call declares. A function called but not defined
 * in the file must be one of those; it is imported from CMDSYS.
 *
 * Streaming and assignment: a variable's load is not emitted as soon as
 * the variable is parsed, because it may turn out to be the target of
 * an assignment (x = ..., x += ..., x++). Instead the parser returns a
 * *pending* operand, and emits the load only once it knows the value is
 * needed (approach.md 4(a), option A). An object reached through a
 * pointer, and an array element, are pending the same way, so that
 * a[i] = x needs no temporary; see the Operand kinds in parse.h.
 *
 * Reordering: when an address has been emitted before the parser sees
 * that it is stored into (*d++ = *s++), the value's code is emitted
 * after it, and then the two blocks are exchanged: the parser marks
 * where blocks start with ir_here() and puts IR_SWAP with their lengths
 * (swap_blocks()), and ccgen moves the code (approach.md 4(d2) and
 * 18.7, A). i + p is reordered the same way, to scale i after p.
 *
 * Constant folding works the same way. A constant is a *pending
 * constant* until its value is needed on the stack, so unary operators
 * on it, and binary operators with two of them, are computed at compile
 * time with C's 16-bit int rules. The left operand of a binary operator
 * can't wait inside its Operand, because the right operand's code must
 * come after it. So it is held on a small stack (held[]), and put()
 * writes out every held constant before any other op. If the right
 * operand emits nothing and is itself a constant, the held left one is
 * still there and the two are folded.
 *
 * An address constant (K_ADDR: a global's or a string's address, plus a
 * constant number of bytes) is pending in the same way (approach.md,
 * "C6 design"), and held the same way as a left operand: plus or minus
 * a constant, and casts to a pointer type, fold into it, so &a[2] + 1
 * or "abc" + 1 is one DATAADDR or STRADDR. A global's initializer is an
 * ordinary expression, parsed with code off, which must end as a
 * constant or an address constant.
 *
 * Each operand carries its C type (see type.c). char is unsigned, and a
 * char value on the stack is always 0 to 255: loads use LLB, and stores
 * into a char keep only the low byte. So a value needs converting to
 * char (ANDI 255) only for a cast or a char function's return value.
 *
 * Control flow uses labels, numbered per function; stage 4 resolves
 * them with fixup chains. Code that a constant makes impossible to
 * reach inside an expression (the x in 0 && x, or the unchosen side of
 * 1 ? a : b) is parsed with put() switched off (suppress), so it is
 * checked but emits nothing. In statements, such as if (0) or
 * while (0), the parser emits a jump around the code, and the peephole
 * stage drops the unreachable code after it.
 */
#ifdef CCISH_STATS
#include <stdio.h>
#endif
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "fixed.h"
#include "nam.h"
#include "tokin.h"
#include "obj.h"
#include "ir.h"
#include "name.h"
#include "type.h"
#include "parse.h"
#include "pstmt.h"
#include "irout.h"


/* Functions: see parse.h. */
int fn_name[MAX_FUNCS];
char fn_type[MAX_FUNCS];
int fn_ptypes[MAX_FUNCS];
char fn_nparams[MAX_FUNCS];
char fn_flags[MAX_FUNCS];
int nfuncs;

int depth;           /* block depth; parameters are at depth 1 */
int frame_next;      /* next free frame offset */
int frame_max;       /* frame size needed so far */
int cur_fn = -1;     /* the function being parsed, or -1 */
int in_params;       /* parsing a function's parameter list */
int nest;            /* current nesting depth, to limit recursion */

#ifdef CCISH_DIAG
/* Return the spelling of name (an id) in single quotes, for error
 * messages. The result is in a static buffer. */
const char *quote(int name)
{
  static char buf[MAX_NAME + 3];
  strcpy(buf, "'");
  strcat(buf, nam_name(name));
  strcat(buf, "'");
  return buf;
}
#endif

/* Report that the current token starts a C feature that isn't
 * supported yet. */
void not_yet(void)
{
  error(24 /* "not supported yet:" */, tok_desc());
}

/* Skip the current token, which must be t; otherwise report message
 * msg (what was expected) and what was found. */
void expect(int t, int msg)
{
  if (tok != t)
    error(msg, tok_desc());
  next();
}

/* Count one more level of nesting, so the parser's recursion depth
 * stays bounded. The caller decrements nest when done. */
void enter_nest(void)
{
  if (++nest > MAX_NEST)
    fatal(25 /* "nesting too deep" */, NULL);
}

/* find_func() has two versions. With CCISH_FAST (build.h), a pointer
 * walks fn_name[]: compiled by ccish, each pass of the scan is 10 ops
 * instead of 12, and every lookup of a function's name scans up to all
 * the functions, but the function is 53 bytes instead of 41 (the end
 * pointer, and the pointer turned back into an index, cost more than
 * the loop saves). Size matters most for now, so the indexed loop is
 * the default (approach.md, "The source rewrites are done"). */
#ifdef CCISH_FAST
/* The function named name (an id), or -1: the faster version. */
int find_func(int name)
{
  const int *p, *end = fn_name + nfuncs;
  for (p = fn_name; p != end; p++)
    if (*p == name)
      return (int)(p - fn_name);
  return -1;
}
#else
/* The function named name (an id), or -1: the smaller version. */
int find_func(int name)
{
  int f;
  for (f = 0; f < nfuncs; f++)
    if (fn_name[f] == name)
      return f;
  return -1;
}
#endif

/* Add a function, not yet declared or defined, with an unknown
 * parameter count, and return it. */
int add_func(int name)
{
  if (nfuncs >= MAX_FUNCS)
    fatal(26 /* "too many functions" */, NULL);
  fn_name[nfuncs] = name;
  fn_type[nfuncs] = TY_INT;
  fn_ptypes[nfuncs] = -1;
  fn_flags[nfuncs] = 0;
  return nfuncs++;
}

/* Function f's parameter count, or -1 if it isn't known yet. */
int nparams_of(int f)
{
  return fn_flags[f] & FN_COUNTED ? fn_nparams[f] : -1;
}

/* Record that function f has n parameters (none if n is -1: still
 * unknown). */
void set_nparams(int f, int n)
{
  if (n < 0)
    return;
  fn_nparams[f] = (char)n;
  fn_flags[f] |= FN_COUNTED;
}

/*
 * The CMDSYS functions a program may call, from src/inc/cmdsys.plh,
 * with their types as a signature: the return type, then each
 * parameter's type, as v (void), i (int), c (char), s (char *),
 * S (const char *), p (void *) or P (const void *). A pointer the
 * function only reads is const, so that passing a const char * needs
 * no cast. Their meanings are PLASMA's, not C's, so their C names are
 * PLASMA's with "__" in front (ppname.c's fixed ids), leaving the
 * standard names to C's own functions: __puts() takes a PLASMA
 * (length-prefixed) string and adds no newline, and __strcpy() copies
 * PLASMA strings. divmod() returns two values and is left out.
 */
static const char *const cmdsys_sigs[NCMDSYS] = {
  "vc", "v", "vS", "vi",                /* putc putln puts puti */
  "vi", "vi", "i", "sc",                /* putb puth getc gets */
  "iiiiii", "iip", "p",                 /* call syscall heapmark */
  "piip", "pi", "ip",                   /* heapallocalign heapalloc
                                         * heaprelease */
  "i", "vpii", "vpPi",                  /* heapavail memset memcpy */
  "ssS", "ssS", "ii", "ii",             /* strcpy strcat toupper sext */
  "iii", "iii", "iii", "iii"            /* isugt isuge isult isule */
};

/* The index of name (an id) in cmdsys_sigs[], or -1 if it isn't a
 * CMDSYS function: they are the fixed ids 1 to NCMDSYS, in that order
 * (CCPP.md, "The fixed ids"). */
int find_cmdsys(int name)
{
  return name >= 1 && name <= NCMDSYS ? name - 1 : -1;
}

/*
 * Variables: one scope stack of parallel arrays, each MAX_VARS long
 * (approach.md, "Sym split design"). The globals are at the bottom,
 * from 0 to nglobals; a function's parameters and locals are above
 * them, up to nvars, innermost last, and are popped when it ends (no
 * locals exist while globals are declared). Variable v has name
 * var_name[v] (an id, or -1 for an unnamed prototype
 * parameter), type var_type[v], offset var_off[v] (a frame offset, or
 * with V_DATA a data offset), and flags var_flags[v]: the block depth it
 * was declared at (V_DEPTH; 0 for a global), V_DATA if it is in the data
 * segment (a global or a static local), and V_KNOWN if reading it gives
 * var_val[v] at compile time (a const scalar with a constant
 * initializer, not volatile).
 */
int var_name[MAX_VARS];
int var_type[MAX_VARS];
int var_off[MAX_VARS];
char var_flags[MAX_VARS];
int var_val[MAX_VARS];
int nvars, nglobals;
#ifdef CCISH_STATS
static int nvars_peak;              /* the most visible at once */
#endif

char var_link[MAX_VARS];     /* its linkage: the VL_* bits (parse.h) */

/* The innermost visible variable named name (an id), or -1. */
int find_var(int name)
{
  int v;
  if (name < 0)
    return -1;
  for (v = nvars - 1; v >= 0; v--)
    if (var_name[v] == name)
      return v;
  return -1;
}

/* The block depth variable v was declared at (0 for a global). */
static int var_depth(int v)
{
  return var_flags[v] & V_DEPTH;
}

/* Is variable v in the data segment (a global or a static local)? A
 * macro: one caller. */
#define var_data(v) ((var_flags[v] & V_DATA) != 0)

/* The visible enum constant or typedef named name, or -1. One hides a
 * variable or function declared less deep, and is hidden by one
 * declared deeper (typedef int T; void f() { int T; } makes T a
 * variable inside f). They can't be at the same depth: that is a
 * redeclaration. */
int find_id(int name)
{
  int i = id_find(name), v;
  if (i < 0)
    return -1;
  v = find_var(name);
  if (v >= 0 && var_depth(v) >= id_depth(i))
    return -1;
  return i;
}

/* The type a visible typedef named name stands for, or -1 if name
 * isn't one. */
int typedef_type(int name)
{
  int i = find_id(name);
  return i >= 0 && id_kind(i) == ID_TYPEDEF ? id_val(i) : -1;
}

/* Report a redeclaration if an enum constant or typedef named name was
 * declared at the current depth (where a variable, function or another
 * of them named name is about to be). */
void check_not_id(int name)
{
  int i = id_find(name);
  if (i >= 0 && id_depth(i) == depth)
    error(27 /* "redeclaration of" */, quote(name));
}

/* Report a redeclaration if a variable named name was declared at
 * the current depth, or, at file scope, a function. */
void check_not_var(int name)
{
  int v = find_var(name);
  if (v >= 0 && var_depth(v) == depth)
    error(27 /* "redeclaration of" */, quote(name));
  if (depth == 0 && find_func(name) >= 0)
    error(27 /* "redeclaration of" */, quote(name));
}

/* Push a variable named name (an id, or -1 for an unnamed prototype
 * parameter) of type type at the current depth, at offset off, with
 * flags (V_DATA or 0), and return it. */
int push_var(int name, int type, int off, int flags)
{
  if (name >= 0) {
    check_not_var(name);
    check_not_id(name);
  }
  if (nvars >= MAX_VARS)
    fatal(28 /* "too many variables" */, NULL);
  var_name[nvars] = name;
  var_type[nvars] = type;
  var_off[nvars] = off;
  var_flags[nvars] = (char)(depth | flags);
  var_link[nvars] = 0;
#ifdef CCISH_STATS
  if (nvars + 1 > nvars_peak)
    nvars_peak = nvars + 1;
#endif
  return nvars++;
}

/* Declare a global variable of the given type, at the next data
 * offset. The caller then writes its bytes to the data stream: its
 * initial value, or zeros. */
int add_global(int name, int type)
{
  int v;
  if (nvars == nglobals) {
    nglobals++;
    return push_var(name, type, data_offset(), V_DATA);
  }
  /* An extern in a block (parse_block_extern()) declares one while
   * locals are visible: they move up a slot. Nothing keeps a local's
   * index from one declaration to the next, and blocks restore their
   * locals by count (parse_block()), so that's safe. */
  if (nvars >= MAX_VARS)
    fatal(28 /* "too many variables" */, NULL);
  for (v = nvars++; v > nglobals; v--) {
    var_name[v] = var_name[v - 1];
    var_type[v] = var_type[v - 1];
    var_off[v] = var_off[v - 1];
    var_flags[v] = var_flags[v - 1];
    var_val[v] = var_val[v - 1];
    var_link[v] = var_link[v - 1];
  }
  var_name[v] = name;
  var_type[v] = type;
  var_off[v] = data_offset();
  var_flags[v] = V_DATA;
  var_link[v] = 0;
  nglobals++;
  return v;
}

/* Declare a local (or parameter) of the given type at the current
 * depth, and give it a frame slot of size bytes. An unnamed prototype
 * parameter has the name -1. */
int add_local(int name, int type, int size)
{
  int v = push_var(name, type, frame_next, 0);
  if (frame_next + size > 255)
    error(30 /* "too many local variables in" */, quote(fn_name[cur_fn]));
  frame_next += size;
  if (frame_next > frame_max)
    frame_max = frame_next;
  return v;
}

/* Parameter types of declared functions, n at a time; fn_ptypes[] says
 * where a function's start. */
char param_types[MAX_PARAM_TYPES];  /* unqualified types */
static int nparam_types;

#ifdef CCISH_STATS
/* For report_stats() (ptop.c): the most variables visible at once, and
 * the parameter types used. */
void parse_stats(int *vars_peak, int *ptypes)
{
  *vars_peak = nvars_peak;
  *ptypes = nparam_types;
}
#endif

/* Record that f's n parameters have the types of its first n
 * variables above the globals (the
 * parameters just parsed), or check them against the types an earlier
 * declaration gave. The caller has already checked the count. A
 * parameter's own qualifiers don't count, as in C: f(const int n) and
 * f(int n) are the same function type. */
void set_param_types(int f, int n)
{
  int i;
  if (fn_ptypes[f] >= 0) {
    for (i = 0; i < n; i++)
      if (param_types[fn_ptypes[f] + i]
          != unqual(var_type[nglobals + i]))
        error(31 /* "conflicting parameter types for" */,
              quote(fn_name[f]));
    return;
  }
  if (nparam_types + n > MAX_PARAM_TYPES)
    fatal(32 /* "too many function parameters in all" */, NULL);
  fn_ptypes[f] = nparam_types;
  for (i = 0; i < n; i++)
    param_types[nparam_types++] = unqual(var_type[nglobals + i]);
}

/* Does the current token start a type name: a type keyword, a
 * qualifier, or a visible typedef name? */
int is_type_start(void)
{
  return T_IS_TYPE_START(tok)
         || (tok == T_ID && typedef_type(tok_val) >= 0);
}

#ifdef CCISH_DIAG
/* Describe tag for a message: 'struct name' or 'union name', or an
 * unnamed struct or union. The result may be in a static buffer. */
const char *tag_desc(int tag)
{
  static char buf[MAX_NAME + 10];
  if (tag_name(tag) < 0)
    return tag_is_union(tag) ? "an unnamed union" : "an unnamed struct";
  strcpy(buf, tag_is_union(tag) ? "'union " : "'struct ");
  strcat(buf, nam_name(tag_name(tag)));
  strcat(buf, "'");
  return buf;
}
#endif

/* Report error msg (such as "a variable can't have the incomplete
 * type") if t is an incomplete struct type, which has no size yet. */
void need_complete(int t, int msg)
{
  if (is_struct(t) && type_size(t) == 0)
    error(msg, tag_desc(struct_tag(t)));
}

/* Parse any qualifiers (const, volatile), and return them as Q_ bits. */
int parse_quals(void)
{
  int q = 0;
  while (T_IS_QUAL(tok)) {
    q |= tok == T_CONST ? Q_CONST : Q_VOLATILE;
    next();
  }
  return q;
}


/* Set by parse_base_type(): the type was a struct or enum, which a
 * declaration may declare with no declarators (struct pt { ... };). */
int base_tagged;

/* Parse int, char, signed and unsigned, in any order, with any
 * qualifiers among them (unsigned const int), adding those to *q, and
 * return the type: unsigned int (TY_UINT), int, or char. unsigned char
 * is plain char, which is unsigned; signed char isn't supported yet. */
static int parse_int_type(int *q)
{
  int base = 0, sign = 0;
  while (T_IS_INT_TYPE(tok) || T_IS_QUAL(tok)) {
    if (T_IS_QUAL(tok)) {
      *q |= parse_quals();
      continue;
    }
    if (tok == T_INT || tok == T_CHAR ? base != 0 : sign != 0)
      error(226 /* "conflicting or repeated type specifier:" */,
            tok_desc());
    if (tok == T_INT || tok == T_CHAR)
      base = tok;
    else
      sign = tok;
    next();
  }
  if (base == T_CHAR) {
    if (sign == T_SIGNED)
      error(227 /* "'signed char' is not supported yet" */, NULL);
    return TY_CHAR;
  }
  return sign == T_UNSIGNED ? TY_UINT : TY_INT;
}

/* Parse a basic type name (int, char, void, unsigned, a struct or enum
 * type, or a typedef name), with any qualifiers before or after it
 * (const char, char const), and return its type. */
int parse_base_type(void)
{
  int q = parse_quals(), t = TY_INT;
  int tagged = T_IS_TAGGED(tok);  /* after the quals */
  if (tok == T_STRUCT || tok == T_UNION) {
    t = parse_struct(tok == T_UNION);
  } else if (tok == T_ENUM) {
    t = parse_enum();
  } else if (T_IS_INT_TYPE(tok)) {
    t = parse_int_type(&q);
  } else {
    if (tok == T_VOID)
      t = TY_VOID;
    else if (tok == T_ID && typedef_type(tok_val) >= 0)
      t = typedef_type(tok_val);
    else
      error(33 /* "expected a type, not" */, tok_desc());
    next();
  }
  base_tagged = tagged;           /* after any members' own types */
  return qualify(t, q | parse_quals());
}

/* The type a letter stands for in a cmdsys_sigs[] signature: S and P
 * are the const forms of s and p, for what a function only reads. */
static int sig_type(char c)
{
  switch (c) {
  case 'v': return TY_VOID;
  case 'c': return TY_CHAR;
  case 's': return pointer_to(TY_CHAR);
  case 'S': return pointer_to(TY_CHAR | Q_CONST);
  case 'p': return pointer_to(TY_VOID);
  case 'P': return pointer_to(TY_VOID | Q_CONST);
  }
  return TY_INT;
}

/* Declare f as CMDSYS function c of cmdsys_sigs[], with its types. */
void declare_cmdsys(int f, int c)
{
  const char *sig = cmdsys_sigs[c];
  int i, n = (int)strlen(sig) - 1;
  if (nparam_types + n > MAX_PARAM_TYPES)
    fatal(32 /* "too many function parameters in all" */, NULL);
  fn_type[f] = sig_type(sig[0]);
  set_nparams(f, n);
  fn_ptypes[f] = nparam_types;
  for (i = 0; i < n; i++)
    param_types[nparam_types++] = sig_type(sig[i + 1]);
  fn_flags[f] |= FN_DECLARED;
}

/* Does f's declaration match CMDSYS function c's types? Parameter
 * types that were never declared (int memset();) match. */
int matches_cmdsys(int f, int c)
{
  const char *sig = cmdsys_sigs[c];
  int i, n = (int)strlen(sig) - 1;
  if (fn_type[f] != sig_type(sig[0]) || nparams_of(f) != n)
    return 0;
  if (fn_ptypes[f] >= 0)
    for (i = 0; i < n; i++)
      if (param_types[fn_ptypes[f] + i] != sig_type(sig[i + 1]))
        return 0;
  return 1;
}

/* ---------- Constants ---------- */

/* Left operands held back for folding (see the comment at the top),
 * each the IR op that pushes it: a constant, or an address constant's
 * DATAADDR or STRADDR, with its operands. Held operands nest only as
 * deep as 1 + (2 + (3 + ...)) does; past MAX_HELD, a left operand is
 * just emitted, and folds no more. */
static char held_op[MAX_HELD];
static int held_a[MAX_HELD];
static int held_f[MAX_HELD];
int nheld;

/* Above 0 while a global's initializer is parsed for an address
 * constant: its string literals take data space, though no code is
 * emitted (suppress). */
int want_addr;

/* Above 0 while parsing code that can never run, such as the right side
 * of 0 && x: it is checked, but put() emits nothing. */
int suppress;

/* Above 0 while parsing the operand of sizeof (which suppress also
 * covers). */
int in_sizeof;

/* Emit an IR op (with function f, or -1), after any held constants. All
 * of the parser's IR goes through here. */
void put(int op, int a, int f)
{
  int i;
  if (suppress)
    return;
  for (i = 0; i < nheld; i++)
    ir_put(held_op[i], held_a[i], held_f[i]);
  nheld = 0;
  ir_put(op, a, f);
}

/* Hold back the op that pushes a left operand (op, a, f), for put() to
 * write out before any other; return its place. The caller checks that
 * there is room (nheld < MAX_HELD). */
int hold(int op, int a, int f)
{
  held_op[nheld] = (char)op;
  held_a[nheld] = a;
  held_f[nheld] = f;
  return nheld++;
}

/* Where variable v is, as an Operand's global field: 0 in the frame, 1
 * in this file's data, or 2 + v for a global reached through its symbol,
 * which is then used (unless no code is emitted): one declared extern
 * with no storage here, or one in the zero-filled space (VL_BSS), whose
 * address only ccrel knows. */
int var_where(int v)
{
  if (!(var_link[v] & (VL_EXTERN | VL_BSS)))
    return var_data(v);
  if (!suppress)
    var_link[v] |= VL_USED;
  return 2 + v;
}

/* The low 16 bits of u, as a signed 16-bit int. Unsigned arithmetic is
 * used so that this works whether the host's int has 16 or 32 bits. */
int wrap16(unsigned u)
{
  u &= 0xFFFF;
  if (u & 0x8000)
    return -(int)(0xFFFF - u) - 1;
  return (int)u;
}

/* The absolute value of v, as unsigned so that -32768 works too. */
static unsigned magnitude(int v)
{
  return v < 0 ? 0u - (unsigned)v : (unsigned)v;
}

/* Reject a constant right operand that would make op undefined in C.
 * The VMs disagree on these cases too (division by zero crashes plvm;
 * shift counts use only the low byte on the 6502). */
void check_right_const(int op, int v)
{
  if (IR_IS_DIVMOD(op) && v == 0)
    error(34 /* "division by zero" */, NULL);
  if (IR_IS_S_SHIFT(op) && (v < 0 || v > 15))
    error(35 /* "shift count out of range (0 to 15):" */,
          v < 0 ? "negative" : "too big");
}

/* a op b, as the VM computes it: C's rules for a 16-bit int. / truncates
 * toward zero, % has the dividend's sign, >> is arithmetic, and
 * comparisons give 1 or 0. If uns is set, the operation is unsigned
 * (/, %, >> and the comparisons differ). Cases check_right_const()
 * rejects give 0 (they only get here in code that can never run). */
int fold(int op, int a, int b, int uns)
{
  unsigned ua = (unsigned)a, ub = (unsigned)b, q;
  if ((IR_IS_DIVMOD(op) && b == 0)
      || (IR_IS_S_SHIFT(op) && (b < 0 || b > 15)))
    return 0;
  if (uns) {
    ua &= 0xFFFF;
    ub &= 0xFFFF;
    switch (op) {
    case IR_LT:  return ua < ub;
    case IR_GT:  return ua > ub;
    case IR_LE:  return ua <= ub;
    case IR_GE:  return ua >= ub;
    case IR_DIV: return wrap16(ua / ub);
    case IR_MOD: return wrap16(ua % ub);
    case IR_SHR: return wrap16(ua >> b);
    }
  }
  switch (op) {
  case IR_EQ:  return a == b;
  case IR_NE:  return a != b;
  case IR_LT:  return a < b;
  case IR_GT:  return a > b;
  case IR_LE:  return a <= b;
  case IR_GE:  return a >= b;
  case IR_ADD: return wrap16(ua + ub);
  case IR_SUB: return wrap16(ua - ub);
  case IR_MUL: return wrap16(ua * ub);
  case IR_DIV:
    q = magnitude(a) / magnitude(b);
    return wrap16((a < 0) != (b < 0) ? 0u - q : q);
  case IR_MOD:
    q = magnitude(a) % magnitude(b);
    return wrap16(a < 0 ? 0u - q : q);
  case IR_AND: return wrap16(ua & ub);
  case IR_OR:  return wrap16(ua | ub);
  case IR_XOR: return wrap16(ua ^ ub);
  case IR_SHL: return wrap16(ua << b);
  case IR_SHR:
    /* >> of a negative int is implementation-defined in C, so
     * shift the non-negative ~a instead. */
    return a < 0 ? ~(~a >> b) : a >> b;
  }
  return 0;
}

/* The op that gives the same result as binary IR op with its operands
 * swapped (op itself if it is commutative, > for <, ...), or -1. */
int swapped_op(int op)
{
  switch (op) {
  case IR_ADD: case IR_MUL: case IR_AND: case IR_OR: case IR_XOR:
  case IR_EQ: case IR_NE:
    return op;
  case IR_LT: return IR_GT;
  case IR_GT: return IR_LT;
  case IR_LE: return IR_GE;
  case IR_GE: return IR_LE;
  }
  return -1;
}

/* Is IR op a comparison (its result is always 1 or 0)? */
int is_compare(int op)
{
  return IR_IS_COMPARE(op);
}

/* ---------- Labels ---------- */

/* Labels are numbered per function. Stage 4 resolves them. Ordinary
 * labels (loops, branches, cases) take 0 up; goto labels take a
 * reserved block at the top (ir.h), so ccgen can tell a goto and its
 * target from the rest by the number alone. */
int nlabels;
int ngoto_labels;             /* goto labels made (from GOTO_LABEL_BASE) */
char label_used[MAX_LABELS];  /* some branch to it so far */

/* The current function's goto labels: lbl_id[i] is a label's id (its
 * identifier's), lbl_num[i] its goto label, and lbl_def[i] whether its
 * ':' has been seen. C allows a goto before its label, so a forward
 * goto reserves the label (undefined so far) and the later ':' defines
 * it; a label never defined is an error, checked at the function's end
 * (check_goto_labels). */
static int lbl_id[GOTO_LABELS];
static int lbl_num[GOTO_LABELS];
static char lbl_def[GOTO_LABELS];
int nlbls;                          /* (parse.h) */

/* Allocate a new ordinary label in the current function (0 up, stopping
 * at the entry label, which is reserved for a self tail call's jump). */
int new_label(void)
{
  if (nlabels >= ENTRY_LABEL)
    error(36 /* "too many branches in" */, quote(fn_name[cur_fn]));
  label_used[nlabels] = 0;
  return nlabels++;
}

/* Allocate a new goto label in the current function (the block above
 * the entry label, GOTO_LABEL_BASE up). */
static int new_goto_label(void)
{
  int n = GOTO_LABEL_BASE + ngoto_labels;
  if (ngoto_labels >= GOTO_LABELS)
    error(312 /* "too many goto labels in" */, quote(fn_name[cur_fn]));
  label_used[n] = 0;
  ngoto_labels++;
  return n;
}

/* The goto label reserved or defined for id, or -1 if there is none. */
static int goto_label_find(int id)
{
  int i;
  for (i = 0; i < nlbls; i++)
    if (lbl_id[i] == id)
      return lbl_num[i];
  return -1;
}

/* The goto label for a 'goto id': find it, or reserve it (undefined so
 * far, for the ':' that may come later). */
int reserve_goto_label(int id)
{
  int n = goto_label_find(id);
  if (n < 0) {
    n = new_goto_label();
    lbl_id[nlbls] = id;
    lbl_num[nlbls] = n;
    lbl_def[nlbls] = 0;
    nlbls++;
  }
  return n;
}

/* The goto label for an 'id:': find and define it, or create it.
 * A second ':' for the same label is a duplicate. */
int define_goto_label(int id)
{
  int i, n;
  for (i = 0; i < nlbls; i++)
    if (lbl_id[i] == id) {
      if (lbl_def[i])
        error(313 /* "duplicate label" */, quote(id));
      lbl_def[i] = 1;
      return lbl_num[i];
    }
  n = new_goto_label();
  lbl_id[nlbls] = id;
  lbl_num[nlbls] = n;
  lbl_def[nlbls] = 1;
  nlbls++;
  return n;
}

/* At a function's end: a goto's target that was never defined. */
void check_goto_labels(void)
{
  int i;
  for (i = 0; i < nlbls; i++)
    if (!lbl_def[i])
      error(314 /* "undefined label" */, quote(lbl_id[i]));
}

/* Emit branch op (IR_JUMP, IR_BRFALSE, ...) to label n. */
void branch(int op, int n)
{
  if (!suppress)
    label_used[n] = 1;
  put(op, n, -1);
}

/* Place label n, which backward branches will use (a loop's top). */
void place_label(int n)
{
  put(IR_LABEL, n, -1);
}

/* Place label n, whose branches all came before it. If there were none,
 * it is left out: then nothing can reach the code after it from here,
 * and the peephole stage can drop that code if it is unreachable. */
void place_forward(int n)
{
  if (label_used[n])
    place_label(n);
}

/* Make the branches so far to label from go to label to instead (an
 * IR_JOIN; nothing if there are none, or from is -1). */
void join_label(int from, int to)
{
  if (suppress || from < 0 || !label_used[from])
    return;
  label_used[to] = 1;
  label_used[from] = 0;
  put(IR_JOIN, from, to);
}

/* Branch to label n if the condition *o is false (or, if on_true, if it
 * is true). A constant condition gives a jump or nothing; a K_JUMP's
 * false branches are joined to n. */
void cond_branch(Operand *o, int n, int on_true)
{
  if (o->kind == K_CONST) {
    if ((o->val != 0) == on_true)
      branch(IR_JUMP, n);
    return;
  }
  if (o->kind == K_JUMP) {
    /* It already jumps when false; when true, it jumps to its true
     * label or falls through. */
    if (on_true) {
      join_label(o->off, n);
      branch(IR_JUMP, n);
      place_forward(o->val);
    } else {
      join_label(o->val, n);
      place_true(o);
    }
    return;
  }
  rvalue(o);
  branch(IR_BR_IF(on_true), n);
}

/* ---------- Expressions ---------- */


/* The data symbol for a global IR op on an operand whose global field
 * is global: the extern variable, or -1 for an address in this file's
 * data. */
static int data_sym_of(int global)
{
  return global >= 2 ? global - 2 : -1;
}

/* May the global that global (an Operand's field) names move to the
 * heap? One in this file's zero-filled space may, and so may an extern
 * array or struct that may be one in another file: of unknown size, or
 * of at least BSS_MIN bytes. Its address is then always pushed as
 * IR_HEAPADDR, the one form ccrel can patch, with any offset added
 * after. */
int movable(int global)
{
  int v = global - 2, t;
  if (global < 2)
    return 0;
  t = var_type[v];
  if (var_link[v] & VL_BSS)
    return 1;
  return (is_array(t) || is_struct(t))
         && (type_size(t) == 0 || type_size(t) >= BSS_MIN);
}

/* Push the address of the variable at off (a global's or a local's).
 * For a global that may move, the address and the offset are apart. */
void push_var_addr(int off, int global)
{
  if (movable(global)) {
    put(IR_HEAPADDR, 0, global - 2);
    if (off != 0) {
      put(IR_CONST, off, -1);
      put(IR_ADD, 0, -1);
    }
  } else if (global) {
    put(IR_DATAADDR, off, data_sym_of(global));
  } else {
    put(IR_LOCALADDR, off, -1);
  }
}

/* Push the variable at off, where global says (var_where()), with the
 * access form bits form (acc_form(): a byte, volatile); through its
 * address, for a global that may move. */
static void load_var(int off, int global, int form)
{
  if (movable(global)) {
    push_var_addr(off, global);
    put(IR_FORM(IR_LOAD, form), 0, -1);
  } else if (global) {
    put(IR_FORM(IR_GLOBAL, form), off, data_sym_of(global));
  } else {
    put(IR_FORM(IR_LOCAL, form), off, -1);
  }
}

/* Before a store through an address, keep a copy of the value below
 * it: for a char, its low byte, the value C gives an assignment to one
 * (as DLB keeps it). */
static void keep_copy(int is_char)
{
  if (is_char) {
    put(IR_CONST, 0xFF, -1);
    put(IR_AND, 0, -1);
  }
  put(IR_DUP, 0, -1);
}

/* Store the top value into the variable o (a K_VAR), keeping the stored
 * value (for a char, its low byte) on the stack: through its address,
 * after a copy of the value, for a global that may move. */
void store_var(Operand *o)
{
  int c = o->type == TY_CHAR, form = acc_form(o->type, o->quals);
  if (movable(o->global)) {
    keep_copy(c);
    push_var_addr(o->off, o->global);
    put(IR_FORM(IR_STORE, form), 0, -1);
  } else if (o->global) {
    put(IR_FORM(IR_SETGLOBAL, form), o->off, data_sym_of(o->global));
  } else {
    put(IR_FORM(IR_SETLOCAL, form), o->off, -1);
  }
}

/* The highest offset a variable can have: 255 in the frame, and 32767
 * in the data segment. */
int max_offset(int global)
{
  return global ? 0x7FFF : 255;
}

/* The form bits (IR_FORM, ir.h) of an access to a place of type t with
 * qualifiers quals: IR_ACC_BYTE for a char, IR_VOLATILE if volatile.
 * It relies on IR_ACC_BYTE being 1 and Q_VOLATILE IR_VOLATILE << 4. */
int acc_form(int t, int quals)
{
  return (t == TY_CHAR) | ((quals & Q_VOLATILE) >> 4);
}

/* The IR op that stores a value of type t, with qualifiers quals, at an
 * address (a char's a byte; a volatile's the volatile form). */
int store_op(int t, int quals)
{
  return IR_FORM(IR_STORE, acc_form(t, quals));
}

/* Is o a place (a variable, or an object reached through an address)
 * whose value hasn't been loaded? */
int is_place(Operand *o)
{
  return o->kind >= K_VAR && o->kind <= K_ELEM;
}

/* The type of a value of type t: an array's value is a pointer to its
 * first element, and a function's a pointer to it. */
int value_type(int t)
{
  if (is_func(t))
    return pointer_to(t);
  return is_array(t) ? pointer_to(type_sub(t)) : t;
}

/* Is o an address constant? A global or static array, whose value is
 * its address, becomes one. */
int addr_const(Operand *o)
{
  if (o->kind == K_VAR && o->global && is_array(o->type)) {
    o->kind = K_ADDR;
    o->type = value_type(o->type);
    o->is_bool = 0;
  }
  return o->kind == K_ADDR;
}

/* The IR op that pushes address constant o, with its operands in *a and
 * *f: STRADDR for a string, DATAADDR for data, or HEAPADDR for a global
 * that may move (whose offset must then be 0: push_var_addr() adds
 * one). */
int addr_op(Operand *o, int *a, int *f)
{
  *a = o->off;
  *f = o->global == G_STR ? -1 : data_sym_of(o->global);
  if (o->global != G_STR && movable(o->global))
    return IR_HEAPADDR;
  return o->global == G_STR ? IR_STRADDR : IR_DATAADDR;
}

/* Is type t (or a qualified one: const unsigned) unsigned int? */
int is_uns(int t)
{
  return unqual(t) == TY_UINT;
}

/* The type of an arithmetic op's result on integer types a and b, by
 * C's usual arithmetic conversions: unsigned int if either is, else
 * int (char promotes to int). */
int arith_type(int a, int b)
{
  return is_uns(a) || is_uns(b) ? TY_UINT : TY_INT;
}

/* Integer type t promoted, as for a unary operator or a shift's left
 * operand: unsigned int stays so, the others become int. */
int promoted(int t)
{
  return is_uns(t) ? TY_UINT : TY_INT;
}

/* Make o the K_ELEM at the address in the variable at off (base_arr 0),
 * or at the array at off (base_arr 1), with no index yet. The variable
 * or array is a global if global is set. */
void make_elem(Operand *o, int off, int global, int base_arr)
{
  o->kind = K_ELEM;
  o->off = off;
  o->global = global;
  o->base_arr = base_arr;
  o->idx = -1;
  o->idx_global = 0;
  o->idx_char = 0;
  o->scale = 0;
  o->disp = 0;
  o->is_bool = 0;
  o->assigned = 0;
}

/* Push the address of the K_ELEM o: its base, plus the index times the
 * scale, plus the displacement. For an array, the displacement is part
 * of the LLA or LA offset when the result is a valid offset. */
void push_address(Operand *o)
{
  int d = o->disp;
  if (o->base_arr && o->off + d >= 0
      && o->off + d <= max_offset(o->global)) {
    push_var_addr(o->off + d, o->global);
    d = 0;
  } else if (o->base_arr) {
    push_var_addr(o->off, o->global);
  } else {
    load_var(o->off, o->global, 0);
  }
  if (o->idx >= 0) {
    load_var(o->idx, o->idx_global, IR_CHAR_FORM(0, o->idx_char));
    if (o->scale != 1) {
      put(IR_CONST, o->scale, -1);
      put(IR_MUL, 0, -1);   /* a word index becomes IDXW */
    }
    put(IR_ADD, 0, -1);
  }
  if (d != 0) {
    put(IR_CONST, d, -1);
    put(IR_ADD, 0, -1);
  }
}

/* Place a K_JUMP's pending true label here, where the code goes on
 * when it is true. */
void place_true(Operand *o)
{
  if (o->off >= 0)
    place_forward(o->off);
  o->off = -1;
}

/* Make sure the operand's value is on the stack. An array's value is
 * the address of its first element (for K_DEREF, already there), and a
 * function's is its address (for any but K_FUNC, the operand's value:
 * see parse.h). */
void rvalue(Operand *o)
{
  if (is_struct(o->type))
    error(37 /* "a struct can't be used as a value here" */, NULL);
  if (is_array(o->type) && is_place(o)) {
    if (o->kind == K_VAR)
      push_var_addr(o->off, o->global);
    else if (o->kind == K_ELEM)
      push_address(o);
    o->kind = K_VALUE;
    o->type = value_type(o->type);
    o->is_bool = 0;
    return;
  }
  switch (o->kind) {
  case K_FUNC:
    put(IR_FUNCADDR, 0, o->val);
    if (!suppress)
      fn_flags[o->val] |= FN_CALLED;  /* it may be called: link it */
    break;
  case K_VOID:
    error(38 /* "void value used in an expression" */, NULL);
    break;
  case K_CONST:
    put(IR_CONST, o->val, -1);
    o->is_bool = o->val == 0 || o->val == 1;
    break;
  case K_ADDR:
    if (o->global == G_STR)
      put(IR_STRADDR, o->off, -1);
    else
      push_var_addr(o->off, o->global);
    o->is_bool = 0;
    break;
  case K_VAR:
    load_var(o->off, o->global, acc_form(o->type, o->quals));
    o->is_bool = 0;
    break;
  case K_ELEM:
    push_address(o);
    /* fall through */
  case K_DEREF:
    put(IR_FORM(IR_LOAD, acc_form(o->type, o->quals)), 0, -1);
    o->is_bool = 0;
    break;
  case K_STORE:
    finish_store(o, 1);
    break;
  case K_JUMP: {
    /* T: CONST 1 JUMP E F: CONST 0 E: */
    int e = new_label();
    place_true(o);
    put(IR_CONST, 1, -1);
    branch(IR_JUMP, e);
    place_forward(o->val);
    put(IR_CONST, 0, -1);
    place_forward(e);
    o->is_bool = 1;
    break;
  }
  }
  o->kind = K_VALUE;
  if (is_func(o->type))
    o->type = pointer_to(o->type);  /* a function's value: its address */
}

/* Allocate a temporary frame word. Temporaries are freed at the end of
 * each full expression (see end_expr()). */
int new_temp(void)
{
  int off = frame_next;
  if (frame_next + 2 > 255)
    error(30 /* "too many local variables in" */, quote(fn_name[cur_fn]));
  frame_next += 2;
  if (frame_next > frame_max)
    frame_max = frame_next;
  return off;
}

/* Frame offset where the current full expression's temporaries start. */
static int temp_mark;

/* Start a full expression (one not inside another, such as an
 * expression statement or a condition). */
void begin_expr(void)
{
  temp_mark = frame_next;
}

/* End a full expression: its temporaries' frame space is free again. */
void end_expr(void)
{
  frame_next = temp_mark;
}

/* If o is K_DEREF, save its address (on the stack) in a temporary, so
 * that it can be pushed again after a value: o becomes a K_ELEM. */
void spill(Operand *o)
{
  int off;
  if (o->kind != K_DEREF)
    return;
  off = new_temp();
  put(IR_SETLOCAL, off, -1);
  put(IR_DROP, 0, -1);
  make_elem(o, off, 0, 0);
}

/* Where the code of an operand starts, given the IR position and the
 * number of held constants when its parsing began. The first op it put
 * wrote those constants (which belong to the code before it) ahead of
 * its own, so its code starts after them, if it emitted any. */
int code_start(int mark, int h)
{
  return ir_here() > mark ? mark + h : mark;
}

/* Exchange the values left by two adjacent blocks of code: the one from
 * IR position start to mid, and the one from mid to here, with IR_SWAP
 * (approach.md 18.7, A). ccgen moves the code if it can, unless fixed
 * is set (a call must stay where it is); otherwise it exchanges the
 * values when the code runs. A start of -1 means the blocks aren't
 * known (only the values are): then the SWAP's lengths are 0 and -1.
 * Nothing may come between the blocks and the SWAP, not even held
 * constants. */
void swap_blocks(int start, int mid, int fixed)
{
  if (start < 0) {
    put(IR_SWAP, 0, -1);
    return;
  }
  if (nheld != 0 || start > mid || mid > ir_here())
    fatal(175 /* "internal error: bad IR rotation" */, NULL);
  put(IR_SWAP, ir_here() - mid, fixed ? -1 : mid - start);
}

/* Emit the store that the K_STORE operand o waits for: the value's code
 * moves ahead of the address's (swap_blocks()), giving "e addr SW". If
 * keep is set, the value (the stored value: its low byte, for a char)
 * stays on the stack: a DUP ends the value's block, so that the copy
 * goes below the address with it. */
void finish_store(Operand *o, int keep)
{
  if (ir_here() != o->mend || nheld != 0)
    fatal(39 /* "internal error: code after a pending store" */, NULL);
  if (keep)
    put(IR_DUP, 0, -1);
  swap_blocks(o->m0, o->m1, 0);
  put(store_op(o->stype, o->quals), 0, -1);
  if (keep && o->vmask) {
    put(IR_CONST, 0xFF, -1);
    put(IR_AND, 0, -1);
  }
  o->kind = keep ? K_VALUE : K_VOID;
}

/* q (Q_CONST or Q_VOLATILE) if type t has it, or holds an object that
 * does, at any depth (a struct's member, an array's element); else 0.
 * A struct with a const member can't be assigned to as a whole; if any
 * part of an object is volatile, all its words are when it's copied,
 * passed, or initialized a word at a time, as an access too many kept
 * volatile does no harm. */
int holds_qual(int t, int q)
{
  int m, tag;
  if (t & q)
    return q;
  if (is_array(t))
    return holds_qual(type_sub(t), q);
  if (!is_struct(t))
    return 0;
  tag = struct_tag(t);
  for (m = member_next(tag, -1); m >= 0; m = member_next(tag, m))
    if (holds_qual(member_type(m), q))
      return q;
  return 0;
}

/* Report an error unless o is an lvalue that can be assigned to (an
 * array can't be, nor a struct with a const member); what is a message
 * part saying how it was used ("left operand of assignment"). */
void need_lvalue(Operand *o, int what)
{
  if (is_array(o->type) && is_place(o))
    error_in(40 /* "an array can't be the" */, what, NULL);
  if (!is_place(o) || o->assigned || is_func(o->type))
    error_in(41 /* "lvalue required as" */, what, NULL);
  if (o->quals & Q_CONST)
    error_in(42 /* "a 'const' object can't be the" */, what, NULL);
  if (holds_qual(o->type, Q_CONST))  /* (a const o stopped above) */
    error_in(43 /* "a struct with a 'const' member can't be the" */,
             what, NULL);
}

/* Store the value on the stack into lvalue o, which is K_VAR or
 * K_ELEM (spill() first). o becomes the assignment's result: for a
 * variable, its value is kept on the stack (DLW, DAW); for an object
 * reached through an address, it is read back from the object only if
 * needed, unless the object is volatile, whose store must not be
 * followed by a read C didn't ask for: then a copy of the value is kept
 * below the address (for a char, its low byte). */
void assign_to(Operand *o)
{
  if (o->kind == K_VAR) {
    store_var(o);
    o->kind = K_VALUE;
    return;
  }
  if (o->quals & Q_VOLATILE)
    keep_copy(o->type == TY_CHAR);
  push_address(o);
  put(store_op(o->type, o->quals), 0, -1);
  if (o->quals & Q_VOLATILE)
    o->kind = K_VALUE;
  else
    o->assigned = 1;
}

/* The number of words a struct of type t takes on the stack. */
int struct_words(int t)
{
  return (type_size(t) + 1) / 2;
}

/* The words a function of return type t returns: none for void, a
 * struct's words, else 1 (a RETURN's operand, for ccgen). */
int result_words(int t)
{
  return t == TY_VOID ? 0 : is_struct(t) ? struct_words(t) : 1;
}

/* Discard the value of an expression used as a statement. A pending
 * variable or constant ("x;") emits nothing, nor does the result of an
 * assignment through a pointer. A load through a pointer is still
 * done, since it may read I/O (such as the Apple II's $C010). A struct
 * isn't loaded (*p; evaluates p), and one on the stack is dropped. */
void discard(Operand *o)
{
  int k;
  if (is_struct(o->type) && is_place(o)) {
    if (o->kind == K_DEREF)
      put(IR_DROP, 0, -1);  /* its address */
    o->kind = K_VOID;
    return;
  }
  if (is_struct(o->type) && o->kind == K_VALUE) {
    for (k = 0; k < struct_words(o->type); k++)
      put(IR_DROP, 0, -1);  /* a call's result */
    o->kind = K_VOID;
    return;
  }
  if (o->kind == K_STORE)
    finish_store(o, 0);
  if (o->kind == K_JUMP) {
    /* Both outcomes go on to the same place. */
    place_true(o);
    place_forward(o->val);
    o->kind = K_VOID;
    return;
  }
  if (o->kind == K_DEREF || (o->kind == K_ELEM && !o->assigned)
      || (o->kind == K_VAR && (o->quals & Q_VOLATILE)))
    rvalue(o);                  /* a volatile x; still reads x */
  if (o->kind == K_VALUE)
    put(IR_DROP, 0, -1);
}

/* Is o a null pointer constant: an integer constant expression 0? (A
 * const int variable holding 0 isn't one, in C.) */
int is_null(Operand *o)
{
  return o->kind == K_CONST && o->val == 0 && !is_pointer(o->type)
         && !o->nonice;
}

/* The size of what pointer type t points to. Arithmetic on a void *,
 * or on a pointer to an incomplete struct, is an error, since what it
 * points to has no size. */
int elem_size(int t)
{
  int n = type_size(type_sub(t));
  if (n == 0)
    error(is_struct(type_sub(t))
          ? 44 /* "pointer arithmetic on a pointer to an incomplete "
                  "struct" */
          : 45 /* "pointer arithmetic on a 'void *'" */, NULL);
  return n;
}
