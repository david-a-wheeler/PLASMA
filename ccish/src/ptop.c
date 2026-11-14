// Stage 1: the parser (approach.md, issue 5) - this file its top
// level and initializers (approach.md, "parse.c split design").
// Recursive descent for declarations and statements, precedence
// climbing for expressions in pexpr.c; the statements and
// declarations are in pstmt.c, and the machinery they drive (the
// output and folding, the labels) is in parse.c. The list below
// names functions across the parser's four files.
//
// Language so far (milestones 1 to 7, and enum and typedef):
//   - types int, char, void, structs, pointers to them (int *, char **,
//     struct node *, ...), and arrays of any of these but void
//     (int m[3][4]);
//   - struct definitions at file or block scope, with members of any
//     complete type, laid out in order with no padding; a struct's tag
//     is visible until the end of its block (see parse_struct()). A
//     member is a place at a constant offset in its struct, so s.m and
//     p->m use the same pending operands as variables and array
//     elements. A struct assignment copies its bytes (copy_struct()),
//     and the result is the object assigned to, not an lvalue
//     (Operand.assigned). Struct and array initializers are flat lists
//     over their scalar members and elements (init_flat()). A struct
//     of up to MAX_BYVAL bytes can be passed and returned by value,
//     as the words of its bytes (see push_struct_words()); a bigger
//     one is an error, so pass a pointer instead;
//   - function definitions and prototypes returning int, char, void or
//     a pointer, with parameters of any of these but void; an array
//     parameter (int a[], int m[][4]) is a pointer, as in C;
//   - local variables, declared anywhere in a block (or in a for
//     statement's first clause), with optional initializers (an array's
//     are stores into the frame), and block scope;
//   - global variables, declared anywhere between functions, with
//     optional initializers, computed at compile time (see "Global
//     initializers" below);
//   - static: a static local is a variable in the data segment, as a
//     global is (V_DATA), with block scope, and a global's kind of
//     initializer; a static global or function is local to the file;
//   - extern, at file scope: a global declared without storage, which
//     another file (or a later definition) provides; see var_link[];
//   - enum constants, which are int constants, and typedef names, both
//     with block scope (see find_id()); an enum type is int;
//   - const and volatile, as part of the type (bits in the type handle;
//     see type.c). Operands carry unqualified types, and a place keeps
//     its object's qualifiers in Operand.quals. A const object can't be
//     assigned to; dropping a qualifier from a pointer's target is a
//     warning. Reading a const scalar with a constant initializer gives
//     its value at compile time (see parse_primary()), but that value
//     isn't a C constant expression (Operand.nonice);
//   - casts;
//   - _Static_assert, at file scope or as a statement in a block;
//   - statements: return, if/else, while, do, for, switch (with case
//     and default labels anywhere in its body), break, continue,
//     expression statements, empty statements and nested blocks;
//   - expressions: integer and character constants, string literals,
//     variables, function calls, parentheses, the pstr() builtin,
//     unary - + ~ ! * & ++ -- and sizeof, postfix ++ -- [] . ->, binary
//     * / % + - << >> & ^ |, comparisons, && || ?:, and
//     = += -= *= /= %= &= |= ^= <<= >>=. Pointer arithmetic scales by
//     the element size, pointers compare unsigned, pointer types are
//     checked as strictly as C says, and an array used as a value is a
//     pointer to its first element.
//
// Like PLASMA, a function returns exactly as many values as it is
// declared to: one for int, none for void. So a void call used as a
// statement needs no DROP, and a void value can't be used.

#include <assert.h>
#ifdef CCISH_STATS
#include <stdio.h>
#endif
#include <string.h>
#include "build.h"
#include "cclimits.h"
#include "diag.h"
#include "diag.src.h"
#include "fixed.h"
#include "token.h"
#include "tokin.h"
#include "obj.h"
#include "obj.sym.h"
#include "ir.h"
#include "name.h"
#include "type.h"
#include "type.tag.h"
#include "parse.h"
#include "irout.h"
#include "ptop.h"
#include "pstmt.h"
#include "pexpr.h"

int zero_next[2];                   // (ptop.h)

// Set by parse_scalar_init(): whether the initializer it parsed was an
// integer constant (not an address), and its value. A const variable
// initialized so has a known value (set_known()).
int init_known, init_val;

// ---------- Global initializers ----------
//
// A global's initial value is computed at compile time and written to
// the data stream as the global's bytes (approach.md, "M5 design"). It
// goes through the expression parser with code output off, and must
// fold to a constant, or, for a pointer, to an address constant
// (K_ADDR): a string literal or a global's address, with any constant
// indexes, members, casts to pointers, and + or - a constant (C6), or
// a function's address (K_FUNC: f, &f, a cast of either to a pointer).
// Anything else (&*p, a pointer variable's value) is an error.

// Append a word, low byte first, to the data (irout.c's data_byte()); v
// is evaluated twice. A macro, not a function: this is its one user.
#define data_word(v) do { data_byte(v); data_byte((v) >> 8); } while (0)

// Is n a parameter count main may have: 0, or 2 (argc and argv)?
#define MAIN_NPARAMS_OK(n) (((n) & ~2) == 0)
_Static_assert(MAIN_NPARAMS_OK(0) && MAIN_NPARAMS_OK(2),
               "MAIN_NPARAMS_OK misses a member");
