// cccomp's parser, stage 1: the table of functions, which irout.c
// reads too, the operand type and the state all the parser's files
// (parse.c, pexpr.c, pstmt.c, ptop.c) share, and the front end's
// entry. Include after obj.h.
#ifndef PARSE_H
#define PARSE_H

// Functions: declared, defined or called. A function is an index into
// these parallel arrays (approach.md, "Sym split design"), each
// MAX_FUNCS long.
enum { FN_DECLARED = 1, FN_DEFINED = 2, FN_CALLED = 4, FN_COUNTED = 8,
       FN_STATIC = 16, FN_NAMED = 32 };   // FN_NAMED: its name is in
                                          // the IR file (irout.c)
extern int fn_name[MAX_FUNCS];      // its name: an id
extern char fn_type[MAX_FUNCS];     // its return type, unqualified (C17
                                    // ignores a return type's
                                    // qualifiers), so it fits a char
extern int fn_ptypes[MAX_FUNCS];    // its parameter types' index in
                                    // param_types[] (parse.c), or -1
                                    // until a declaration gives them
extern char fn_nparams[MAX_FUNCS];  // its parameter count, once known
                                    // (FN_COUNTED)
extern char fn_flags[MAX_FUNCS];    // FN_*
extern int nfuncs;

// An operand on its way through the expression parser. The middle
// three kinds, K_VAR, K_DEREF and K_ELEM, are places (lvalues, or
// arrays): what to load or store isn't decided until the parser sees
// what follows.
//
// K_VAR is a variable, local or global; locals are in the frame, and
// globals are in the data segment (LAW / SAW, with relocated addresses).
//
// K_ELEM is an object whose address can be computed at any time from
// variables and constants, without a temporary: base + index * scale +
// disp. The base is the pointer in the variable at off (*p, p[i]) or
// the address of the array at off (a[i]), and the index is a variable,
// or there is none. So nothing is emitted until the parser sees whether
// the object is assigned to, and a[i] = x is "x LLA a LLW i IDXW SW"
// (approach.md 4(a), and "Pending array elements" under Milestones). A
// constant index on an array variable needs no address at all: a[2] is
// a K_VAR at a fixed offset.
//
// An object reached through any other address (*f(), a[i + 1]) is
// K_DEREF, with the address on the stack. Storing into it needs the
// value first and the address on top, and there is no SWAP. So for
// *f() = e, the parser emits e after the address, then has the two
// blocks of code exchanged (swap_blocks(), approach.md 4(d2)):
// "e addr SW", with no temporary.
// Until it sees whether the assignment's value is used, the result is
// K_STORE: the address, then the value, are the last code emitted, and
// the exchange and the store wait. If the value is used, a DUP goes
// after the value first, so the value stays below the store.
//
// Compound assignment and ++/-- need the address twice (for the load
// and the store), which no reordering can provide. For those, a
// K_DEREF's address is saved in a temporary local (spill()), and it
// becomes a K_ELEM based on that temporary (approach.md 4(e)). A
// compound assignment to an indexed K_ELEM (a[i] += e) is spilled too,
// so its address is computed once. A SWAP opcode would make these
// spills unnecessary: addr DUP LW e ADD SWAP SW (approach.md issue 21).
//
// A place of array type is the array object. Used as a value, it is a
// pointer to its first element (rvalue()); value_type() gives that
// type. It isn't converted before it is used, so that a[i], *a and &a
// can still see the array.
//
// A function is like an array (approach.md, "Function pointer
// design"): used as a value, it is a pointer to itself. A named one is
// K_FUNC. Any other operand of function type (*fp) is the function
// whose address is the operand's value: the place's, the constant's,
// or the value on the stack. So *fp is fp until it is loaded, and &*fp
// is fp again.
enum {
  K_VALUE,        // its value is on the stack (a struct's: its words,
                  // first word deepest; see push_struct_words())
  K_VOID,         // a void call: nothing on the stack
  K_CONST,        // a constant, not emitted yet (val = value)
  K_VAR,          // a variable, not loaded yet (off, global)
  K_DEREF,        // the object at the address on the stack
  K_ELEM,         // the object at base + index * scale + disp
  K_STORE,        // an assignment through an address, whose store
                  // waits (m0, m1, mend, stype, vmask)
  K_JUMP,         // a condition (&&, ||, ! on them) whose code is
                  // emitted: it jumps to label val when false, and
                  // otherwise is true: it falls through, or jumps
                  // to label off, if off isn't -1 (approach.md C4,
                  // "Conditions as jumps")
  K_ADDR,         // an address constant, not emitted yet: off bytes
                  // into the data (global 1), off bytes past extern
                  // global v's address (global 2 + v), or the string
                  // pool's offset off (global G_STR: a literal, from
                  // disp to val, its NUL's end)
  K_FUNC          // function val's address, not emitted yet: type is
                  // the function's (a function designator), or a
                  // pointer type (&f, or a cast)
};

