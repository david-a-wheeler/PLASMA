/*
 * Stage 1: the parser. Recursive descent for declarations and
 * statements, precedence climbing for expressions (approach.md, issue
 * 5). It emits IR ops as it goes (ir_put) and builds no syntax tree.
 *
 * Language so far (milestones 1 to 7, and enum and typedef):
 *   - types int, char, void, structs, pointers to them (int *, char **,
 *     struct node *, ...), and arrays of any of these but void
 *     (int m[3][4]);
 *   - struct definitions at file or block scope, with members of any
 *     complete type, laid out in order with no padding; a struct's tag
 *     is visible until the end of its block (see parse_struct()). A
 *     member is a place at a constant offset in its struct, so s.m and
 *     p->m use the same pending operands as variables and array
 *     elements. A struct assignment copies its bytes (copy_struct()),
 *     and the result is the object assigned to, not an lvalue
 *     (Operand.assigned). Struct and array initializers are flat lists
 *     over their scalar members and elements (init_flat()). A struct
 *     of up to MAX_BYVAL bytes can be passed and returned by value,
 *     as the words of its bytes (see push_struct_words()); a bigger
 *     one is an error, so pass a pointer instead;
 *   - function definitions and prototypes returning int, char, void or
 *     a pointer, with parameters of any of these but void; an array
 *     parameter (int a[], int m[][4]) is a pointer, as in C;
 *   - local variables, declared anywhere in a block (or in a for
 *     statement's first clause), with optional initializers (an array's
 *     are stores into the frame), and block scope;
 *   - global variables, declared anywhere between functions, with
 *     optional initializers, computed at compile time (see "Global
 *     initializers" below);
 *   - static: a static local is a variable in the data segment, as a
 *     global is (V_DATA), with block scope, and a global's kind of
 *     initializer; a static global or function is local to the file;
 *   - extern, at file scope: a global declared without storage, which
 *     another file (or a later definition) provides; see var_link[];
 *   - enum constants, which are int constants, and typedef names, both
 *     with block scope (see find_id()); an enum type is int;
 *   - const and volatile, as part of the type (bits in the type handle;
 *     see type.c). Operands carry unqualified types, and a place keeps
 *     its object's qualifiers in Operand.quals. A const object can't be
 *     assigned to; dropping a qualifier from a pointer's target is a
 *     warning. Reading a const scalar with a constant initializer gives
 *     its value at compile time (see parse_primary()), but that value
 *     isn't a C constant expression (Operand.nonice);
 *   - casts;
 *   - statements: return, if/else, while, do, for, switch (with case
 *     and default labels anywhere in its body), break, continue,
 *     expression statements, empty statements and nested blocks;
 *   - expressions: integer and character constants, string literals,
 *     variables, function calls, parentheses, the pstr() builtin,
 *     unary - + ~ ! * & ++ -- and sizeof, postfix ++ -- [] . ->, binary
 *     * / % + - << >> & ^ |, comparisons, && || ?:, and
 *     = += -= *= /= %= &= |= ^= <<= >>=. Pointer arithmetic scales by
 *     the element size, pointers compare unsigned, pointer types are
 *     checked as strictly as C says, and an array used as a value is a
 *     pointer to its first element.
 * A function must be declared before it is called (no implicit
 * declarations), except the CMDSYS functions in cmdsys_sigs[] below,
 * which their first call declares. A function called but not defined
 * in the file must be one of those; it is imported from CMDSYS.
 *
 * Like PLASMA, a function returns exactly as many values as it is
 * declared to: one for int, none for void. So a void call used as a
 * statement needs no DROP, and a void value can't be used.
 *
 * Streaming and assignment: a variable's load is not emitted as soon as
 * the variable is parsed, because it may turn out to be the target of
 * an assignment (x = ..., x += ..., x++). Instead the parser returns a
 * *pending* operand, and emits the load only once it knows the value is
 * needed (approach.md 4(a), option A). An object reached through a
 * pointer, and an array element, are pending the same way, so that
 * a[i] = x needs no temporary; see the Operand kinds below.
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
#include "ptop.h"
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
 * no cast. Their names and meanings are PLASMA's, not C's: puts()
 * takes a PLASMA (length-prefixed) string and adds no newline, and
 * strcpy() copies PLASMA strings. divmod() returns two values and is
 * left out.
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

/* Is variable v in the data segment (a global or a static local)? */
static int var_data(int v)
{
  return (var_flags[v] & V_DATA) != 0;
}

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
static int push_var(int name, int type, int off, int flags)
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
  if (nvars != nglobals)
    fatal(29 /* "internal error: a global declared among locals" */, NULL);
  nglobals++;
  return push_var(name, type, data_offset(), V_DATA);
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