_Static_assert(!MAIN_NPARAMS_OK(1) && !MAIN_NPARAMS_OK(3)
               && !MAIN_NPARAMS_OK(4) && !MAIN_NPARAMS_OK(6)
               && !MAIN_NPARAMS_OK(-1) && !MAIN_NPARAMS_OK(255),
               "MAIN_NPARAMS_OK holds a non-member");

// Is an operand of kind k a constant a static initializer may hold: a
// number, an address constant or a function's address?
#define K_IS_INIT_CONST(k) K_IN(k, 1 << K_CONST | 1 << K_ADDR | 1 << K_FUNC)
_Static_assert(K_IS_INIT_CONST(K_CONST) && K_IS_INIT_CONST(K_ADDR)
               && K_IS_INIT_CONST(K_FUNC), "K_IS_INIT_CONST misses a member");
_Static_assert(!K_IS_INIT_CONST(K_VALUE) && !K_IS_INIT_CONST(K_VOID)
               && !K_IS_INIT_CONST(K_VAR) && !K_IS_INIT_CONST(K_DEREF)
               && !K_IS_INIT_CONST(K_ELEM) && !K_IS_INIT_CONST(K_STORE)
               && !K_IS_INIT_CONST(K_JUMP),
               "K_IS_INIT_CONST holds a non-member");

// Parse the initializer of one scalar of type type (an int, char or
// pointer), check it as an assignment, and write it to the data stream.
// It is an ordinary expression, parsed with code off (and its string
// literals stored, want_addr), which must be a constant or, for a
// pointer, an address constant (approach.md, "C6 design"). Sets
// init_known and init_val.
static void parse_scalar_init(int type)
{
  Operand o;
  want_addr++;
  parse_skipped(PREC_ASSIGN, &o);
  want_addr--;
  if (is_pointer(type))
    addr_const(&o);
  if (!K_IS_INIT_CONST(o.kind))
    error0(206 /* "initializer is not a constant ccish supports" */);
#ifdef CCISH_LINT
  if (o.nonice && o.kind == K_CONST)
    error_in(201 /* "a const variable's value isn't a constant"
                  " expression in C" */,
             205 /* "(an initializer here must be one)" */, NULL);
#endif
  check_assign(type, &o, 198 /* "initialization" */, NULL);
  init_known = o.kind == K_CONST;
  init_val = o.val;
  if (o.kind == K_FUNC) {
    // A function's address: a relocation to its symbol, which links
    // it, as a call does (approach.md, "Function pointer design").
    fn_flags[o.val] |= FN_CALLED;
    data_addr(REL_FUNC, o.val, 0);
    return;
  }
  if (o.kind == K_CONST) {
    if (type_size(type) == 1)
      data_byte(o.val);
    else
      data_word(o.val);
    return;
  }
  // An address must stay within its string, within its zero-filled
  // object (C11 6.5.6p8), or within the data or zero area written so
  // far (one past the end is allowed, as in C).
  if (o.global == G_STR) {
    if (o.off < o.disp || o.off > o.val)
      error0(155 /* "initializer address is out of range" */);
    data_addr(REL_STR, o.off, 0);
  } else if (o.global >= 2) {
    // An extern global's or a zero-filled one's: through its symbol,
    // and now it can't move to the heap.
    if ((var_link[o.global - 2] & VL_BSS)
        && (o.off < 0 || o.off > type_size(var_type[o.global - 2])))
      error0(155 /* "initializer address is out of range" */);
    var_link[o.global - 2] |= VL_USED | VL_INDATA;
    data_addr(REL_DSYM, o.global - 2, o.off);
  } else {
    if (o.off < 0
        || o.off > (o.global == 1 ? data_offset() : zero_next[ZERO_AREA]))
      error0(155 /* "initializer address is out of range" */);
    data_addr(o.global == 1 ? REL_DATA : REL_ZERO, o.off, 0);
  }
}

// An array's or struct's initializer is parsed the same way for a
// global and a local; only where its elements go differs. A global's
// are written to the data stream, in order. A local's are stores into
// the frame (approach.md, "M5 design"), at frame offset init_base plus
// each element's offset: CN 1 SLW t. Two neighboring char constants are
// stored as one word (CW SLW, 5 bytes, not 8), and the zeros after the
// last initializer are stored with a chain of DLWs, or by memset when
// that is shorter. If the local holds a volatile, its stores are all
// volatile, so none is removed; pairing and memset stay, as a frame
// can't be a device whose access width matters (README).
static int init_local;          // initializing a local array
static int init_vol;            // and it holds a volatile: Q_VOLATILE
static int init_set;            // then IR_SETLOCAL's form to use
static int init_base;           // its frame offset
static int init_name;           // its name (an id), for messages
static int held_char = -1;      // a char constant to be stored with the
                                // next one, at frame offset held_at
static int held_at;

// Make sure the frame reaches offset end, for a local array whose size
// comes from its initializer, so that it grows as elements are added
// (and temporaries come after it).
static void reserve(int end)
{
  if (end <= frame_next)
    return;
  if (end > 255)
    error(115 /* "local array doesn't fit in the 255-byte frame (make "
                 "it static or global):" */, quote(init_name));
  frame_next = end;
  if (frame_next > frame_max)
    frame_max = frame_next;
}