#define G_STR (-1)      // K_ADDR's global field: the string pool

typedef struct {
  int kind;
  int type;       // its C type, unqualified. A char value on the stack
                  // is always 0 to 255, so it needs no conversion to
                  // char.
  int quals;      // a place: its object's qualifiers (Q_CONST,
                  // Q_VOLATILE), which type leaves out
  int nonice;     // K_CONST: not an integer constant expression in C,
                  // because a const variable's value went into it
                  // (const int N = 10; N + 1). It can't size an array
                  // or initialize a global, and 0 isn't a null
                  // pointer constant.
  int off;        // K_VAR: its offset; K_ELEM: the base's offset;
                  // K_JUMP: its true label, still to place, or -1
  int global;     // K_VAR, K_ELEM: 1 if off is a data offset (a
                  // global), 0 if a frame offset, or 2 + v if off is
                  // an addend to extern global v (var_where())
  int val;        // K_CONST: the value; K_JUMP: the false label
  int is_bool;    // K_VALUE: known to be 1 or 0 (a comparison, !, &&)
  int assigned;   // K_VAR, K_ELEM: the result of an assignment to
                  // the object (K_VAR: only a struct's), so not an
                  // lvalue. Its value, if needed, is read back from
                  // the object.
  // K_ELEM only:
  int base_arr;   // the base is the address of the array at off, not
                  // the pointer in the variable at off
  int idx;        // offset of the index variable, or -1
  int idx_global; // the index variable is a global
  int idx_char;   // the index variable is a char
  int scale;      // bytes per step of the index
  int disp;       // a constant number of bytes added
  // K_STORE only. The address's code is from IR position m0 to m1,
  // and the value's from m1 to mend. is_bool says whether the value
  // is 1 or 0.
  int m0, m1, mend;
  int stype;      // the type of the object stored into
  int vmask;      // the value needs ANDI 255 to be what was stored
                  // (an int stored into a char)
} Operand;

// C precedence levels (higher binds tighter).
enum {
  PREC_COMMA = 1, PREC_ASSIGN, PREC_COND, PREC_OROR, PREC_ANDAND,
  PREC_OR, PREC_XOR, PREC_AND, PREC_EQUALITY, PREC_RELATIONAL,
  PREC_SHIFT, PREC_ADD, PREC_MUL
};


// Variables: one scope stack of parallel arrays in parse.c
// (approach.md, "Sym split design"): the globals at the bottom, from 0
// to nglobals; a function's parameters and locals above them,
// innermost last, popped when it ends. var_flags[] holds the block
// depth plus:
enum { V_DEPTH = 0x3F, V_DATA = 0x40, V_KNOWN = 0x80 };
extern int var_name[MAX_VARS];      // its name: an id, or -1
extern int var_type[MAX_VARS];      // its type, qualified
extern int var_off[MAX_VARS];       // frame offset, or data offset
extern char var_link[MAX_VARS];     // its linkage (the VL_* bits)
extern int nvars, nglobals;
extern char var_flags[MAX_VARS];  // the depth, plus the V_* bits
extern int var_val[MAX_VARS];     // its known value (V_KNOWN)

// A global's linkage, for the symbol summary (approach.md issue 11):
// VL_STATIC if it is local to the file, VL_USED if code or data refers
// to it, and VL_EXTERN while it is declared extern with no storage in
// this file (var_off[] is then 0, the base of an addend). VL_BSS if it
// is defined with no initializer and is at least BSS_MIN bytes, so has
// no data bytes, only an offset (var_off[]) in the file's zero-filled
// space, and may move to the heap (approach.md, "Large zero-filled data
// on the heap"); VL_INDATA if an initializer holds its address, which
// stops that. 0 for a local.
enum { VL_STATIC = 1, VL_USED = 2, VL_EXTERN = 4, VL_BSS = 8,
       VL_INDATA = 16 };

// Parameter types of declared functions, in parse.c.
#define MAX_PARAM_TYPES 352 // parameter types of all declared functions
                            // (320, pexpr.c)
extern char param_types[MAX_PARAM_TYPES];   // unqualified types

// The parser's state, in parse.c, shared by the parser's files.
extern int depth;           // block depth; parameters are at depth 1
extern int frame_next;      // next free frame offset
extern int frame_max;       // frame size needed so far
extern int cur_fn;          // the function being parsed, or -1
#define MAX_NEST     32     // nesting of parentheses and blocks
extern int nest;            // nesting depth, to limit recursion
extern int in_params;       // parsing a function's parameter list
#define MAX_HELD      8     // left operands held back at once
extern int nheld;          // left operands held back, so far
extern int suppress;       // parsing code that can never run
extern char label_used[MAX_LABELS];  // some branch to it so far
extern int nlabels;         // labels made in the function, so far
extern int base_tagged;     // parse_base_type() was a struct or enum
extern int want_addr;       // a global's initializer, for an address