/* Declare a static local of the given type at the current depth. It has
 * block scope, like any local, but lives in the data segment like a
 * global (V_DATA, at the next data offset), so it keeps its value
 * between calls and takes no frame space. The caller then writes its
 * bytes to the data stream: its initial value, or zeros. */
int add_static_local(int name, int type)
{
  return push_var(name, type, data_offset(), V_DATA);
}

/* Parameter types of declared functions, n at a time; fn_ptypes[] says
 * where a function's start. */
char param_types[MAX_PARAM_TYPES];  /* unqualified types */
static int nparam_types;

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

/* Describe tag for a message: 'struct name', or an unnamed struct. The
 * result may be in a static buffer. */
const char *tag_desc(int tag)
{
  static char buf[MAX_NAME + 10];
  if (tag_name(tag) < 0)
    return "an unnamed struct";
  strcpy(buf, "'struct ");
  strcat(buf, nam_name(tag_name(tag)));
  strcat(buf, "'");
  return buf;
}

/* Report error msg (such as "a variable can't have the incomplete
 * type") if t is an incomplete struct type, which has no size yet. */
void need_complete(int t, int msg)
{
  if (is_struct(t) && type_size(t) == 0)
    error(msg, tag_desc(struct_tag(t)));
}

/* Parse any qualifiers (const, volatile), and return them as Q_ bits. */
static int parse_quals(void)
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
  if (tok == T_STRUCT) {
    t = parse_struct();
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

/* Parse any '*'s after a base type t: each makes a pointer to the type
 * before it, so "char **" is a pointer to a pointer to char. Qualifiers
 * after a '*' qualify that pointer: char *const p is a const pointer to
 * char. */
int parse_pointers(int t)
{
  while (tok == '*') {
    next();
    t = pointer_to(t);
    t |= parse_quals();
  }
  return t;
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
#define MAX_HELD 8
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
static int in_sizeof;

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
static int hold(int op, int a, int f)
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
static int var_where(int v)
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
static void check_right_const(int op, int v)
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
static int fold(int op, int a, int b, int uns)
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
static int swapped_op(int op)
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
static int is_compare(int op)
{
  return IR_IS_COMPARE(op);
}

/* ---------- Labels ---------- */

/* Labels are numbered per function. Stage 4 resolves them. */
int nlabels;
char label_used[MAX_LABELS];  /* some branch to it so far */

/* Allocate a new label in the current function. */
int new_label(void)
{
  if (nlabels >= MAX_LABELS)
    error(36 /* "too many branches in" */, quote(fn_name[cur_fn]));
  label_used[nlabels] = 0;
  return nlabels++;
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

/* Defined in the Expressions section below, but used by cond_branch. */
void place_true(Operand *o);

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
  branch(on_true ? IR_BRTRUE : IR_BRFALSE, n);
}

/* ---------- Expressions ---------- */

static void parse_unary(Operand *o);
static void parse_postfix(Operand *o);
static void parse_sizeof(Operand *o);
static void binary(Operand *o, Operand *r, int op, int lstart, int mid);
static void finish_store(Operand *o, int keep);
static void offset_place(Operand *o, int off);

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
static int movable(int global)
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
static void push_var_addr(int off, int global)
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

/* Push the variable at off, where global says (var_where()): a word,
 * or a byte if is_char; through its address, for a global that may
 * move. */
static void load_var(int off, int global, int is_char)
{
  if (movable(global)) {
    push_var_addr(off, global);
    put(is_char ? IR_LOADB : IR_LOAD, 0, -1);
  } else if (global) {
    put(is_char ? IR_GLOBALB : IR_GLOBAL, off, data_sym_of(global));
  } else {
    put(is_char ? IR_LOCALB : IR_LOCAL, off, -1);
  }
}

/* Store the top value into the variable o (a K_VAR), keeping the stored
 * value (for a char, its low byte) on the stack: through its address,
 * after a copy of the value, for a global that may move. */
void store_var(Operand *o)
{
  int c = o->type == TY_CHAR;
  if (movable(o->global)) {
    if (c) {
      put(IR_CONST, 0xFF, -1);
      put(IR_AND, 0, -1);       /* the byte a char store keeps */
    }
    put(IR_DUP, 0, -1);
    push_var_addr(o->off, o->global);
    put(c ? IR_STOREB : IR_STORE, 0, -1);
  } else if (o->global) {
    put(c ? IR_SETGLOBALB : IR_SETGLOBAL, o->off, data_sym_of(o->global));
  } else {
    put(c ? IR_SETLOCALB : IR_SETLOCAL, o->off, -1);
  }
}

/* The highest offset a variable can have: 255 in the frame, and 32767
 * in the data segment. */
static int max_offset(int global)
{
  return global ? 0x7FFF : 255;
}

/* The IR op that loads a value of type t from an address (a char is a
 * byte). */
static int load_op(int t)
{
  return t == TY_CHAR ? IR_LOADB : IR_LOAD;
}

/* The IR op that stores a value of type t at an address. */
static int store_op(int t)
{
  return t == TY_CHAR ? IR_STOREB : IR_STORE;
}

/* Is o a place (a variable, or an object reached through an address)
 * whose value hasn't been loaded? */
static int is_place(Operand *o)
{
  return o->kind >= K_VAR && o->kind <= K_ELEM;
}

/* The type of a value of type t: an array's value is a pointer to its
 * first element. */
int value_type(int t)
{
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
static int addr_op(Operand *o, int *a, int *f)
{
  *a = o->off;
  *f = o->global == G_STR ? -1 : data_sym_of(o->global);
  if (o->global != G_STR && movable(o->global))
    return IR_HEAPADDR;
  return o->global == G_STR ? IR_STRADDR : IR_DATAADDR;
}

/* Is type t (or a qualified one: const unsigned) unsigned int? */
static int is_uns(int t)
{
  return unqual(t) == TY_UINT;
}

/* The type of an arithmetic op's result on integer types a and b, by
 * C's usual arithmetic conversions: unsigned int if either is, else
 * int (char promotes to int). */
static int arith_type(int a, int b)
{
  return is_uns(a) || is_uns(b) ? TY_UINT : TY_INT;
}

/* Integer type t promoted, as for a unary operator or a shift's left
 * operand: unsigned int stays so, the others become int. */
static int promoted(int t)
{
  return is_uns(t) ? TY_UINT : TY_INT;
}

/* Make o the K_ELEM at the address in the variable at off (base_arr 0),
 * or at the array at off (base_arr 1), with no index yet. The variable
 * or array is a global if global is set. */
static void make_elem(Operand *o, int off, int global, int base_arr)
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
static void push_address(Operand *o)
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
    load_var(o->idx, o->idx_global, o->idx_char);
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
 * the address of its first element (for K_DEREF, already there). */
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
    load_var(o->off, o->global, o->type == TY_CHAR);
    o->is_bool = 0;
    break;
  case K_ELEM:
    push_address(o);
    /* fall through */
  case K_DEREF:
    put(load_op(o->type), 0, -1);
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
}

/* Allocate a temporary frame word. Temporaries are freed at the end of
 * each full expression (see end_expr()). */
static int new_temp(void)
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
static void spill(Operand *o)
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
static void swap_blocks(int start, int mid, int fixed)
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
static void finish_store(Operand *o, int keep)
{
  if (ir_here() != o->mend || nheld != 0)
    fatal(39 /* "internal error: code after a pending store" */, NULL);
  if (keep)
    put(IR_DUP, 0, -1);
  swap_blocks(o->m0, o->m1, 0);
  put(store_op(o->stype), 0, -1);
  if (keep && o->vmask) {
    put(IR_CONST, 0xFF, -1);
    put(IR_AND, 0, -1);
  }
  o->kind = keep ? K_VALUE : K_VOID;
}

/* Does type t hold a const object: is it a struct with a const member,
 * at any depth (in a member struct, or as an array's elements)? Such a
 * struct can't be assigned to as a whole. */
static int holds_const(int t)
{
  int m, tag;
  if (is_array(t))
    return is_const(type_sub(t)) || holds_const(type_sub(t));
  if (!is_struct(t))
    return 0;
  tag = struct_tag(t);
  for (m = member_next(tag, -1); m >= 0; m = member_next(tag, m))
    if (is_const(member_type(m)) || holds_const(member_type(m)))
      return 1;
  return 0;
}

/* Report an error unless o is an lvalue that can be assigned to (an
 * array can't be, nor a struct with a const member); what is a message
 * part saying how it was used ("left operand of assignment"). */
static void need_lvalue(Operand *o, int what)
{
  if (is_array(o->type) && is_place(o))
    error_in(40 /* "an array can't be the" */, what, NULL);
  if (!is_place(o) || o->assigned)
    error_in(41 /* "lvalue required as" */, what, NULL);
  if (o->quals & Q_CONST)
    error_in(42 /* "a 'const' object can't be the" */, what, NULL);
  if (holds_const(o->type))
    error_in(43 /* "a struct with a 'const' member can't be the" */,
             what, NULL);
}

/* Store the value on the stack into lvalue o, which is K_VAR or
 * K_ELEM (spill() first). o becomes the assignment's result: for a
 * variable, its value is kept on the stack (DLW, DAW); for an object
 * reached through an address, it is read back from the object only if
 * needed. */
static void assign_to(Operand *o)
{
  if (o->kind == K_VAR) {
    store_var(o);
    o->kind = K_VALUE;
    return;
  }
  push_address(o);
  put(store_op(o->type), 0, -1);
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
  if (o->kind == K_DEREF || (o->kind == K_ELEM && !o->assigned))
    rvalue(o);
  if (o->kind == K_VALUE)
    put(IR_DROP, 0, -1);
}

/* Is o a null pointer constant: an integer constant expression 0? (A
 * const int variable holding 0 isn't one, in C.) */
static int is_null(Operand *o)
{
  return o->kind == K_CONST && o->val == 0 && !is_pointer(o->type)
         && !o->nonice;
}

/* The size of what pointer type t points to. Arithmetic on a void *,
 * or on a pointer to an incomplete struct, is an error, since what it
 * points to has no size. */
static int elem_size(int t)
{
  int n = type_size(type_sub(t));
  if (n == 0)
    error(is_struct(type_sub(t))
          ? 44 /* "pointer arithmetic on a pointer to an incomplete "
                  "struct" */
          : 45 /* "pointer arithmetic on a 'void *'" */, NULL);
  return n;
}

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
  int from = value_type(o->type), lost;
  if (o->kind == K_VOID)
    error(38 /* "void value used in an expression" */, NULL);
  if (is_struct(to) || is_struct(from)) {
    if (unqual(to) != unqual(from))
      error_in(46 /* "incompatible types in" */, where, detail);
    return;
  }
  if (is_pointer(to)) {
    if (!is_pointer(from)) {
      if (!is_null(o))
        error_in(47 /* "pointer from integer without a cast in" */,
                 where, detail);
    } else if (!compatible(from, to)) {
      error_in(48 /* "incompatible pointer types in" */, where, detail);
    } else {
      lost = type_sub(from) & ~type_sub(to) & Q_ALL;
      if (lost & Q_CONST)
        warning_in(49 /* "'const' qualifier discarded from pointer "
                      "target type in" */, where, detail);
      if (lost & Q_VOLATILE)
        warning_in(50 /* "'volatile' qualifier discarded from pointer "
                      "target type in" */, where, detail);
    }
  } else if (is_pointer(from)) {
    error_in(51 /* "integer from pointer without a cast in" */,
             where, detail);
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
  } else if (is_pointer(type) && addr_const(o)) {
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

/* Parse a call to fn, starting at '(': emit the arguments, check their
 * count (and their types, if fn's parameter types are known), then
 * emit the call. */
static void parse_call(int f, Operand *o)
{
  char where[MAX_NAME + 16];
  Operand a;
  int nargs = 0;
  next();                         /* '(' */
  if (tok != ')') {
    for (;;) {
      parse_expr(PREC_ASSIGN, &a);
      if (fn_ptypes[f] >= 0 && nargs < nparams_of(f)) {
        strcpy(where, num_text(nargs + 1));
        strcat(where, " of ");
        strcat(where, quote(fn_name[f]));
        check_assign(param_types[fn_ptypes[f] + nargs], &a,
                     200 /* "argument" */, where);
      }
      if (is_struct(a.type)) {
        if (fn_ptypes[f] < 0)
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
  if (nparams_of(f) < 0)
    set_nparams(f, nargs);      /* first use fixes the count */
  else if (nparams_of(f) != nargs)
    error(55 /* "wrong number of arguments in call to" */,
          quote(fn_name[f]));
  fn_flags[f] |= FN_CALLED;
  if (!suppress)
    ncalls++;
  put(IR_CALL, nargs, f);
  o->kind = fn_type[f] != TY_VOID ? K_VALUE : K_VOID;
  o->type = fn_type[f];       /* a struct: its words are on the stack */
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
      c = parse_pointers(parse_base_type());
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
    if (tok == '(') {
      if (name == ID_PSTR) {
        parse_pstr();
        o->type = pointer_to(TY_CHAR);
        return;
      }
      if (find_var(name) >= 0)
        error(62 /* "called object is not a function:" */,
              quote(name));
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
    v = find_var(name);
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
    if (find_func(name) >= 0)
      error(63 /* "function pointers are not supported yet:" */,
            quote(name));
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
  char msg[2 * MAX_NAME + 16];
  int t = arrow ? value_type(o->type) : o->type, tag, m, off;

  next();                         /* '.' or '->' */
  if (arrow && !(is_pointer(t) && is_struct(type_sub(t))))
    error(72 /* "'->' needs a pointer to a struct" */, NULL);
  if (!arrow && !is_struct(t))
    error(73 /* "'.' needs a struct" */, NULL);
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
    strcpy(msg, quote(tok_val));
    strcat(msg, " in ");
    strcat(msg, tag_desc(tag));
    error(214 /* "no member named" */, msg);
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

/* Parse any postfix operators after the operand o: ++, --, [], . and
 * ->. (A call's "(" is handled with the function name.) */
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
      not_yet();
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
      /* A type name: a base type, '*'s and array sizes. */
      t = parse_dims(parse_pointers(parse_base_type()), 0);
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
    *o = take_b ? b : c;
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
    if (op >= IR_ULT && op <= IR_UGE) {     /* already put */
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

/* The function a struct copy calls: memcpy(dst, src, n), from CMDSYS
 * unless the program declares its own, which must take C's arguments
 * in C's order. A value it returns is dropped. */
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