// Store the held char constant, if any, by itself.
static void flush_char(void)
{
  if (held_char < 0)
    return;
  put(IR_CONST, held_char, -1);
  put(IR_CHAR_FORM(init_set, 1), held_at, -1);
  put_op(IR_DROP);
  held_char = -1;
}

// Initialize the byte at offset off of the array to v: write it to the
// data stream, or, for a local, store it (with the byte before it, if
// that is a held constant).
static void init_byte(int off, int v)
{
  int at = init_base + off;
  if (!init_local) {
    data_byte(v);
    return;
  }
  reserve(at + 1);
  v &= 0xFF;
  if (held_char >= 0 && held_at == at - 1) {
    put(IR_CONST, held_char | (v << 8), -1);
    put(init_set, held_at, -1);
    put_op(IR_DROP);
    held_char = -1;
    return;
  }
  flush_char();
  held_char = v;
  held_at = at;
}

// The CMDSYS function __memset, declared from the CMDSYS table if the
// program hasn't used it yet; or -1 if the program has its own.
static int cmdsys_memset(void)
{
  int f = find_func(ID_MEMSET), c = find_cmdsys(ID_MEMSET);
  if (f < 0) {
    f = add_func(ID_MEMSET);
    declare_cmdsys(f, c);
  } else if ((fn_flags[f] & FN_DEFINED) || !matches_cmdsys(f, c)) {
    return -1;
  }
  return f;
}

// Initialize n bytes at offset off of the array to 0: write them to the
// data stream, or, for a local, store them. Up to 3 words take
// CN 0 DLW ... SLW (1 + 2 bytes per word); more take
// LLA CN 0 CB n CALL memset (7 or 8 bytes).
static void init_zero(int n, int off)
{
  int at = init_base + off, words, i, f;
  if (!init_local) {
    data_zero(n);
    return;
  }
  if (n <= 0)
    return;
  words = n >> 1;               // n / 2: a shift is safe, since n > 0
  reserve(at + n);
  if (held_char == 0 && held_at == at - 1) {
    // A held 0 just before (a string's NUL) joins the zeros.
    held_char = -1;
    at--;
    n++;
    words = n >> 1;
  }
  if (words + (n & 1) > 3 && (f = cmdsys_memset()) >= 0) {
    put(IR_LOCALADDR, at, -1);
    put_op(IR_CONST);
    put(IR_CONST, n, -1);
    fn_flags[f] |= FN_CALLED;
    put(IR_CALL, 3, f);
    return;
  }
  put_op(IR_CONST);
  for (i = 0; i < words; i++)
    put(init_set, at + 2 * i, -1);
  if (n & 1)
    put(IR_CHAR_FORM(init_set, 1), at + n - 1, -1);
  put_op(IR_DROP);
}

// Parse the initializer of one scalar element of type type, at offset
// off of the array. A global's must be constant (parse_scalar_init); a
// local's may be any expression, as C99 allows.
static void init_elem(int type, int off)
{
  Operand e, v;
  int at = init_base + off;
  if (!init_local) {
    parse_scalar_init(type);
    return;
  }
  reserve(at + type_size(type));
  begin_expr();
  parse_expr(PREC_ASSIGN, &e);
  check_assign(type, &e, 198 /* "initialization" */, NULL);
  if (type == TY_CHAR && e.kind == K_CONST) {
    init_byte(off, e.val);
  } else {
    rvalue(&e);
    v.kind = K_VAR;
    v.type = type;
    v.quals = init_vol;         // a volatile's store is volatile
    v.off = at;
    v.global = 0;
    store_var(&v);
    put_op(IR_DROP);
  }
  end_expr();
}

// Initialize the array, from offset off, with the string literal (the
// current token): a char array with room for max chars, or for any
// number if max is 0. Its NUL is included if there is room, as in C.
// Returns the number of bytes initialized.
static int init_string(int max, int off)
{
  int i, n = tok_len;
  if (max && n > max)
    error(156 /* "initializer string is too long for" */,
          quote(init_name));
  for (i = 0; i < n; i++)
    init_byte(off + i, tok_text[i]);
  if (!max || n < max) {
    init_byte(off + n, 0);
    n++;
  }
  next();
  return n;
}

// Set by init_flat() when the initializer list's '}' is reached.
static int init_done;
// The end of what has been initialized so far: the offset after the
// last scalar (or string). With no padding, the scalars of an
// aggregate are contiguous, so everything before it is initialized.
static int init_end;

// After one scalar (or string) of a brace list: skip a ',', and note
// whether the list ends here.
static void after_leaf(void)
{
  if (tok == ',')
    next();
  else if (tok != '}')
    error(157 /* "expected '}' or ',' in an initializer, not" */,
          tok_desc());
  init_done = tok == '}';
}

static void init_flat(int t, int off);

// Initialize the subobject of type t at offset off, an element or a
// member, as one item of the enclosing brace list (C11 6.7.9p20): from
// its own brace list if one starts here, else from the enclosing
// list's next items (brace elision). What its own list leaves out is
// zero, written now, so that everything before init_end stays
// initialized and a global's data stays in order.
static void init_sub(int t, int off)
{
  int end = off + type_size(t);
  if (tok != '{') {
    init_flat(t, off);
    return;
  }
  next();
  init_done = tok == '}';       // {}: all zero
  init_flat(t, off);
  if (!init_done)
    error(161 /* "too many initializers for" */, quote(init_name));
  next();                       // '}'
  init_zero(end - init_end, init_end);
  init_end = end;
  init_done = 0;
  after_leaf();
}