// What parse.c makes available to the parser's other files: the
// message and token helpers, the tables' operations, the type-name
// parsing, put (IR output with the pending constants), and the
// operand machinery.
#ifdef CCISH_DIAG
const char *quote(int name);
const char *tag_desc(int tag);
#else
#define quote(name) ((const char *)0)   // no detail in the small build
#define tag_desc(tag) ((const char *)0)
#endif
void not_yet(void);
void expect(int t, int msg);
int  find_func(int name);
int  add_func(int name);
int  nparams_of(int f);
void set_nparams(int f, int n);
int  find_cmdsys(int name);
void declare_cmdsys(int f, int c);
int  matches_cmdsys(int f, int c);
int  find_var(int name);
int  find_id(int name);
int  typedef_type(int name);
void check_not_id(int name);
void check_not_var(int name);
int  add_global(int name, int type);
int  add_local(int name, int type, int size);
int  push_var(int name, int type, int off, int flags);
void set_param_types(int f, int n);
int  is_type_start(void);
int  parse_base_type(void);
int  parse_quals(void);
void need_complete(int t, int msg);
int  new_label(void);
void branch(int op, int n);
void place_label(int n);
void place_forward(int n);
void join_label(int from, int to);
// Goto labels (decision 34): id is the label's identifier. A forward
// goto reserves the label; its ':' defines it; the function's end
// checks that every target was defined.
extern int ngoto_labels;            // goto labels made
extern int nlbls;                   // goto labels defined or used;
                                    // both reset at a function's start

int  reserve_goto_label(int id);
int  define_goto_label(int id);
void check_goto_labels(void);
void place_true(Operand *o);
void cond_branch(Operand *o, int n, int on_true);
void enter_nest(void);
int  wrap16(unsigned u);
int  code_start(int mark, int h);
void put(int op, int a, int f);
void begin_expr(void);
void end_expr(void);
void rvalue(Operand *o);
void discard(Operand *o);
int  struct_words(int t);
void store_var(Operand *o);
int  addr_const(Operand *o);
int  value_type(int t);
int  result_words(int t);
// For the symbol summary: function f's SYM_* flags and type hash, and
// the number of globals and global v's flags, data offset, size, type
// hash and name (an id).
int  func_sym(int f, int *hash);
int  data_sym(int v, int *off, int *size, int *hash, int *name);
// Imports (ptop.c): whether function f, or global v as IMP_DATA + v,
// is one, with its PLASMA name (name, an id, if it has no label) and
// module; and the modules.
#define IMP_DATA 0x4000
int  sym_import(int key, int name, int *label);
int  dep_count(void);
void compile_file(const char *in, const char *base);   // the front end
void compile_abort(void);                   // after an error


// The expression machinery's internals, exposed for pexpr.c (the
// expression grammar): the rest of the output and constant
// folding, the place machinery, and the type checks.
extern int in_sizeof;        // parsing sizeof's operand (no scaling)
int  hold(int op, int a, int f);
int  var_where(int v);
void check_right_const(int op, int v);
int  fold(int op, int a, int b, int uns);
int  swapped_op(int op);
int  is_compare(int op);
int  movable(int global);
void push_var_addr(int off, int global);
int  max_offset(int global);
int  holds_qual(int t, int q);      // q if t is or holds a q
int  acc_form(int t, int quals);    // an access's IR_FORM bits
int  is_place(Operand *o);
int  addr_op(Operand *o, int *a, int *f);
// Is type t (or a qualified one: const unsigned) unsigned int?
#define IS_UNS(t)  (UNQUAL(t) == TY_UINT)
// Integer type t promoted, as for a unary operator or a shift's left
// operand: unsigned int stays so, the others become int; TY_UINT is
// TY_INT + 1 (type.h), so it's TY_INT plus IS_UNS's 0 or 1.
#define PROMOTED(t)  (TY_INT + IS_UNS(t))
// The IR op that stores a value of type t, with qualifiers quals, at an
// address (a char's a byte; a volatile's the volatile form).
#define STORE_OP(t, quals)  IR_FORM(IR_STORE, acc_form(t, quals))
int  arith_type(int a, int b);
void make_elem(Operand *o, int off, int global, int base_arr);
void push_address(Operand *o);
int  new_temp(void);
void spill(Operand *o);
void swap_blocks(int start, int mid, int fixed);
void finish_store(Operand *o, int keep);
void need_lvalue(Operand *o, int what);
void assign_to(Operand *o);
int  is_null(Operand *o);
int  elem_size(int t);
#ifdef CCISH_STATS
void parse_stats(int *vars_peak, int *ptypes);
#endif
#endif
