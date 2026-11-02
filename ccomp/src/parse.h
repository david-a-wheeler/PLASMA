/*
 * cccomp's parser, stage 1 (parse.c): the table of functions, which
 * irout.c reads too, the operand type shared by the parser's files,
 * and the front end's entry. Include after obj.h.
 */
#ifndef PARSE_H
#define PARSE_H

/* Functions: declared, defined or called. A function is an index into
 * these parallel arrays (approach.md, "Sym split design"), each
 * MAX_FUNCS long. */
enum { FN_DECLARED = 1, FN_DEFINED = 2, FN_CALLED = 4, FN_COUNTED = 8,
       FN_STATIC = 16, FN_NAMED = 32 };   /* FN_NAMED: its name is in
                                           * the IR file (irout.c) */
extern int fn_name[MAX_FUNCS];      /* its name: an id */
extern char fn_type[MAX_FUNCS];     /* its return type, unqualified (C17
                                     * ignores a return type's
                                     * qualifiers), so it fits a char */
extern int fn_ptypes[MAX_FUNCS];    /* its parameter types' index in
                                     * param_types[] (parse.c), or -1
                                     * until a declaration gives them */
extern char fn_nparams[MAX_FUNCS];  /* its parameter count, once known
                                     * (FN_COUNTED) */
extern char fn_flags[MAX_FUNCS];    /* FN_* */
extern int nfuncs;

/*
 * An operand on its way through the expression parser. The middle
 * three kinds, K_VAR, K_DEREF and K_ELEM, are places (lvalues, or
 * arrays): what to load or store isn't decided until the parser sees
 * what follows.
 *
 * K_VAR is a variable, local or global; locals are in the frame, and
 * globals are in the data segment (LAW / SAW, with relocated addresses).
 *
 * K_ELEM is an object whose address can be computed at any time from
 * variables and constants, without a temporary: base + index * scale +
 * disp. The base is the pointer in the variable at off (*p, p[i]) or
 * the address of the array at off (a[i]), and the index is a variable,
 * or there is none. So nothing is emitted until the parser sees whether
 * the object is assigned to, and a[i] = x is "x LLA a LLW i IDXW SW"
 * (approach.md 4(a), and "Pending array elements" under Milestones). A
 * constant index on an array variable needs no address at all: a[2] is
 * a K_VAR at a fixed offset.
 *
 * An object reached through any other address (*f(), a[i + 1]) is
 * K_DEREF, with the address on the stack. Storing into it needs the
 * value first and the address on top, and there is no SWAP. So for
 * *f() = e, the parser emits e after the address, then has the two
 * blocks of code exchanged (swap_blocks(), approach.md 4(d2)):
 * "e addr SW", with no temporary.
 * Until it sees whether the assignment's value is used, the result is
 * K_STORE: the address, then the value, are the last code emitted, and
 * the exchange and the store wait. If the value is used, a DUP goes
 * after the value first, so the value stays below the store.
 *
 * Compound assignment and ++/-- need the address twice (for the load
 * and the store), which no reordering can provide. For those, a
 * K_DEREF's address is saved in a temporary local (spill()), and it
 * becomes a K_ELEM based on that temporary (approach.md 4(e)). A
 * compound assignment to an indexed K_ELEM (a[i] += e) is spilled too,
 * so its address is computed once. A SWAP opcode would make these
 * spills unnecessary: addr DUP LW e ADD SWAP SW (approach.md issue 21).
 *
 * A place of array type is the array object. Used as a value, it is a
 * pointer to its first element (rvalue()); value_type() gives that
 * type. It isn't converted before it is used, so that a[i], *a and &a
 * can still see the array.
 */
enum {
  K_VALUE,        /* its value is on the stack (a struct's: its words,
                   * first word deepest; see push_struct_words()) */
  K_VOID,         /* a void call: nothing on the stack */
  K_CONST,        /* a constant, not emitted yet (val = value) */
  K_VAR,          /* a variable, not loaded yet (off, global) */
  K_DEREF,        /* the object at the address on the stack */
  K_ELEM,         /* the object at base + index * scale + disp */
  K_STORE,        /* an assignment through an address, whose store
                   * waits (m0, m1, mend, stype, vmask) */
  K_JUMP,         /* a condition (&&, ||, ! on them) whose code is
                   * emitted: it jumps to label val when false, and
                   * otherwise is true: it falls through, or jumps
                   * to label off, if off isn't -1 (approach.md C4,
                   * "Conditions as jumps") */
  K_ADDR          /* an address constant, not emitted yet: off bytes
                   * into the data (global 1), off bytes past extern
                   * global v's address (global 2 + v), or the string
                   * pool's offset off (global G_STR: a literal, from
                   * disp to val, its NUL's end) */
};

#define G_STR (-1)      /* K_ADDR's global field: the string pool */

typedef struct {
  int kind;
  int type;       /* its C type, unqualified. A char value on the stack
                   * is always 0 to 255, so it needs no conversion to
                   * char. */
  int quals;      /* a place: its object's qualifiers (Q_CONST,
                   * Q_VOLATILE), which type leaves out */
  int nonice;     /* K_CONST: not an integer constant expression in C,
                   * because a const variable's value went into it
                   * (const int N = 10; N + 1). It can't size an array
                   * or initialize a global, and 0 isn't a null
                   * pointer constant. */
  int off;        /* K_VAR: its offset; K_ELEM: the base's offset;
                   * K_JUMP: its true label, still to place, or -1 */
  int global;     /* K_VAR, K_ELEM: 1 if off is a data offset (a
                   * global), 0 if a frame offset, or 2 + v if off is
                   * an addend to extern global v (var_where()) */
  int val;        /* K_CONST: the value; K_JUMP: the false label */
  int is_bool;    /* K_VALUE: known to be 1 or 0 (a comparison, !, &&) */
  int assigned;   /* K_VAR, K_ELEM: the result of an assignment to
                   * the object (K_VAR: only a struct's), so not an
                   * lvalue. Its value, if needed, is read back from
                   * the object. */
  /* K_ELEM only: */
  int base_arr;   /* the base is the address of the array at off, not
                   * the pointer in the variable at off */
  int idx;        /* offset of the index variable, or -1 */
  int idx_global; /* the index variable is a global */
  int idx_char;   /* the index variable is a char */
  int scale;      /* bytes per step of the index */
  int disp;       /* a constant number of bytes added */
  /* K_STORE only. The address's code is from IR position m0 to m1,
   * and the value's from m1 to mend. is_bool says whether the value
   * is 1 or 0. */
  int m0, m1, mend;
  int stype;      /* the type of the object stored into */
  int vmask;      /* the value needs ANDI 255 to be what was stored
                   * (an int stored into a char) */
} Operand;

/* C precedence levels (higher binds tighter). */
enum {
  PREC_COMMA = 1, PREC_ASSIGN, PREC_COND, PREC_OROR, PREC_ANDAND,
  PREC_OR, PREC_XOR, PREC_AND, PREC_EQUALITY, PREC_RELATIONAL,
  PREC_SHIFT, PREC_ADD, PREC_MUL
};

void parse_program(void);
/* For the symbol summary: function f's SYM_* flags and type hash, and
 * the number of globals and global v's flags, data offset, size, type
 * hash and name (an id). */
int  func_sym(int f, int *hash);
int  data_sym_count(void);
int  bss_size(void);                /* the zero-filled space's bytes */
int  data_sym(int v, int *off, int *size, int *hash, int *name);
void compile_file(const char *in, const char *base);   /* the front end */
void compile_abort(void);                   /* after an error */

#endif