// Initialize the object of type t at offset off (of the variable being
// initialized) from a brace list, as far as the list goes. Each element
// or member may have its own brace list (init_sub()), or the scalars
// may be flat, as C allows: the scalars of arrays and structs, in
// order, at any depth (struct pt ps[2] = {1, 2, 3, 4};). A char array
// may be given a string literal instead ({"abc", 5}). An array of
// unknown size (count 0, only at the top) takes as many elements as the
// list gives.
static void init_flat(int t, int off)
{
  int n, i, m, tag;
  if (is_array(t) && UNQUAL(type_sub(t)) == TY_CHAR && tok == TC_STR) {
    n = type_count(t);
    i = init_string(n, off);
    init_zero(n - i, off + i);
    init_end = off + (n ? n : i);
    after_leaf();
  } else if (is_array(t)) {
    n = type_count(t);
    for (i = 0; (n == 0 || i < n) && !init_done; i++)
      init_sub(type_sub(t), off + i * type_size(type_sub(t)));
  } else if (is_struct(t)) {
    tag = STRUCT_TAG(t);
    for (m = member_next(tag, -1); m >= 0 && !init_done;
         m = member_next(tag, m)) {
      init_sub(mb_type[m], off + mb_off[m]);
      if (tg_union[tag])
        break;   // as in C, a union is initialized by its first
                 // member only; a further initializer hits the
                 // "too many" error below
    }
  } else {
    init_elem(UNQUAL(t), off);
    init_end = off + type_size(t);
    after_leaf();
  }
}

// Parse the initializer of an array or struct of type type, named name,
// for a global or a local (init_local): a brace list (init_flat()), or,
// for a char array, a string literal. Returns the type, with an array
// size that was left out filled in from the initializer. Whatever the
// list doesn't reach is zero.
static int parse_aggr_init(int type, int name)
{
  int esize;

  init_name = name;
  init_end = 0;
  if (is_array(type) && UNQUAL(type_sub(type)) == TY_CHAR
      && tok == TC_STR) {
    init_end = init_string(type_count(type), 0);
  } else {
    expect('{', is_array(type)
                ? 159 /* "expected '{' to start an array initializer, "
                         "not" */
                : 160 /* "expected '{' to start a struct initializer, "
                         "not" */);
    init_done = tok == '}';
    init_flat(type, 0);
    if (!init_done)
      error(161 /* "too many initializers for" */, quote(name));
    next();                     // '}'
  }
  if (is_array(type) && type_count(type) == 0) {
    if (init_end == 0)
      error(162 /* "an array of unknown size needs initializers:" */,
            quote(name));
    esize = type_size(type_sub(type));
    type = array_of(type_sub(type), (init_end + esize - 1) / esize);
  }
  init_zero(type_size(type) - init_end, init_end);    // the rest
  return type;
}

// Parse the initializer of a global of type type, named name, and
// write its bytes to the data stream. Returns the type, with an array
// size that was left out filled in.
int parse_global_init(int type, int name)
{
  if (is_array(type) || is_struct(type)) {
    init_local = 0;
    init_base = 0;
    return parse_aggr_init(type, name);
  }
  if (tok == '{') {                   // int x = {5}; is valid C
    next();
    parse_scalar_init(type);
    if (tok == ',')
      next();
    expect('}', 117 /* "expected '}' after the initializer, not" */);
  } else {
    parse_scalar_init(type);
  }
  return type;
}

// Parse the brace initializer of a local array or struct of type type,
// named name, at frame offset base, as stores into the frame: volatile
// ones if it holds a volatile (holds_qual()). Returns the type, with an
// array size that was left out filled in.
int parse_local_init(int type, int name, int base)
{
  init_local = 1;
  init_vol = holds_qual(type, Q_VOLATILE);
  init_set = IR_FORM(IR_SETLOCAL, acc_form(TY_INT, init_vol));
  init_base = base;
  held_char = -1;
  type = parse_aggr_init(type, name);
  flush_char();
  init_local = 0;
  reserve(base + type_size(type));
  return type;
}

#ifdef CCISH_LINT
// Can a global of type a be declared again with type b? They must be
// the same, except that one array may leave its size out.
static int same_global_type(int a, int b)
{
  if (a == b)
    return 1;
  return is_array(a) && is_array(b) && type_sub(a) == type_sub(b)
         && (type_count(a) == 0 || type_count(b) == 0);
}
#endif

// Declare a global named name of type type, with storage class storage
// (TC_STATIC, TC_EXTERN or 0), and return it. An extern one has no
// storage here (VL_EXTERN); otherwise it is at the next data offset, and
// the caller writes its bytes. A global may be declared again if all but
// one of its declarations are extern, as in C; the last gives it its
// storage. Linkage follows C11 6.2.2: extern after static keeps it
// static, and static after a declaration that isn't is an error.
static int declare_global(int name, int type, int storage)
{
  int s = find_var(name);
  if (s < 0) {
    s = add_global(name, type);
    if (storage == TC_EXTERN) {
      var_off[s] = 0;
      var_link[s] = VL_EXTERN;
    } else if (storage == TC_STATIC) {
      var_link[s] = VL_STATIC;
    }
    return s;
  }
  // Not a validity check only: two with storage (int x; int x;, valid
  // C, tentative definitions) are something ccish can't compile.
  if (storage != TC_EXTERN && !(var_link[s] & VL_EXTERN))
    error(27 /* "redeclaration of" */, quote(name));    /* two have storage */
#ifdef CCISH_LINT
  if (!same_global_type(var_type[s], type))
    error(165 /* "conflicting declaration of" */, quote(name));
#endif
  if (type_count(type) > 0)
    var_type[s] = type;         // extern int a[]; then int a[10];
#ifdef CCISH_LINT
  if (storage == TC_STATIC && !(var_link[s] & VL_STATIC))
    error(217 /* "static declaration follows a non-static declaration"
                 " of" */, quote(name));
#endif
  if (storage == TC_EXTERN)
    return s;
  var_link[s] &= ~VL_EXTERN;
  var_off[s] = data_offset();
  return s;
}

// Imports from other PLASMA modules (approach.md issue 22, decision 35).
// A function or extern global declared with an asm label,
// __asm__("PERR") (which ccpp turns into the id PERR), or declared
// after "#pragma plasma import MODULE" in the same file, is a PLASMA
// import if no linked object defines it. Its PLASMA name is the label,
// else its C name (cclink uppercases it); a pragma also names the
// module to load first, which cclink writes only if an import from it is
// used. A pragma covers the rest of its own file only, so a header's
// doesn't turn the including file's externs into imports. Only the
// few symbols involved are kept, here, as parallel arrays: imp_sym[]
// (a function's index, or IMP_DATA plus a global's), imp_label[] (its
// label's id, or 0) and imp_dep[] (its module: 1 plus its index in
// dep_id[], or 0). dep_id[] holds the modules' names, and dep_file[]
// the file each one's pragma now covers (0 for none: files count
// from 1).
#define MAX_IMPORTS  16     // limit file: declarations with an asm label, or
                            // after #pragma plasma import
static int imp_sym[MAX_IMPORTS];
static int imp_label[MAX_IMPORTS];
static unsigned char imp_dep[MAX_IMPORTS];
static int nimports;
int dep_id[MAX_DEPS];               // (ptop.h; MAX_DEPS is there)
static unsigned char dep_file[MAX_DEPS];
static int ndeps;

// __plasma_import NAME, what ccpp makes of #pragma plasma import NAME:
// module NAME is to be loaded for what the rest of this file declares
// (in place of any earlier pragma of the file's).
static void parse_pimport(void)
{
  int i, d = -1;
  next();                         // __plasma_import
  if (tok != TC_ID)
    error0(13 /* "expected a module name after #pragma plasma"
                 " import" */);
  for (i = 0; i < ndeps; i++) {
    if (dep_file[i] == tok_file)
      dep_file[i] = 0;
    if (dep_id[i] == tok_val)
      d = i;
  }
  if (d < 0) {
    if (ndeps >= MAX_DEPS)
      fatal0(224 /* "too many modules imported" */);
    d = ndeps++;
    dep_id[d] = tok_val;
  }
  dep_file[d] = (unsigned char)tok_file;
  next();
}

// Parse an asm label, __asm__ (NAME), after a declarator, if there is
// one: return NAME's id, or 0.
int parse_asm_label(void)
{
  int label;
  if (tok != TC_ID || tok_val != ID_ASM)
    return 0;
  next();
  expect('(', 57 /* "expected '(' after __asm__, not" */);
  if (tok != TC_ID)
    error(158 /* "expected a PLASMA name in quotes in __asm__, not" */,
          tok_desc());
  label = tok_val;
  next();
  expect(')', 60 /* "expected ')', not" */);
  return label;
}

// Note that the function, or extern global, key (a function's index,
// or IMP_DATA plus a global's) was declared here with asm label label
// (an id, or 0): it is an import if it has a label, or if a pragma
// covers this file. A CMDSYS function without a label stays one (a
// header may declare __puts after a pragma).
void note_import(int key, int label)
{
  int i, d = 0;
  for (i = 0; i < ndeps; i++)
    if (dep_file[i] == tok_file)
      d = i + 1;
  if (!label && (!d || (key < IMP_DATA && find_cmdsys(fn_name[key]) >= 0)))
    return;
  for (i = 0; i < nimports && imp_sym[i] != key; i++)
    ;
  if (i == nimports) {
    if (nimports >= MAX_IMPORTS)
      fatal0(189 /* "too many imports (with an asm label, or after a"
                    " #pragma plasma import)" */);
    nimports++;
    imp_sym[i] = key;
    imp_label[i] = 0;
    imp_dep[i] = 0;
  }
  if (label)
    imp_label[i] = label;
  if (d)
    imp_dep[i] = (unsigned char)d;
}

// For the symbol summary: if key (as for note_import()) is an import,
// set *label to its PLASMA name (its label, else name, its C name) and
// return its module (0 for none, else 1 plus a dep_id[] index);
// otherwise return -1.
int sym_import(int key, int name, int *label)
{
  int i;
  for (i = 0; i < nimports; i++)
    if (imp_sym[i] == key) {
      *label = imp_label[i] ? imp_label[i] : name;
      return imp_dep[i];
    }
  return -1;
}

// For the symbol summary: the number of modules pragmas named.
int dep_count(void)
{
  return ndeps;
}

// Declare the global variable name of type type, with storage class
// storage (TC_STATIC, TC_EXTERN or 0), and parse its asm label and its
// initializer, if any. A global without an initializer is zero when the
// module loads. Only an extern can be an import (a label on a
// definition does nothing).
static void define_global(int name, int type, int storage)
{
  int s, label = parse_asm_label();
  type = parse_var_type(type, 1);
  // In C, the name is in scope in its own initializer.
  s = declare_global(name, type, storage);
  if (storage == TC_EXTERN)
    note_import(IMP_DATA + s, label);
  type = var_type[s];
  if (tok == '=') {
    if (storage == TC_EXTERN)
      error0(216 /* "'extern' with an initializer is not supported" */);
    next();
    var_type[s] = parse_global_init(type, name);
    if (init_known)
      set_known(s, init_val);
    // All zeros (var_off[] is where its data began): in the zero-filled
    // space instead, as if it had no initializer, which C makes the
    // same (C11 6.7.9p10).
    if (data_drop_zeros(var_off[s]))
      zero_var(s, ZERO_GLOBALS);
  } else if (storage != TC_EXTERN) {
    if (is_array(type) && type_count(type) == 0)
      error0(87 /* "array size missing" */);
    // In the zero-filled space, with no data bytes, whatever its
    // size: cclink places it, after every object's data, or on the heap
    // if it's at least BSS_MIN bytes (../doc/xload.md, 3.3).
    zero_var(s, ZERO_GLOBALS);
  }
}

// Put static variable s, of its type's size, in a zero-filled space:
// a static local (local, ZERO_AREA) in the file's zero area, a global
// (ZERO_GLOBALS) in its zero-filled globals' space (../doc/xload.md,
// 3.3).
void zero_var(int s, int local)
{
  int at = zero_next[local], n = type_size(var_type[s]);
  if (at > 0x7FFF - n)
    error0(182 /* "too much data: globals over 32767 bytes" */);
  var_link[s] |= local ? VL_ZAREA : VL_BSS;
  var_off[s] = at;
  zero_next[local] = at + n;
}

// Parse global declarations, after the base type base and the first
// declarator's type and name, with storage class storage: int x = 1,
// *p = &x, buf[100]; A declarator may also declare a function (int x,
// f(int);).
static void parse_globals(int base, int type, int name, int storage)
{
  for (;;) {
    if (is_func(type))
      note_import(declare_function(name, type, storage == TC_STATIC),
                  parse_asm_label());
    else
      define_global(name, type, storage);
    if (tok != ',')
      break;
    next();
    type = parse_declarator(base, 0, &name);
  }
  expect(';', 118 /* "expected ';' after declaration, not" */);
}

// Declare function name of function type type (its return type; the
// parameter count is fixed by the first call), as int f(); does: a
// declarator that isn't a file-scope function's own (int x, f(int);
// fn_t g; int g(int); in a block). In a block it is declared as at
// file scope, from there on. is_static makes it local to the file.
// Returns the function.
int declare_function(int name, int type, int is_static)
{
  int f;
  type = UNQUAL(type_sub(type));
  check_byval(type, 0);
#ifdef CCISH_LINT
  if (find_var(name) >= 0)
    error(27 /* "redeclaration of" */, quote(name));
  check_not_id(name);
#endif
  f = find_func(name);
  if (f < 0)
    f = add_func(name);
#ifdef CCISH_LINT
  if ((fn_flags[f] & FN_DECLARED) && fn_type[f] != type)
    error(165 /* "conflicting declaration of" */, quote(fn_name[f]));
#endif
  fn_type[f] = type;
  if (is_static)
    fn_flags[f] |= FN_STATIC;
  fn_flags[f] |= FN_DECLARED;
  return f;
}

// An extern declaration in a block, after the "extern": each
// declarator declares a global with no storage here (or a function),
// as at file scope, from here on. C would hide the name after the
// block, and ccish doesn't; a visible local of the same name would
// hide the global in the block, so that's an error.
void parse_block_extern(void)
{
  int base = parse_base_type(), type, name, s, label;
  for (;;) {
    type = parse_declarator(base, 0, &name);
    label = parse_asm_label();
    if (is_func(type)) {
      note_import(declare_function(name, type, 0), label);
    } else {
      s = find_var(name);
      if (s >= nglobals)
        error(163 /* "'extern' in a block can't hide a local:" */,
              quote(name));
      if (tok == '=')
        error0(216 /* "'extern' with an initializer is not supported" */);
      s = declare_global(name, parse_var_type(type, 1), TC_EXTERN);
      note_import(IMP_DATA + s, label);
    }
    if (tok != ',')
      break;
    next();
  }
  expect(';', 118 /* "expected ';' after declaration, not" */);
}

// The function begin_function() found or added.
static int new_func;

// Begin the function named name, declared at file scope, at its own
// parameter list's '(' (parse_declarator(), D_PARAMS): find or add it,
// and parse the parameters as its variables. Returns their count, or
// -1 for "()".
int begin_function(int name)
{
  int n;
#ifdef CCISH_LINT
  if (find_var(name) >= 0)
    error(27 /* "redeclaration of" */, quote(name));
  check_not_id(name);
#endif
  new_func = find_func(name);
  if (new_func < 0)
    new_func = add_func(name);
  cur_fn = new_func;              // for messages about its variables
  in_params = 1;
  n = parse_params();
  in_params = 0;
  return n;
}

// Parse a _Static_assert declaration (C11 6.7p6): a constant
// expression that must be nonzero, a string-literal message, and a ';'
// (as gcc and clang require; C's grammar leaves it out). It emits
// nothing: parse_const() parses with the code off, and the message is
// only checked, so it takes no data. As in C, a const variable's value
// isn't a constant here (parse_const() says how to accept one).
void parse_static_assert(void)
{
  int v;
  next();                         // _Static_assert
  expect('(', 320 /* "bad _Static_assert, at" */);
  v = parse_const(321 /* "a _Static_assert's expression must be an"
                         " integer constant" */,
                  322 /* "(a _Static_assert's expression must be one)" */);
  expect(',', 320 /* "bad _Static_assert, at" */);
  if (tok != TC_STR)
    error(320 /* "bad _Static_assert, at" */, tok_desc());
  if (v == 0)                     // before next(): tok_text is the message
    error_d(323 /* "static assertion failed:" */, tok_text);
  next();                         // the message
  expect(')', 320 /* "bad _Static_assert, at" */);
  expect(';', 320 /* "bad _Static_assert, at" */);
}

// Leave a function's scope, after its prototype or its body: no
// function is being parsed, and its parameters and locals, and the tags
// and names declared in them, are gone.
static void end_function_scope(void)
{
  cur_fn = -1;
  nvars = nglobals;
  tag_hide(0);
  id_hide(0);
}

// Parse one top-level item: global variable declarations, a function
// prototype, or a function definition. Any may start with "static",
// which makes it local to the file, or "extern", which declares a
// global without storage, and changes nothing on a function.
static void parse_toplevel(void)
{
  int base, type, nparams, pwords, i, f, name, storage = 0;

  depth = 0;                      // file scope, for tags and names
  if (tok == TC_ID && tok_val == ID_PIMPORT) {
    parse_pimport();
    return;
  }
  if (tok == TC_STATIC_ASSERT) {
    parse_static_assert();
    return;
  }
  if (TC_IS_ID_DECL(tok)) {
    parse_typedef();
    return;
  }
  if (TC_IS_STORAGE(tok)) {
    storage = tok;
#ifdef CCISH_LINT
    if (storage == TC_REGISTER)
      error0(247 /* "a declaration at file scope can't be register" */);
#endif
    next();
    if (!is_type_start())
      error(storage == TC_STATIC
            ? 147 /* "expected a type after 'static', not" */
            : 218 /* "expected a type after 'extern', not" */,
            tok_desc());
  }
  if (tok == TC_KEYWORD)
    not_yet();
  if (!is_type_start())
    error(164 /* "expected a declaration or function definition, not" */,
          tok_desc());
  base = parse_base_type();
  if (tok == ';' && base_tagged) {
    next();                     // struct pt { ... }; declares only
    return;
  }
  type = parse_declarator(base, D_PARAMS, &name);
  if (decl_nparams == -2) {
    parse_globals(base, type, name, storage);
    return;
  }
  // A function, its parameters parsed (begin_function()). Its return
  // type loses its qualifiers: const int f(void) returns an int.
  f = new_func;
  nparams = decl_nparams;
  type = UNQUAL(type_sub(type));
  check_byval(type, 0);
  if (storage == TC_STATIC) {
#ifdef CCISH_LINT
    if ((fn_flags[f] & FN_DECLARED) && !(fn_flags[f] & FN_STATIC))
      error(217 /* "static declaration follows a non-static declaration"
                   " of" */, quote(name));
#endif
    fn_flags[f] |= FN_STATIC;
  }
  assert(frame_next >= 0);
  pwords = frame_next >> 1;
  i = parse_asm_label();
  if (tok == ';') {
    // A prototype. "()" leaves the parameter count unspecified.
    note_import(f, i);
#ifdef CCISH_LINT
    if ((fn_flags[f] & FN_DECLARED) && fn_type[f] != type)
      error(165 /* "conflicting declaration of" */, quote(fn_name[f]));
#endif
    fn_type[f] = type;
    if (nparams >= 0) {
#ifdef CCISH_LINT
      if (nparams_of(f) >= 0 && nparams_of(f) != nparams)
        error(166 /* "conflicting parameter count for" */,
              quote(fn_name[f]));
#endif
      set_nparams(f, nparams);
      set_param_types(f, nparams);
    }
    fn_flags[f] |= FN_DECLARED;
    next();
    end_function_scope();       // and the tags in its parameters
    return;
  }
  if (nparams < 0)
    nparams = 0;
  if (tok != '{')
    error(167 /* "expected '{' or ';' after function header, not" */,
          tok_desc());
#ifdef CCISH_LINT
  for (i = nglobals; i < nvars; i++)
    if (var_name[i] < 0)
      error(168 /* "parameter name missing in definition of" */,
            quote(fn_name[f]));
#endif
  if (fn_flags[f] & FN_DEFINED)
    error(104 /* "redefinition of" */, quote(fn_name[f]));
#ifdef CCISH_LINT
  if ((fn_flags[f] & FN_DECLARED) && fn_type[f] != type)
    error(165 /* "conflicting declaration of" */, quote(fn_name[f]));
#endif
#ifdef CCISH_LINT
  if (nparams_of(f) >= 0 && nparams_of(f) != nparams)
    error(169 /* "parameter count differs from earlier use of" */,
          quote(fn_name[f]));
  if (fn_name[f] == ID_MAIN && !MAIN_NPARAMS_OK(nparams))
    error0(170 /* "main takes no parameters, or two" */);
#endif
  set_param_types(f, nparams);
  fn_type[f] = type;
  set_nparams(f, nparams);
  fn_flags[f] |= FN_DECLARED | FN_DEFINED;

  nlabels = 0;
  ngoto_labels = 0;             // a new goto label table
  nlbls = 0;
  put(IR_FUNC, pwords, f);        // ENTER takes the parameters' words
  parse_block(0);
  check_goto_labels();            // every goto's target was defined
  // Falling off the end of an int or char function returns 0. If the
  // last statement was a return, the peephole stage drops this as
  // dead code.
  if (type != TY_VOID)
    push_zero_result(type);
  put(IR_RETURN, result_words(type), -1);
  put(IR_ENDFUNC, frame_max, f);
  end_function_scope();
}

#ifdef CCISH_STATS
// Report how full the tables got, on one line of stderr, for
// tools/measure.py: functions, variables visible at once, parameter
// types, enum constants and typedef names at once, types, struct tags
// and struct members; then the most any function used: ordinary
// labels, nesting, and case labels in the switches open at once.
static void report_stats(void)
{
  int v[10], i;
  v[0] = nfuncs;
  parse_stats(&v[1], &v[2]);
  v[3] = id_peak();
  type_stats(&v[4], &v[5], &v[6]);
  v[7] = stat_labels;
  v[8] = stat_nest;
  v[9] = stat_cases;
  fputs("cccomp-stats", stderr);
  for (i = 0; i < 10; i++) {
    fputs(" ", stderr);
    fputs(num_text(v[i]), stderr);
  }
  fputs("\n", stderr);
}
#endif

// Stage 1's entry: parse the whole source file. A function called but
// not defined here is left to the final pass, which finds it in
// another file or CMDSYS (approach.md issue 11).
// A macro, not a function: it has one caller.
#define parse_program() while (tok != TC_END) parse_toplevel()

// After an error in the front end: close its open files (ProDOS needs
// it), writing out what the IR file holds.
void compile_abort(void)
{
  tok_close();
  ir_abort();
}

// The front end: compile the token stream and name file of input
// name in (ccpp's NAME.i and NAME.nam) into the IR file of output name
// base (approach.md 18.7), for the back end.
void compile_file(const char *in, const char *base)
{
  tok_open(in);
  ir_open(base);
  parse_program();
  tok_close();
  src_line = 0;
  ir_finish();
#ifdef CCISH_STATS
  report_stats();
#endif
}

// Function f's SYM_* flags for the symbol summary, and its type hash
// in *hash: of its return type and parameter types, or 0 if they
// aren't known (int f();).
int func_sym(int f, int *hash)
{
  int flags = 0, c, i, h;
  if (fn_flags[f] & FN_DEFINED)
    flags |= SYM_DEFINED;
  if (sym_import(f, 0, &h) >= 0)
    flags |= SYM_FIMPORT;
  if (fn_flags[f] & FN_CALLED)
    flags |= SYM_CALLED;
  if (fn_type[f] != TY_VOID)
    flags |= SYM_RETURNS;
  if (fn_flags[f] & FN_STATIC)
    flags |= SYM_LOCAL;
  c = find_cmdsys(fn_name[f]);
  if (c >= 0)
    flags |= matches_cmdsys(f, c) ? SYM_CMDSYS : SYM_NOTCMDSYS;
  *hash = 0;
  if (fn_ptypes[f] >= 0) {
    h = hash_byte(type_hash(fn_type[f], 0), '(');
    for (i = 0; i < nparams_of(f); i++)
      h = type_hash(param_types[fn_ptypes[f] + i], h);
    h = hash_byte(h, ')');
    *hash = h ? h : 1;
  }
  return flags;
}

// Global v's SYM_* flags for the symbol summary, with its data offset
// (if defined), size (0 if an array's size is unknown), type hash
// (leaving out an array's size, so that int a[] matches int a[10]) and
// name.
int data_sym(int v, int *off, int *size, int *hash, int *name)
{
  int t = var_type[v], flags = 0, h = 0;
  if (!(var_link[v] & VL_EXTERN))
    flags |= SYM_DEFINED;
  if (sym_import(IMP_DATA + v, 0, &h) >= 0)
    flags |= SYM_DIMPORT;
  h = 0;
  if (var_link[v] & VL_USED)
    flags |= SYM_CALLED;
  if (var_link[v] & VL_STATIC)
    flags |= SYM_LOCAL;
  if (var_link[v] & VL_BSS)
    flags |= SYM_BSS;           // var_off[] is in the zero-filled space
  if (var_link[v] & VL_INDATA)
    flags |= SYM_INDATA;
  *off = flags & SYM_DEFINED ? var_off[v] : 0;
  *size = is_array(t) && type_count(t) == 0 ? 0 : type_size(t);
  if (is_array(t)) {
    h = hash_byte(h, '[');
    t = type_sub(t);
  }
  h = type_hash(t, h);
  *hash = h ? h : 1;
  *name = var_name[v];
  return flags;
}
