/*
 * Stage 1: the parser. Recursive descent for declarations and
 * statements, precedence climbing for expressions (approach.md, issue
 * 5). It emits IR ops as it goes (ir_put) and builds no syntax tree.
 *
 * Language so far (milestones 1 to 5, and M6 steps 1 to 3):
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
 *   - global variables, private to the module, declared anywhere
 *     between functions, with optional initializers, computed at compile
 *     time (see "Global initializers" below);
 *   - static: a static local is a variable in the data segment, as a
 *     global is (S_GLOBAL), with block scope, and a global's kind of
 *     initializer; on a global or a function, static changes nothing
 *     yet, since nothing is exported;
 *   - const and volatile, as part of the type (bits in the type handle;
 *     see type.c). Operands carry unqualified types, and a place keeps
 *     its object's qualifiers in Operand.quals. A const object can't be
 *     assigned to; dropping a qualifier from a pointer's target is a
 *     warning. Reading a const scalar with a constant initializer gives
 *     its value at compile time (see parse_primary()), but that value
 *     isn't a C constant expression (Operand.nonice);
 *   - casts;
 *   - statements: return, if/else, while, do, for, break, continue,
 *     expression statements, empty statements and nested blocks;
 *   - expressions: integer and character constants, string literals,
 *     variables, function calls, parentheses, the pstr() builtin,
 *     unary - + ~ ! * & ++ -- and sizeof, postfix ++ -- [] . ->, binary
 *     * / % + - << >> & ^ |, comparisons, && || ?:, and
 *     = += -= *= /= %= &= |= ^= <<= >>=. Pointer arithmetic scales by
 *     the element size, pointers compare unsigned, pointer types are
 *     checked as strictly as C says, and an array used as a value is a
 *     pointer to its first element.
 * A function called but not defined in the file must be one of the
 * CMDSYS functions in cmdsys_funcs[] below; it is imported from CMDSYS.
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
 * after it, and then the two blocks are exchanged in stage 2's buffer
 * (ir_rotate(), approach.md 4(d2)). The parser marks where blocks start
 * with ir_here(). i + p is reordered the same way, to scale i after p.
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
#include <string.h>
#include "ccish.h"

Sym syms[MAX_SYMS];         /* functions */
int nsyms;

static Sym locals[MAX_LOCALS];  /* parameters and locals in scope */
static int nlocals;
static int depth;           /* block depth; parameters are at depth 1 */
static int frame_next;      /* next free frame offset */
static int frame_max;       /* frame size needed so far */
static Sym *cur_fn;
static int in_params;       /* parsing a function's parameter list */
static int nest;            /* current nesting depth, to limit recursion */

/* Return name in single quotes, for error messages. The result is in a
 * static buffer. */
static const char *quote(const char *name)
{
    static char buf[MAX_NAME + 3];
    strcpy(buf, "'");
    strcat(buf, name);
    strcat(buf, "'");
    return buf;
}

/* Report that the current token starts a C feature that isn't
 * supported yet. */
static void not_yet(void)
{
    error("not supported yet:", tok_desc());
}

/* Skip the current token, which must be t; otherwise report what was
 * expected and what was found. */
static void expect(int t, const char *what)
{
    if (tok != t)
        error(what, tok_desc());
    next();
}

/* Count one more level of nesting, so the parser's recursion depth
 * stays bounded. The caller decrements nest when done. */
static void enter_nest(void)
{
    if (++nest > MAX_NEST)
        fatal("nesting too deep", NULL);
}

/* The function symbol named name, or NULL. */
static Sym *find_func(const char *name)
{
    int i;
    for (i = 0; i < nsyms; i++)
        if (!strcmp(syms[i].name, name))
            return &syms[i];
    return NULL;
}

/* Add a function symbol, not yet declared or defined, with an unknown
 * parameter count. */
static Sym *add_func(const char *name)
{
    Sym *s;
    if (nsyms >= MAX_SYMS)
        fatal("too many functions", NULL);
    s = &syms[nsyms++];
    memset(s, 0, sizeof(*s));
    strcpy(s->name, name);
    s->kind = S_FUNC;
    s->type = TY_INT;       /* C89 implicit declarations return int */
    s->ptypes = -1;
    s->nparams = -1;
    return s;
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
static const struct { const char *name, *sig; } cmdsys_funcs[] = {
    {"putc", "vc"}, {"putln", "v"}, {"puts", "vS"}, {"puti", "vi"},
    {"putb", "vi"}, {"puth", "vi"}, {"getc", "i"}, {"gets", "sc"},
    {"call", "iiiiii"}, {"syscall", "iip"}, {"heapmark", "p"},
    {"heapallocalign", "piip"}, {"heapalloc", "pi"}, {"heaprelease", "ip"},
    {"heapavail", "i"}, {"memset", "vpii"}, {"memcpy", "vpPi"},
    {"strcpy", "ssS"}, {"strcat", "ssS"}, {"toupper", "ii"}, {"sext", "ii"},
    {"isugt", "iii"}, {"isuge", "iii"}, {"isult", "iii"}, {"isule", "iii"},
    {NULL, NULL}
};

/* The index of name in cmdsys_funcs[], or -1 if it isn't there. */
static int find_cmdsys(const char *name)
{
    int i;
    for (i = 0; cmdsys_funcs[i].name; i++)
        if (!strcmp(cmdsys_funcs[i].name, name))
            return i;
    return -1;
}

/* Innermost visible local with this name, or NULL. */
static Sym *find_local(const char *name)
{
    int i;
    for (i = nlocals - 1; i >= 0; i--)
        if (!strcmp(locals[i].name, name))
            return &locals[i];
    return NULL;
}

/* Global variables, in declaration order. */
static Sym globals[MAX_GLOBALS];
static int nglobals;

/* The global variable with this name, or NULL. */
static Sym *find_global(const char *name)
{
    int i;
    for (i = 0; i < nglobals; i++)
        if (!strcmp(globals[i].name, name))
            return &globals[i];
    return NULL;
}

/* The variable a name refers to here: the innermost local, else a
 * global, else NULL. */
static Sym *find_var(const char *name)
{
    Sym *s = find_local(name);
    return s ? s : find_global(name);
}

/* Declare a global variable of the given type, at the next data
 * offset. The caller then writes its bytes to the data stream: its
 * initial value, or zeros. */
static Sym *add_global(const char *name, int type)
{
    Sym *s;
    if (find_global(name) || find_func(name))
        error("redeclaration of", quote(name));
    if (nglobals >= MAX_GLOBALS)
        fatal("too many global variables", NULL);
    s = &globals[nglobals++];
    memset(s, 0, sizeof(*s));
    strcpy(s->name, name);
    s->kind = S_GLOBAL;
    s->type = type;
    s->offset = data_offset();
    return s;
}

/* Declare a local (or parameter) of the given type at the current
 * depth, and give it a frame slot of size bytes. An unnamed prototype
 * parameter has the name "". */
static Sym *add_local(const char *name, int type, int size)
{
    Sym *s = find_local(name);
    if (name[0] && s && s->depth == depth)
        error("redeclaration of", quote(name));
    if (nlocals >= MAX_LOCALS)
        fatal("too many variables in scope", NULL);
    if (frame_next + size > 255)
        error("too many local variables in", quote(cur_fn->name));
    s = &locals[nlocals++];
    memset(s, 0, sizeof(*s));
    strcpy(s->name, name);
    s->kind = S_LOCAL;
    s->type = type;
    s->depth = depth;
    s->offset = frame_next;
    frame_next += size;
    if (frame_next > frame_max)
        frame_max = frame_next;
    return s;
}

/* Declare a static local of the given type at the current depth. It has
 * block scope, like any local, but lives in the data segment like a
 * global (S_GLOBAL, at the next data offset), so it keeps its value
 * between calls and takes no frame space. The caller then writes its
 * bytes to the data stream: its initial value, or zeros. */
static Sym *add_static_local(const char *name, int type)
{
    Sym *s = find_local(name);
    if (s && s->depth == depth)
        error("redeclaration of", quote(name));
    if (nlocals >= MAX_LOCALS)
        fatal("too many variables in scope", NULL);
    s = &locals[nlocals++];
    memset(s, 0, sizeof(*s));
    strcpy(s->name, name);
    s->kind = S_GLOBAL;
    s->type = type;
    s->depth = depth;
    s->offset = data_offset();
    return s;
}

/* Parameter types of declared functions, n at a time; Sym.ptypes says
 * where a function's start. */
static int param_types[MAX_PARAM_TYPES];
static int nparam_types;

/* Record that fn's n parameters have the types of locals[0..n-1] (the
 * parameters just parsed), or check them against the types an earlier
 * declaration gave. The caller has already checked the count. A
 * parameter's own qualifiers don't count, as in C: f(const int n) and
 * f(int n) are the same function type. */
static void set_param_types(Sym *fn, int n)
{
    int i;
    if (fn->ptypes >= 0) {
        for (i = 0; i < n; i++)
            if (param_types[fn->ptypes + i] != unqual(locals[i].type))
                error("conflicting parameter types for", quote(fn->name));
        return;
    }
    if (nparam_types + n > MAX_PARAM_TYPES)
        fatal("too many function parameters in all", NULL);
    fn->ptypes = nparam_types;
    for (i = 0; i < n; i++)
        param_types[nparam_types++] = unqual(locals[i].type);
}

/* Is t a token that starts a type name? */
static int is_type_start(int t)
{
    return t == T_INT || t == T_CHAR || t == T_VOID || t == T_CONST
           || t == T_VOLATILE || t == T_STRUCT;
}

/* Describe tag for a message: 'struct name', or an unnamed struct. The
 * result may be in a static buffer. */
static const char *tag_desc(int tag)
{
    static char buf[MAX_NAME + 10];
    if (tag_name(tag) < 0)
        return "an unnamed struct";
    sprintf(buf, "'struct %s'", name_of(tag_name(tag)));
    return buf;
}

/* Report an error if t is an incomplete struct type, which has no size
 * yet; what says what would have that type ("a variable"). */
static void need_complete(int t, const char *what)
{
    char msg[48];
    if (is_struct(t) && type_size(t) == 0) {
        sprintf(msg, "%s can't have the incomplete type", what);
        error(msg, tag_desc(struct_tag(t)));
    }
}

/* Parse any qualifiers (const, volatile), and return them as Q_ bits. */
static int parse_quals(void)
{
    int q = 0;
    while (tok == T_CONST || tok == T_VOLATILE) {
        q |= tok == T_CONST ? Q_CONST : Q_VOLATILE;
        next();
    }
    return q;
}

static int parse_struct(void);

/* Parse a basic type name (int, char, void, or a struct type), with any
 * qualifiers before or after it (const char, char const), and return
 * its type. */
static int parse_base_type(void)
{
    int q = parse_quals(), t = TY_INT;
    if (tok == T_STRUCT) {
        t = parse_struct();
    } else {
        if (tok == T_CHAR)
            t = TY_CHAR;
        else if (tok == T_VOID)
            t = TY_VOID;
        else if (tok != T_INT)
            error("expected a type, not", tok_desc());
        next();
    }
    return t | q | parse_quals();
}

/* Parse any '*'s after a base type t: each makes a pointer to the type
 * before it, so "char **" is a pointer to a pointer to char. Qualifiers
 * after a '*' qualify that pointer: char *const p is a const pointer to
 * char. */
static int parse_pointers(int t)
{
    while (tok == '*') {
        next();
        t = pointer_to(t);
        t |= parse_quals();
    }
    return t;
}

/* The type a letter stands for in a cmdsys_funcs[] signature: S and P
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

/* Declare fn as CMDSYS function c of cmdsys_funcs[], with its types. */
static void declare_cmdsys(Sym *fn, int c)
{
    const char *sig = cmdsys_funcs[c].sig;
    int i, n = (int)strlen(sig) - 1;
    if (nparam_types + n > MAX_PARAM_TYPES)
        fatal("too many function parameters in all", NULL);
    fn->type = sig_type(sig[0]);
    fn->nparams = n;
    fn->ptypes = nparam_types;
    for (i = 0; i < n; i++)
        param_types[nparam_types++] = sig_type(sig[i + 1]);
    fn->declared = 1;
}

/* Does fn's declaration match CMDSYS function c's types? Parameter
 * types that were never declared (an implicit declaration) match. */
static int matches_cmdsys(Sym *fn, int c)
{
    const char *sig = cmdsys_funcs[c].sig;
    int i, n = (int)strlen(sig) - 1;
    if (fn->type != sig_type(sig[0]) || fn->nparams != n)
        return 0;
    if (fn->ptypes >= 0)
        for (i = 0; i < n; i++)
            if (param_types[fn->ptypes + i] != sig_type(sig[i + 1]))
                return 0;
    return 1;
}

/* ---------- Constants ---------- */

/* The error for a const variable's value where C requires a constant
 * expression (an array size, a global's initializer). */
static const char not_ice[] =
    "a const variable's value isn't a constant expression in C";

/* Set by parse_scalar_init(): whether the initializer it parsed was an
 * integer constant (not an address), and its value. A const variable
 * initialized so has a known value (set_known()). */
static int init_known, init_val;

/* Left operands held back for folding; see the comment at the top. */
static int held[MAX_NEST + 1];
static int nheld;

/* Above 0 while parsing code that can never run, such as the right side
 * of 0 && x: it is checked, but put() emits nothing. */
static int suppress;

/* Above 0 while parsing the operand of sizeof (which suppress also
 * covers). */
static int in_sizeof;

/* Emit an IR op, after any held constants. All of the parser's IR goes
 * through here. */
static void put(int op, int a, Sym *s)
{
    int i;
    if (suppress)
        return;
    for (i = 0; i < nheld; i++)
        ir_put(IR_CONST, held[i], NULL);
    nheld = 0;
    ir_put(op, a, s);
}

/* The low 16 bits of u, as a signed 16-bit int. Unsigned arithmetic is
 * used so that this works whether the host's int has 16 or 32 bits. */
static int wrap16(unsigned u)
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
    if ((op == IR_DIV || op == IR_MOD) && v == 0)
        error("division by zero", NULL);
    if ((op == IR_SHL || op == IR_SHR) && (v < 0 || v > 15))
        error("shift count out of range (0 to 15):",
              v < 0 ? "negative" : "too big");
}

/* a op b, as the VM computes it: C's rules for a 16-bit int. / truncates
 * toward zero, % has the dividend's sign, >> is arithmetic, and
 * comparisons give 1 or 0. Cases check_right_const() rejects give 0
 * (they only get here in code that can never run). */
static int fold(int op, int a, int b)
{
    unsigned ua = (unsigned)a, ub = (unsigned)b, q;
    if (((op == IR_DIV || op == IR_MOD) && b == 0)
        || ((op == IR_SHL || op == IR_SHR) && (b < 0 || b > 15)))
        return 0;
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
    return op >= IR_EQ && op <= IR_GE;
}

/* ---------- Labels ---------- */

/* Labels are numbered per function. Stage 4 resolves them. */
static int nlabels;
static int label_uses[MAX_LABELS];  /* branches to each label so far */

/* Allocate a new label in the current function. */
static int new_label(void)
{
    if (nlabels >= MAX_LABELS)
        error("too many branches in", quote(cur_fn->name));
    label_uses[nlabels] = 0;
    return nlabels++;
}

/* Emit branch op (IR_JUMP, IR_BRFALSE, ...) to label n. */
static void branch(int op, int n)
{
    if (!suppress)
        label_uses[n]++;
    put(op, n, NULL);
}

/* Place label n, which backward branches will use (a loop's top). */
static void place_label(int n)
{
    put(IR_LABEL, n, NULL);
}

/* Place label n, whose branches all came before it. If there were none,
 * it is left out: then nothing can reach the code after it from here,
 * and the peephole stage can drop that code if it is unreachable. */
static void place_forward(int n)
{
    if (label_uses[n] > 0)
        place_label(n);
}

/* ---------- Expressions ---------- */

/*
 * An operand on its way through the expression parser. The last three
 * kinds are places (lvalues, or arrays): what to load or store isn't
 * decided until the parser sees what follows.
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
 * *f() = e, the parser emits e after the address, then exchanges the
 * two blocks of code while they are still in stage 2's buffer
 * (ir_rotate(), approach.md 4(d2)): "e addr SW", with no temporary.
 * Until it sees whether the assignment's value is used, the result is
 * K_STORE: the address, then the value, are the last code emitted, and
 * the rotation and the store wait. If the value is used, a DUP goes
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
    K_STORE         /* an assignment through an address, whose store
                     * waits (m0, m1, mend, stype, vmask) */
};

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
    int off;        /* K_VAR: its offset; K_ELEM: the base's offset */
    int global;     /* K_VAR, K_ELEM: off is a data offset (a global),
                     * not a frame offset */
    int val;        /* K_CONST: the value */
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

static void parse_expr(int min_prec, Operand *o);
static void parse_unary(Operand *o);
static void parse_postfix(Operand *o);
static void parse_sizeof(Operand *o);
static int parse_dims(int t, int may_omit);
static int parse_local_init(int type, const char *name, int base);
static int parse_global_init(int type, const char *name);
static void binary(Operand *o, Operand *r, int op, int lstart, int mid);
static void finish_store(Operand *o, int keep);
static void offset_place(Operand *o, int off);

/* Push the variable at off, a data offset if global, else a frame
 * offset: a word, or a byte if is_char. */
static void load_var(int off, int global, int is_char)
{
    if (global)
        put(is_char ? IR_GLOBALB : IR_GLOBAL, off, NULL);
    else
        put(is_char ? IR_LOCALB : IR_LOCAL, off, NULL);
}

/* Push the address of the variable at off (a global's or a local's). */
static void push_var_addr(int off, int global)
{
    put(global ? IR_DATAADDR : IR_LOCALADDR, off, NULL);
}

/* Store the top value into the variable o (a K_VAR), keeping the stored
 * value (for a char, its low byte) on the stack. */
static void store_var(Operand *o)
{
    int c = o->type == TY_CHAR;
    if (o->global)
        put(c ? IR_SETGLOBALB : IR_SETGLOBAL, o->off, NULL);
    else
        put(c ? IR_SETLOCALB : IR_SETLOCAL, o->off, NULL);
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
    return o->kind == K_VAR || o->kind == K_DEREF || o->kind == K_ELEM;
}

/* The type of a value of type t: an array's value is a pointer to its
 * first element. */
static int value_type(int t)
{
    return is_array(t) ? pointer_to(type_sub(t)) : t;
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
            put(IR_CONST, o->scale, NULL);
            put(IR_MUL, 0, NULL);   /* a word index becomes IDXW */
        }
        put(IR_ADD, 0, NULL);
    }
    if (d != 0) {
        put(IR_CONST, d, NULL);
        put(IR_ADD, 0, NULL);
    }
}

/* Make sure the operand's value is on the stack. An array's value is
 * the address of its first element (for K_DEREF, already there). */
static void rvalue(Operand *o)
{
    if (is_struct(o->type))
        error("a struct can't be used as a value here", NULL);
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
        error("void value used in an expression", NULL);
        break;
    case K_CONST:
        put(IR_CONST, o->val, NULL);
        o->is_bool = o->val == 0 || o->val == 1;
        break;
    case K_VAR:
        load_var(o->off, o->global, o->type == TY_CHAR);
        o->is_bool = 0;
        break;
    case K_ELEM:
        push_address(o);
        /* fall through */
    case K_DEREF:
        put(load_op(o->type), 0, NULL);
        o->is_bool = 0;
        break;
    case K_STORE:
        finish_store(o, 1);
        break;
    }
    o->kind = K_VALUE;
}

/* Allocate a temporary frame word. Temporaries are freed at the end of
 * each full expression (see end_expr()). */
static int new_temp(void)
{
    int off = frame_next;
    if (frame_next + 2 > 255)
        error("too many local variables in", quote(cur_fn->name));
    frame_next += 2;
    if (frame_next > frame_max)
        frame_max = frame_next;
    return off;
}

/* Frame offset where the current full expression's temporaries start. */
static int temp_mark;

/* Start a full expression (one not inside another, such as an
 * expression statement or a condition). */
static void begin_expr(void)
{
    temp_mark = frame_next;
}

/* End a full expression: its code can't be reordered any more, so stage
 * 2 can lower it, and its temporaries' frame space is free again. */
static void end_expr(void)
{
    ir_flush();
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
    put(IR_SETLOCAL, off, NULL);
    put(IR_DROP, 0, NULL);
    make_elem(o, off, 0, 0);
}

/* Where the code of an operand starts, given the IR position and the
 * number of held constants when its parsing began. The first op it put
 * wrote those constants (which belong to the code before it) ahead of
 * its own, so its code starts after them, if it emitted any. */
static int code_start(int mark, int h)
{
    return ir_here() > mark ? mark + h : mark;
}

/* Emit the store that the K_STORE operand o waits for: the value's code
 * moves ahead of the address's, giving "e addr SW". If keep is set, the
 * value (the stored value: its low byte, for a char) stays on the
 * stack. If the buffer overflowed and the address's code is gone, swap
 * after the fact with two temporaries instead. */
static void finish_store(Operand *o, int keep)
{
    int t1 = -1, t2;
    if (ir_here() != o->mend || nheld != 0)
        fatal("internal error: code after a pending store", NULL);
    if (ir_holds(o->m0)) {
        if (keep)
            put(IR_DUP, 0, NULL);   /* it ends up below the address */
        ir_rotate(o->m0, o->m1);
    } else {
        t1 = new_temp();
        t2 = new_temp();
        put(IR_SETLOCAL, t1, NULL);
        put(IR_DROP, 0, NULL);
        put(IR_SETLOCAL, t2, NULL);
        put(IR_DROP, 0, NULL);
        put(IR_LOCAL, t1, NULL);
        put(IR_LOCAL, t2, NULL);
    }
    put(store_op(o->stype), 0, NULL);
    if (keep && t1 >= 0)
        put(IR_LOCAL, t1, NULL);
    if (keep && o->vmask) {
        put(IR_CONST, 0xFF, NULL);
        put(IR_AND, 0, NULL);
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
 * array can't be, nor a struct with a const member); what says how it
 * was used. */
static void need_lvalue(Operand *o, const char *what)
{
    if (is_array(o->type) && is_place(o))
        error("an array can't be the", what);
    if (!is_place(o) || o->assigned)
        error("lvalue required as", what);
    if (o->quals & Q_CONST)
        error("a 'const' object can't be the", what);
    if (holds_const(o->type))
        error("a struct with a 'const' member can't be the", what);
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
    put(store_op(o->type), 0, NULL);
    o->assigned = 1;
}

/* The number of words a struct of type t takes on the stack. */
static int struct_words(int t)
{
    return (type_size(t) + 1) / 2;
}

/* Discard the value of an expression used as a statement. A pending
 * variable or constant ("x;") emits nothing, nor does the result of an
 * assignment through a pointer. A load through a pointer is still
 * done, since it may read I/O (such as the Apple II's $C010). A struct
 * isn't loaded (*p; evaluates p), and one on the stack is dropped. */
static void discard(Operand *o)
{
    int k;
    if (is_struct(o->type) && is_place(o)) {
        if (o->kind == K_DEREF)
            put(IR_DROP, 0, NULL);  /* its address */
        o->kind = K_VOID;
        return;
    }
    if (is_struct(o->type) && o->kind == K_VALUE) {
        for (k = 0; k < struct_words(o->type); k++)
            put(IR_DROP, 0, NULL);  /* a call's result */
        o->kind = K_VOID;
        return;
    }
    if (o->kind == K_STORE)
        finish_store(o, 0);
    if (o->kind == K_DEREF || (o->kind == K_ELEM && !o->assigned))
        rvalue(o);
    if (o->kind == K_VALUE)
        put(IR_DROP, 0, NULL);
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
              ? "pointer arithmetic on a pointer to an incomplete struct"
              : "pointer arithmetic on a 'void *'", NULL);
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
 * where says where, for the message. */
static void check_assign(int to, Operand *o, const char *where)
{
    int from = value_type(o->type), lost;
    if (o->kind == K_VOID)
        error("void value used in an expression", NULL);
    if (is_struct(to) || is_struct(from)) {
        if (unqual(to) != unqual(from))
            error("incompatible types in", where);
        return;
    }
    if (is_pointer(to)) {
        if (!is_pointer(from)) {
            if (!is_null(o))
                error("pointer from integer without a cast in", where);
        } else if (!compatible(from, to)) {
            error("incompatible pointer types in", where);
        } else {
            lost = type_sub(from) & ~type_sub(to) & Q_ALL;
            if (lost & Q_CONST)
                warning("'const' qualifier discarded from pointer target"
                        " type in", where);
            if (lost & Q_VOLATILE)
                warning("'volatile' qualifier discarded from pointer target"
                        " type in", where);
        }
    } else if (is_pointer(from)) {
        error("integer from pointer without a cast in", where);
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
        put(IR_CONST, 0, NULL);
        put(IR_NE, 0, NULL);
        o->is_bool = 1;
    }
    o->type = TY_INT;
}

/* Convert o to char, as C does for a cast or a char function's return
 * value: keep the low byte. A constant stays pending; anything else
 * ends up on the stack. A char, or a 1 or 0, needs no code. */
static void to_char(Operand *o)
{
    if (o->kind == K_CONST) {
        o->val &= 0xFF;
    } else {
        rvalue(o);
        if (o->type != TY_CHAR && !o->is_bool) {
            put(IR_CONST, 0xFF, NULL);
            put(IR_AND, 0, NULL);
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
        error("a cast to a struct type isn't allowed", NULL);
    if (type == TY_VOID) {
        discard(o);
        o->kind = K_VOID;
    } else if (type == TY_CHAR) {
        to_char(o);
    } else if (o->kind != K_CONST) {
        rvalue(o);
    }
    o->type = type;
}

/* Parse an expression whose code can never run (such as the x in
 * 0 && x): check it, but emit nothing. */
static void parse_skipped(int min_prec, Operand *o)
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
static void push_struct_words(Operand *o)
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
        put(IR_SETLOCAL, t + 2 * k, NULL);
        put(IR_DROP, 0, NULL);
    }
    o->kind = K_VAR;
    o->off = t;
    o->global = 0;
    o->assigned = 1;
}

/* Push zero for a function of return type t that ends without a value:
 * as many words as it returns, so that the stack stays balanced. */
static void push_zero_result(int t)
{
    int k, w = is_struct(t) ? struct_words(t) : 1;
    for (k = 0; k < w; k++)
        put(IR_CONST, 0, NULL);
}

/* Report an error if t is a struct too big to pass or return by value
 * (MAX_BYVAL bytes), or an incomplete one; what is "passed" or
 * "returned". */
static void check_byval(int t, const char *what)
{
    char msg[64];
    if (!is_struct(t))
        return;
    need_complete(t, *what == 'p' ? "a parameter" : "a return value");
    if (type_size(t) > MAX_BYVAL) {
        sprintf(msg, "a struct over %d bytes can't be %s by value:",
                MAX_BYVAL, what);
        error(msg, *what == 'p' ? "use a pointer to it"
                               : "return it through a pointer parameter");
    }
}

/* Calls emitted so far. A compound assignment compares it before and
 * after its right side, to see whether that has a call. */
static int ncalls;

/* Parse a call to fn, starting at '(': emit the arguments, check their
 * count (and their types, if fn's parameter types are known), then
 * emit the call. */
static void parse_call(Sym *fn, Operand *o)
{
    char where[MAX_NAME + 32];
    Operand a;
    int nargs = 0;
    next();                         /* '(' */
    if (tok != ')') {
        for (;;) {
            parse_expr(PREC_ASSIGN, &a);
            if (fn->ptypes >= 0 && nargs < fn->nparams) {
                sprintf(where, "argument %d of %s", nargs + 1,
                        quote(fn->name));
                check_assign(param_types[fn->ptypes + nargs], &a, where);
            }
            if (is_struct(a.type)) {
                if (fn->ptypes < 0)
                    error("passing a struct needs a prototype for",
                          quote(fn->name));
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
    expect(')', "expected ')' after arguments, not");
    if (fn->nparams < 0) {
        fn->nparams = nargs;        /* first use fixes the count */
    } else if (fn->nparams != nargs) {
        error("wrong number of arguments in call to", quote(fn->name));
    }
    fn->called = 1;
    if (!suppress)
        ncalls++;
    put(IR_CALL, nargs, fn);
    o->kind = fn->type != TY_VOID ? K_VALUE : K_VOID;
    o->type = fn->type;         /* a struct: its words are on the stack */
}

/* pstr("literal"): the address of the literal's PLASMA (length-prefixed)
 * form, which is the byte just before its first character. */
static void parse_pstr(void)
{
    int off;
    next();                         /* '(' */
    if (tok != T_STR)
        error("pstr() needs a string literal, not", tok_desc());
    if (tok_len > 255)
        error("string too long for pstr() (over 255 characters)", NULL);
    off = suppress ? 1 : data_string(tok_text, tok_len);
    next();
    expect(')', "expected ')' after pstr argument, not");
    put(IR_STRADDR, off - 1, NULL);
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
 * at compile time (Sym.has_val), a K_CONST, so const int N = 10; makes
 * x < N use CB 10. Not when it is a place: assigned to (an error, made
 * by need_lvalue()), incremented, or the operand of &, so &N works. */
static void parse_primary(Operand *o)
{
    char name[MAX_NAME + 1];
    Sym *s;
    int c, place = want_place;

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
        /* A literal in code that can never run takes no data space. */
        put(IR_STRADDR, suppress ? 0 : data_string(tok_text, tok_len),
            NULL);
        o->type = pointer_to(TY_CHAR);
        next();
        return;
    case '(':
        enter_nest();
        next();
        if (is_type_start(tok)) {
            /* A cast: (type) unary-expression. */
            c = parse_pointers(parse_base_type());
            expect(')', "expected ')' after the type in a cast, not");
            parse_unary(o);
            cast(o, c);
            nest--;
            return;
        }
        /* The result may still be a pending variable: (x) = 1 is valid. */
        parse_expr(PREC_COMMA, o);
        expect(')', "expected ')', not");
        nest--;
        return;
    case T_ID:
        strcpy(name, tok_text);
        next();
        if (tok == '(') {
            if (!strcmp(name, "pstr")) {
                parse_pstr();
                o->type = pointer_to(TY_CHAR);
                return;
            }
            if (find_var(name))
                error("called object is not a function:", quote(name));
            s = find_func(name);
            if (!s) {
                s = add_func(name);
                c = find_cmdsys(name);
                if (c >= 0)
                    declare_cmdsys(s, c);   /* by the CMDSYS table */
                /* Otherwise an implicit declaration: returns int, and
                 * this call fixes the parameter count. */
            }
            parse_call(s, o);
            return;
        }
        s = find_var(name);
        if (s && s->has_val && !place && !assign_op(&c)
            && tok != T_INC && tok != T_DEC) {
            o->kind = K_CONST;      /* a const variable's known value */
            o->type = unqual(s->type);
            o->val = s->val;
            o->nonice = 1;
            return;
        }
        if (s) {
            o->kind = K_VAR;        /* load it later, if needed */
            o->type = unqual(s->type);
            o->quals = s->type & Q_ALL;
            o->off = s->offset;
            o->global = s->kind == S_GLOBAL;
            return;
        }
        if (find_func(name))
            error("function pointers are not supported yet:", quote(name));
        error("undeclared identifier", quote(name));
        return;
    case T_KEYWORD:
        not_yet();
        return;
    }
    error("expected an expression, not", tok_desc());
}

/* Check that lvalue o can be the operand of ++ or --, and get it ready:
 * push its value, leaving o itself ready to be stored into. Returns
 * the amount to add: the element size for a pointer, else 1. */
static int start_incdec(Operand *o)
{
    Operand v;
    need_lvalue(o, "operand of ++ or --");
    spill(o);
    v = *o;
    rvalue(&v);
    return is_pointer(o->type) ? elem_size(o->type) : 1;
}

/* ++x / --x: x = x + 1, and the value is the new x. */
static void pre_incdec(Operand *o, int ir_op)
{
    put(IR_CONST, start_incdec(o), NULL);
    put(ir_op, 0, NULL);
    assign_to(o);
    o->is_bool = 0;
}

/* x++ / x--: the value is the old x. Keep a copy of it below the
 * update; when the value is unused, the peephole stage removes the
 * copy. */
static void post_incdec(Operand *o, int ir_op)
{
    int step = start_incdec(o);
    put(IR_DUP, 0, NULL);
    put(IR_CONST, step, NULL);
    put(ir_op, 0, NULL);
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
        error("void value used in an expression", NULL);
    if (is_array(t) && is_place(o)) {
        /* *a is a[0]: the same address, with the element's type. */
        set_object(o, type_sub(t));
        return;
    }
    if (!is_pointer(t))
        error("unary '*' needs a pointer operand", NULL);
    if (is_void_ptr(t))
        error("dereferencing a 'void *'", NULL);
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
    int t = o->type, simple, size, d;

    next();                         /* '[' */
    simple = (tok == T_NUM || tok == T_ID) && pending_base(o, tok == T_ID)
             && next_is_rbracket();
    if (!simple)
        rvalue(o);
    parse_expr(PREC_COMMA, &r);
    expect(']', "expected ']' after an array index, not");
    if (!simple) {
        if (!is_pointer(o->type) && !is_pointer(value_type(r.type)))
            error("subscripted value is neither array nor pointer", NULL);
        if (is_pointer(o->type) && is_pointer(value_type(r.type)))
            error("array subscript is not an integer", NULL);
        binary(o, &r, IR_ADD, -1, -1);
        deref(o);
        return;
    }
    if (is_pointer(value_type(r.type)))
        error("array subscript is not an integer", NULL);
    if (r.kind != K_CONST && r.kind != K_VAR)
        fatal("internal error: unexpected array index", NULL);
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
        error("lvalue required as", "unary '&' operand");
    if (o->kind == K_VAR)
        push_var_addr(o->off, o->global);
    else if (o->kind == K_ELEM)
        push_address(o);                /* &*p is p, &a[i] is a + i */
    else if (o->kind != K_DEREF)        /* K_DEREF: already on the stack */
        error("lvalue required as", "unary '&' operand");
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
        put(IR_CONST, off, NULL);
        put(IR_ADD, 0, NULL);
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
    char msg[MAX_NAME + 40];
    int t = arrow ? value_type(o->type) : o->type, tag, m, off;

    next();                         /* '.' or '->' */
    if (arrow && !(is_pointer(t) && is_struct(type_sub(t))))
        error("'->' needs a pointer to a struct", NULL);
    if (!arrow && !is_struct(t))
        error("'.' needs a struct", NULL);
    if (!arrow && o->kind == K_VALUE)
        materialize(o);             /* f().x */
    tag = struct_tag(arrow ? type_sub(t) : t);
    if (tok != T_ID)
        error("expected a member name, not", tok_desc());
    if (tag_size(tag) < 0)
        error("member access into the incomplete type", tag_desc(tag));
    m = member_find(tag, name_find(tok_text));
    if (m < 0) {
        sprintf(msg, "%s has no member named", tag_desc(tag));
        error(msg, quote(tok_text));
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
            error("pointer operand to unary", t == '-' ? "'-'"
                  : t == '+' ? "'+'" : "'~'");
        if (o->kind == K_CONST) {
            if (t == '-')
                o->val = wrap16(0u - (unsigned)o->val);
            else if (t == '~')
                o->val = wrap16(~(unsigned)o->val);
            else if (t == '!')
                o->val = !o->val;
            o->type = TY_INT;
            return;
        }
        rvalue(o);
        o->type = TY_INT;           /* a char operand is promoted */
        if (t == '-')
            put(IR_NEG, 0, NULL);
        else if (t == '~')
            put(IR_COMP, 0, NULL);
        else if (t == '!')
            put(IR_NOT, 0, NULL);
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
        if (is_type_start(tok)) {
            /* A type name: a base type, '*'s and array sizes. */
            t = parse_dims(parse_pointers(parse_base_type()), 0);
            expect(')', "expected ')' after the type in sizeof, not");
        } else {
            /* A parenthesized expression, which may be followed by
             * postfix operators: sizeof (a)[0] is the size of (a)[0]. */
            parse_expr(PREC_COMMA, o);
            expect(')', "expected ')', not");
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
        error("sizeof can't be applied to type void", NULL);
    if (is_struct(t) && type_size(t) == 0)
        error("sizeof can't be applied to the incomplete type",
              tag_desc(struct_tag(t)));
    o->kind = K_CONST;
    o->val = type_size(t);
    o->type = TY_INT;
    o->nonice = 0;              /* sizeof N is a constant expression */
    o->is_bool = 0;
}

/*
 * Parse the right side of && or || (the current token), with *o as the
 * left side, which binds at precedence prec. The result is C's 1 or 0.
 * Code: "a BRAND L b' L:" for &&, where b' is b as 1 or 0. If a is 0,
 * BRAND leaves it as the result. For ||, a must be made 1 or 0 too,
 * since BROR leaves it when it is nonzero. A constant left side folds.
 */
static void parse_logical(Operand *o, int prec)
{
    int is_and = tok == T_ANDAND, end;
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
        /* 1 && x and 0 || x are x, as 1 or 0. */
        end = o->nonice;
        parse_expr(prec + 1, &r);
        to_bool(&r);
        *o = r;
        o->nonice |= end;
        return;
    }
    if (is_and)
        rvalue(o);
    else
        to_bool(o);
    end = new_label();
    branch(is_and ? IR_BRAND : IR_BROR, end);
    parse_expr(prec + 1, &r);
    to_bool(&r);
    rvalue(&r);
    place_forward(end);
    o->kind = K_VALUE;
    o->type = TY_INT;
    o->is_bool = 1;
}

/* Is o a void call (no value)? */
static int is_void(Operand *o)
{
    return o->kind == K_VOID;
}

/* The type of x ? b : c, given whether b and c are null pointer
 * constants. Integers give their common type, or int if they differ
 * (char and int: C promotes both). A pointer may meet the same type,
 * a void * (giving void *), or a null pointer constant. Two pointers
 * give a pointer to a type with the qualifiers of both targets: char *
 * and const char * give const char *. */
static int cond_type(Operand *b, int b_null, Operand *c, int c_null)
{
    int bt = value_type(b->type), ct = value_type(c->type), q;
    if (!is_pointer(bt) && !is_pointer(ct))
        return bt == ct ? bt : TY_INT;
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
    error("type mismatch in conditional expression", NULL);
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
        expect(':', "expected ':' in conditional expression, not");
        if (take_b)
            parse_skipped(PREC_COND, &c);
        else
            parse_expr(PREC_COND, &c);
        if (is_void(&b) != is_void(&c))
            error("only one side of '?:' is void", NULL);
        type = cond_type(&b, is_null(&b), &c, is_null(&c));
        b_null = o->nonice;
        *o = take_b ? b : c;
        if (is_place(o))
            rvalue(o);              /* the result is not an lvalue */
        o->type = type;
        o->nonice |= b_null;        /* N ? 1 : 2 is no more constant */
        return;
    }
    rvalue(o);
    else_l = new_label();
    end_l = new_label();
    branch(IR_BRFALSE, else_l);
    parse_expr(PREC_COMMA, &b);
    b_null = is_null(&b);
    if (!is_void(&b))
        rvalue(&b);
    branch(IR_JUMP, end_l);
    expect(':', "expected ':' in conditional expression, not");
    place_forward(else_l);
    parse_expr(PREC_COND, &c);
    type = cond_type(&b, b_null, &c, is_null(&c));
    if (!is_void(&c))
        rvalue(&c);
    if (is_void(&b) != is_void(&c))
        error("only one side of '?:' is void", NULL);
    place_forward(end_l);
    o->kind = b.kind;
    o->type = type;
    o->is_bool = b.is_bool && c.is_bool;
}

/* Is IR op one of < > <= >=? */
static int is_relational(int op)
{
    return op == IR_LT || op == IR_GT || op == IR_LE || op == IR_GE;
}

/* The C spelling of arithmetic IR op, for messages. */
static const char *op_name(int op)
{
    static const char *const names[] = {
        "'+'", "'-'", "'*'", "'/'", "'%'", "'&'", "'|'", "'^'", "'<<'",
        "'>>'"
    };
    return op >= IR_ADD && op <= IR_SHR ? names[op - IR_ADD] : "operator";
}

/* Flip the top value's sign bit (XOR $8000). Pointers are compared
 * unsigned, as signed values with both sign bits flipped, since PLASMA
 * only has signed comparisons (approach.md, "M4 design"). */
static void flip_sign(void)
{
    put(IR_CONST, -32768, NULL);
    put(IR_XOR, 0, NULL);
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
        put(IR_CONST, n, NULL);
        put(IR_MUL, 0, NULL);
    }
}

/* A comparison with a pointer operand: check the types (equality allows
 * a null pointer constant or a void *; < > <= >= need the same type),
 * then push r. For < > <= >=, the left side's sign was already flipped,
 * so flip r's too. */
static void compare_pointers(Operand *o, Operand *r, int op)
{
    int lt = o->type, rt = value_type(r->type);
    if (!is_pointer(lt) || !is_pointer(rt)) {
        if (is_relational(op) || !(is_null(o) || is_null(r)))
            error("comparison between pointer and integer", NULL);
    } else if (is_relational(op) ? !same_target(lt, rt)
                                 : !compatible(lt, rt)) {
        error("comparison of incompatible pointer types", NULL);
    }
    if (is_relational(op) && r->kind == K_CONST) {
        r->val = wrap16((unsigned)r->val ^ 0x8000u);
        rvalue(r);
    } else {
        rvalue(r);
        if (is_relational(op))
            flip_sign();
    }
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
    int lt = o->type, rt = value_type(r->type), n, t;
    int result = TY_INT;

    if (r->kind == K_VOID)
        rvalue(r);                  /* an error */
    if (is_compare(op) && (is_pointer(lt) || is_pointer(rt))) {
        compare_pointers(o, r, op);
    } else if (!is_pointer(lt) && !is_pointer(rt)) {
        rvalue(r);
    } else if (is_pointer(lt) && !is_pointer(rt)
               && (op == IR_ADD || op == IR_SUB)) {
        push_scaled(r, lt);         /* p + i, p - i */
        result = lt;
    } else if (!is_pointer(lt) && op == IR_ADD) {
        /* i + p: i is already below p. If i's code can still move, it
         * goes after p's and is scaled there ("p i IDXW"). Otherwise it
         * is scaled with p saved in a temporary (approach.md 4(b)). */
        n = elem_size(rt);
        rvalue(r);
        if (n != 1 && lstart >= 0 && ir_holds(lstart)) {
            ir_rotate(lstart, mid);
            put(IR_CONST, n, NULL);
            put(IR_MUL, 0, NULL);
        } else if (n != 1) {
            t = new_temp();
            put(IR_SETLOCAL, t, NULL);
            put(IR_DROP, 0, NULL);
            put(IR_CONST, n, NULL);
            put(IR_MUL, 0, NULL);
            put(IR_LOCAL, t, NULL);
        }
        result = rt;
    } else if (is_pointer(lt) && is_pointer(rt) && op == IR_SUB) {
        /* p - q: the difference in elements. */
        if (!same_target(lt, rt))
            error("incompatible pointer types in", "subtraction");
        n = elem_size(lt);
        rvalue(r);
        put(IR_SUB, 0, NULL);
        if (n == 2) {
            put(IR_CONST, 1, NULL);
            put(IR_SHR, 0, NULL);
        } else if (n != 1) {
            put(IR_CONST, n, NULL);
            put(IR_DIV, 0, NULL);
        }
        o->kind = K_VALUE;
        o->type = TY_INT;
        o->is_bool = 0;
        return;
    } else {
        error("invalid operands to binary", op_name(op));
    }
    put(op, 0, NULL);
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
    check_assign(o->type, &r, "assignment");
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

/* Compute x op e after e's code, with e's value (on the stack) below
 * x's: the operator applied with its operands reversed. ADD and the
 * other commutative ops need nothing more; x - e is -(e - x); the rest
 * need e saved in a temporary first. For a pointer x (+= and -= only),
 * e is scaled first. x becomes the result. */
static void reversed_op(Operand *x, int op)
{
    int t = -1;
    if (is_pointer(x->type) && elem_size(x->type) != 1) {
        put(IR_CONST, elem_size(x->type), NULL);
        put(IR_MUL, 0, NULL);
    }
    if (swapped_op(op) != op && op != IR_SUB) {
        t = new_temp();
        put(IR_SETLOCAL, t, NULL);
        put(IR_DROP, 0, NULL);
    }
    rvalue(x);
    if (t >= 0)
        put(IR_LOCAL, t, NULL);
    put(op, 0, NULL);
    if (t < 0 && op == IR_SUB)
        put(IR_NEG, 0, NULL);
    if (!is_pointer(x->type))
        x->type = TY_INT;           /* a char operand is promoted */
    x->kind = K_VALUE;
    x->is_bool = 0;
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
 *   - if e has a call, the load comes after it, so both addresses
 *     follow the call, and the operator is applied reversed
 *     (reversed_op());
 *   - otherwise e can't change p (except with undefined behavior), and
 *     the load is moved ahead of e's code, if it can still move.
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
    if (ncalls == calls && ir_holds(start)) {
        xm = ir_here();
        rvalue(x);
        ir_rotate(start, xm);       /* x's load goes before e */
        binary(x, r, op, -1, -1);
        return;
    }
    reversed_op(x, op);
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
static Sym *copy_func(void)
{
    Sym *s = find_func("memcpy");
    if (!s) {
        s = add_func("memcpy");
        declare_cmdsys(s, find_cmdsys("memcpy"));
    } else if (s->nparams != 3) {
        error("a struct copy calls memcpy(dst, src, n), but this program"
              " declares memcpy differently", NULL);
    }
    if (!suppress)
        s->called = 1;
    return s;
}

/*
 * Copy the struct r into the place o (approach.md, "M6 design"). r's
 * code starts at IR position start (if it emitted any). Between two
 * places whose addresses need no code yet (K_VAR, K_ELEM), the words
 * are moved one at a time (LLW b SLW a ...) when that is no bigger
 * than calling memcpy; otherwise memcpy(dst, src, n) is called, with
 * dst's address first. If r's address was already emitted and o's
 * wasn't, the two are exchanged in stage 2's buffer, as for a store
 * (finish_store()). A struct on the stack (a call's result) is stored
 * word by word, last first. o becomes the result: the object assigned
 * to, not an lvalue (for a K_DEREF, its address was kept with DUP).
 */
static void copy_struct(Operand *o, Operand *r, int start)
{
    Operand d, v;
    Sym *fn;
    int n = type_size(o->type), k, cost = 0, mid, t;

    if (r->kind == K_VALUE && o->kind != K_DEREF) {
        /* Its words are on the stack: store them, last word first. */
        for (k = struct_words(o->type) - 1; k >= 0; k--) {
            d = *o;
            offset_place(&d, 2 * k);
            d.type = n - 2 * k == 1 ? TY_CHAR : TY_INT;
            if (d.kind == K_VAR) {
                store_var(&d);
                put(IR_DROP, 0, NULL);
            } else {
                push_address(&d);
                put(store_op(d.type), 0, NULL);
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
                    put(IR_DROP, 0, NULL);
                } else {
                    push_address(&d);
                    put(store_op(d.type), 0, NULL);
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
    } else if (ir_holds(start)) {
        mid = ir_here();
        push_place_addr(o);
        ir_rotate(start, mid);
    } else {
        t = new_temp();
        put(IR_SETLOCAL, t, NULL);
        put(IR_DROP, 0, NULL);
        push_place_addr(o);
        put(IR_LOCAL, t, NULL);
    }
    put(IR_CONST, n, NULL);
    put(IR_CALL, 3, fn);
    if (fn->type != TY_VOID)
        put(IR_DROP, 0, NULL);
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
        put(IR_DUP, 0, NULL);       /* the address, kept for the result */
    mark = ir_here();
    h = nheld;
    parse_expr(PREC_ASSIGN, &r);
    check_assign(o->type, &r, "assignment");
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
    check_assign(o->type, &r, "assignment");
    rvalue(&r);
    assign_to(o);
    o->is_bool = ir_op < 0 && r.is_bool && o->kind == K_VALUE;
}

/*
 * Precedence climbing: parse an expression whose operators all have
 * precedence min_prec or higher. The result is left in *o, possibly
 * still pending (a bare variable).
 */
static void parse_expr(int min_prec, Operand *o)
{
    int ir_op, supported, prec, m0 = ir_here(), h0 = nheld, lstart, mid;
    Operand r;

    enter_nest();
    parse_unary(o);

    if (assign_op(&ir_op)) {
        /* Assignment is right-associative and binds loosest of all but
         * ','; it is only possible directly after a unary expression. */
        if (min_prec > PREC_ASSIGN)
            error("lvalue required as", "left operand of assignment");
        need_lvalue(o, "left operand of assignment");
        next();
        parse_assign(o, ir_op, m0, h0);
    }

    for (;;) {
        int mark = -1;
        lstart = -1;
        if (assign_op(&ir_op))
            error("lvalue required as", "left operand of assignment");
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
        if (o->kind == K_CONST && !is_pointer(o->type)) {
            /* Hold it back: it may fold with the right operand. */
            if (nheld > MAX_NEST)
                fatal("nesting too deep", NULL);
            mark = nheld;
            held[nheld++] = o->val;
        } else {
            rvalue(o);
            if (is_pointer(o->type) && is_relational(ir_op))
                flip_sign();        /* see compare_pointers() */
            /* All the code since this call began is the left operand. */
            lstart = code_start(m0, h0);
        }
        mid = ir_here();
        next();
        parse_expr(prec + 1, &r);   /* all binary ops are left-assoc */
        if (r.kind == K_CONST && !suppress)
            check_right_const(ir_op, r.val);
        if (mark >= 0 && nheld > mark) {
            /* The right operand emitted nothing, so the left constant
             * is still held. */
            if (is_pointer(value_type(r.type))) {
                if (ir_op == IR_ADD && r.kind != K_VOID) {
                    /* c + p: emit p, then c scaled. */
                    Operand c = *o;
                    nheld--;
                    rvalue(&r);
                    push_scaled(&c, r.type);
                    put(IR_ADD, 0, NULL);
                    *o = r;
                    continue;
                }
            } else if (r.kind == K_CONST) {
                nheld--;
                o->val = fold(ir_op, o->val, r.val);
                o->type = TY_INT;
                o->nonice |= r.nonice;
                continue;
            } else if (r.kind == K_VAR && swapped_op(ir_op) >= 0) {
                /* c op x: emit x c op' (op' is op with its operands
                 * swapped), so that the peephole stage sees the
                 * constant right before the op (1+x: INCR). */
                nheld--;
                rvalue(&r);
                put(IR_CONST, o->val, NULL);
                put(swapped_op(ir_op), 0, NULL);
                o->kind = K_VALUE;
                o->type = TY_INT;
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

/* ---------- Statements ---------- */

static void parse_block(int new_scope);

/* Parse any array sizes ("[3][4]") after a declarator's name, and
 * return the declared type, given the type t before them: int m[3][4]
 * is an array of 3 arrays of 4 ints. If may_omit is set (an array
 * parameter), the first size may be left out, giving a count of 0. */
static int parse_dims(int t, int may_omit)
{
    Operand e;
    int n = 0;
    if (tok != '[')
        return t;
    enter_nest();
    next();
    if (tok == ']') {
        if (!may_omit)
            error("array size missing", NULL);
    } else {
        parse_expr(PREC_COND, &e);
        if (e.kind != K_CONST || is_pointer(e.type))
            error("array size must be a constant", NULL);
        if (e.nonice)
            error(not_ice, "(an array size must be one)");
        if (e.val <= 0)
            error("array size must be positive", NULL);
        n = e.val;
    }
    expect(']', "expected ']' after an array size, not");
    t = parse_dims(t, 0);
    need_complete(t, "an array element");
    if (type_size(t) == 0)
        error("array elements can't have type void", NULL);
    nest--;
    return array_of(t, n);
}

/* Parse the start of a declarator after the base type base: any '*'s,
 * then the name, which is copied to name. Returns the type so far; any
 * array sizes come after the name. */
static int parse_declarator(int base, char *name)
{
    int type = parse_pointers(base);
    if (tok == '(')
        error("declarators in parentheses are not supported yet",
              "(such as int (*p)[4])");
    if (tok != T_ID)
        error("expected a name, not", tok_desc());
    strcpy(name, tok_text);
    next();
    return type;
}

/* Parse the rest of a variable's declarator after its name (any array
 * sizes) and return its full type, which can't be void or an
 * incomplete struct. If may_omit is set, the first array size may be
 * left out (count 0), for an initializer to give. */
static int parse_var_type(int type, int may_omit)
{
    if (unqual(type) == TY_VOID)
        error("a variable can't have type void", NULL);
    type = parse_dims(type, may_omit);
    need_complete(type, "a variable");
    return type;
}

/* Parse a struct's member list, from '{' to '}', and complete tag's
 * struct: each member is at the next byte, with no padding, since the
 * 6502 has no alignment rules (approach.md, "M6 design"). While the
 * list is parsed, the tag's size is -2, so a nested redefinition of
 * the same tag is caught. */
static void parse_members(int tag)
{
    char name[MAX_NAME + 1];
    int off = 0, base, t;

    enter_nest();
    tag_set_size(tag, -2);
    next();                         /* '{' */
    if (tok == '}')
        error("a struct needs at least one member", NULL);
    while (tok != '}') {
        if (tok == T_KEYWORD)
            not_yet();
        if (!is_type_start(tok))
            error("expected a struct member, not", tok_desc());
        base = parse_base_type();
        for (;;) {
            t = parse_declarator(base, name);
            if (tok == '(')
                error("a struct member can't be a function:", quote(name));
            t = parse_dims(t, 0);
            if (unqual(t) == TY_VOID)
                error("a struct member can't have type void", NULL);
            need_complete(t, "a struct member");
            if (member_find(tag, name_find(name)) >= 0)
                error("duplicate struct member", quote(name));
            if (off > MAX_OBJECT - type_size(t))
                error("struct too large (over 32767 bytes)", NULL);
            member_add(tag, intern(name), t, off);
            off += type_size(t);
            if (tok != ',')
                break;
            next();
        }
        expect(';', "expected ';' after a struct member, not");
    }
    next();
    tag_set_size(tag, off);
    nest--;
}

/*
 * Parse a struct type after "struct", and return it: a tag, a member
 * list, or both. As in C:
 *   - struct s { ... } defines s in the current block (or at file
 *     scope), completing an earlier "struct s;" there;
 *   - struct s; alone declares a new s in the current block, hiding any
 *     outer one;
 *   - struct s elsewhere refers to the innermost visible s, or declares
 *     a new, incomplete one, so that struct node *next; works before
 *     struct node is complete.
 * A tag first seen in a parameter list is visible only inside that
 * function, which is rarely what was meant; gcc warns, and so do we.
 */
static int parse_struct(void)
{
    int name = -1, tag = -1;

    next();                         /* struct */
    if (tok == T_ID) {
        name = intern(tok_text);
        next();
        tag = tag_find(name, tok == '{' || tok == ';' ? depth : -1);
    } else if (tok != '{') {
        error("expected a struct name or '{' after 'struct', not",
              tok_desc());
    }
    if (tag < 0) {
        tag = tag_new(name, depth);
        if (in_params && name >= 0)
            warning("declared in a parameter list, so not visible outside"
                    " it:", tag_desc(tag));
    }
    if (tok == '{') {
        if (tag_size(tag) != -1)
            error("redefinition of", tag_desc(tag));
        parse_members(tag);
    }
    return struct_type(tag);
}

/* Initialize the local struct s from a struct expression (after the
 * '='), as C allows for a local: struct pt b = a; It is a struct
 * copy into s. */
static void init_struct_copy(Sym *s)
{
    Operand v, e;
    int mark, h;
    begin_expr();
    v.kind = K_VAR;
    v.type = unqual(s->type);
    v.quals = 0;
    v.off = s->offset;
    v.global = 0;
    v.assigned = 0;
    v.is_bool = 0;
    mark = ir_here();
    h = nheld;
    parse_expr(PREC_ASSIGN, &e);
    check_assign(v.type, &e, "initialization");
    copy_struct(&v, &e, code_start(mark, h));
    end_expr();
}

/* Record that the variable s has the known value v, if reading it may
 * give v at compile time: it is a const scalar (an int, char or
 * pointer, not an array or struct) and not volatile. A char keeps the
 * low byte, as stored. */
static void set_known(Sym *s, int v)
{
    if ((s->type & Q_ALL) != Q_CONST || is_array(s->type)
        || is_struct(s->type))
        return;
    s->has_val = 1;
    s->val = unqual(s->type) == TY_CHAR ? v & 0xFF : v;
}

/* A declaration: a base type, then declarators, each a name with any
 * '*'s before it, any array sizes after it, and an optional
 * initializer: int a = 1, *p = &a, v[10]; A struct type may have no
 * declarators (struct pt { int x, y; };). If is_static is set (after
 * "static"), the variables are static locals: in the data segment, with
 * constant initializers applied once, when the module loads. */
static void parse_declaration(int is_static)
{
    int base = parse_base_type();
    if (tok == ';' && is_struct(base)) {
        next();                     /* struct pt { ... }; declares only */
        return;
    }
    for (;;) {
        Operand v, e;
        Sym *s;
        char name[MAX_NAME + 1];
        int type = parse_var_type(parse_declarator(base, name), 1), braced;
        if (tok == '(')
            error("local function declarations are not supported", NULL);
        if (is_array(type) && type_count(type) == 0 && tok != '=')
            error("array size missing", NULL);
        if (is_static) {
            /* In C, the name is in scope in its own initializer. */
            s = add_static_local(name, type);
            if (tok == '=') {
                next();
                s->type = parse_global_init(type, name);
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
            error("local array doesn't fit in the 255-byte frame"
                  " (make it static or global):", quote(name));
        if (is_struct(type) && frame_next + type_size(type) > 255)
            error("local struct doesn't fit in the 255-byte frame"
                  " (make it static or global):", quote(name));
        /* In C, the name is in scope in its own initializer. (An array
         * whose size comes from its initializer grows as it is parsed:
         * see reserve().) */
        s = add_local(name, type, type_size(type));
        if (tok == '=' && (is_array(type) || is_struct(type))) {
            next();
            if (is_struct(type) && tok != '{')
                init_struct_copy(s);    /* struct pt b = a; */
            else
                s->type = parse_local_init(type, name, s->offset);
        } else if (tok == '=') {
            next();
            braced = tok == '{';        /* int x = {5}; is valid C */
            if (braced)
                next();
            begin_expr();
            parse_expr(PREC_ASSIGN, &e);
            check_assign(type, &e, "initialization");
            if (e.kind == K_CONST)
                set_known(s, e.val);
            rvalue(&e);
            v.kind = K_VAR;
            v.type = unqual(type);
            v.off = s->offset;
            v.global = 0;
            store_var(&v);
            put(IR_DROP, 0, NULL);
            end_expr();
            if (braced) {
                if (tok == ',')
                    next();
                expect('}', "expected '}' after the initializer, not");
            }
        }
        if (tok != ',')
            break;
        next();
    }
    expect(';', "expected ';' after declaration, not");
}

/* Parse a full expression used as a statement (or a for clause), and
 * discard its value. */
static void parse_expr_statement(void)
{
    Operand o;
    begin_expr();
    parse_expr(PREC_COMMA, &o);
    discard(&o);
    end_expr();
}

/* Branch to label n if the condition *o is false (or, if on_true, if it
 * is true). A constant condition gives a jump or nothing. */
static void cond_branch(Operand *o, int n, int on_true)
{
    if (o->kind == K_CONST) {
        if ((o->val != 0) == on_true)
            branch(IR_JUMP, n);
        return;
    }
    rvalue(o);
    branch(on_true ? IR_BRTRUE : IR_BRFALSE, n);
}

/* Parse a full expression as a condition, and branch to label n as
 * cond_branch does. */
static void parse_cond(int n, int on_true)
{
    Operand o;
    begin_expr();
    parse_expr(PREC_COMMA, &o);
    cond_branch(&o, n, on_true);
    end_expr();
}

/* Parse "( condition )" and branch to label n as cond_branch does. */
static void parse_paren_cond(int n, int on_true)
{
    expect('(', "expected '(' before condition, not");
    parse_cond(n, on_true);
    expect(')', "expected ')' after condition, not");
}

/* The innermost loops' labels, for break and continue. */
static int brk_label[MAX_NEST], cont_label[MAX_NEST];
static int nloops;

static void parse_statement(void);

/* Parse a loop body, with break going to label brk and continue to
 * label cont. */
static void loop_body(int brk, int cont)
{
    if (nloops >= MAX_NEST)
        fatal("loops nested too deeply", NULL);
    brk_label[nloops] = brk;
    cont_label[nloops] = cont;
    nloops++;
    parse_statement();
    nloops--;
}

/* if (c) s1 [else s2]:  c BRFLS Lelse  s1  [BRNCH Lend]  Lelse: [s2  Lend:] */
static void parse_if(void)
{
    int else_l = new_label(), end_l;
    next();                         /* "if" */
    parse_paren_cond(else_l, 0);
    parse_statement();
    if (tok == T_ELSE) {
        next();
        end_l = new_label();
        branch(IR_JUMP, end_l);
        place_forward(else_l);
        parse_statement();
        place_forward(end_l);
    } else {
        place_forward(else_l);
    }
}

/* while (c) s:  Ltop: c BRFLS Lbrk  s  BRNCH Ltop  Lbrk: */
static void parse_while(void)
{
    int top = new_label(), brk = new_label();
    next();                         /* "while" */
    place_label(top);
    parse_paren_cond(brk, 0);
    loop_body(brk, top);
    branch(IR_JUMP, top);
    place_forward(brk);
}

/* do s while (c);  Ltop: s  Lcont: c BRTRU Ltop  Lbrk: */
static void parse_do(void)
{
    int top = new_label(), cont = new_label(), brk = new_label();
    next();                         /* "do" */
    place_label(top);
    loop_body(brk, cont);
    expect(T_WHILE, "expected 'while' after the body of 'do', not");
    place_forward(cont);
    parse_paren_cond(top, 1);
    expect(';', "expected ';' after 'do ... while (...)', not");
    place_forward(brk);
}

/*
 * for (init; c; step) s. The step's code comes before the body's, but
 * runs after it, so it is jumped around (approach.md 17 plans a side
 * buffer to avoid these jumps):
 *   init  Ltop: c BRFLS Lbrk  BRNCH Lbody
 *   Lcont: step  BRNCH Ltop
 *   Lbody: s  BRNCH Lcont  Lbrk:
 * With no step, continue goes straight to Ltop. The init may declare
 * variables (as in C99); they are in scope only in the loop.
 */
static void parse_for(void)
{
    int saved_nlocals = nlocals, saved_next = frame_next;
    int top, brk, cont, body;

    next();                         /* "for" */
    expect('(', "expected '(' after 'for', not");
    depth++;
    if (tok == T_STATIC)
        error("a 'for' loop can't declare a static variable", NULL);
    if (is_type_start(tok)) {
        parse_declaration(0);       /* includes the ';' */
    } else {
        if (tok != ';')
            parse_expr_statement();
        expect(';', "expected ';' after 'for' initialization, not");
    }
    top = new_label();
    brk = new_label();
    place_label(top);
    if (tok != ';')
        parse_cond(brk, 0);
    expect(';', "expected ';' after 'for' condition, not");
    cont = top;
    if (tok != ')') {
        body = new_label();
        cont = new_label();
        branch(IR_JUMP, body);
        place_label(cont);
        parse_expr_statement();
        branch(IR_JUMP, top);
        place_forward(body);
    }
    expect(')', "expected ')' after 'for' clauses, not");
    loop_body(brk, cont);
    branch(IR_JUMP, cont);
    place_forward(brk);
    depth--;
    nlocals = saved_nlocals;
    frame_next = saved_next;
}

/* break; and continue;: jump to the innermost loop's labels. */
static void parse_break_continue(void)
{
    int is_break = tok == T_BREAK;
    next();
    if (nloops == 0)
        error(is_break ? "'break' is not inside a loop"
                       : "'continue' is not inside a loop", NULL);
    branch(IR_JUMP, is_break ? brk_label[nloops - 1]
                             : cont_label[nloops - 1]);
    expect(';', is_break ? "expected ';' after 'break', not"
                         : "expected ';' after 'continue', not");
}

/* Parse one statement (or declaration) and emit its code. */
static void parse_statement(void)
{
    Operand o;
    switch (tok) {
    case T_IF:
        parse_if();
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
    case T_BREAK:
    case T_CONTINUE:
        parse_break_continue();
        return;
    case T_ELSE:
        error("'else' without a matching 'if'", NULL);
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
            if (cur_fn->type != TY_VOID)
                push_zero_result(cur_fn->type);  /* C89 allows this */
        } else {
            if (cur_fn->type == TY_VOID)
                error("void function returns a value:", quote(cur_fn->name));
            begin_expr();
            parse_expr(PREC_COMMA, &o);
            check_assign(cur_fn->type, &o, "return");
            if (cur_fn->type == TY_CHAR)
                to_char(&o);        /* a char function returns 0 to 255 */
            if (is_struct(cur_fn->type))
                push_struct_words(&o);  /* returned as its words */
            else
                rvalue(&o);
            end_expr();
        }
        expect(';', "expected ';' after return value, not");
        put(IR_RETURN, 0, NULL);
        return;
    case T_INT:
    case T_CHAR:
    case T_VOID:
    case T_CONST:
    case T_VOLATILE:
    case T_STRUCT:
        parse_declaration(0);
        return;
    case T_STATIC:
        next();
        if (!is_type_start(tok))
            error("expected a type after 'static', not", tok_desc());
        parse_declaration(1);
        return;
    case T_KEYWORD:
        not_yet();
        return;
    }
    parse_expr_statement();
    expect(';', "expected ';' after expression, not");
}

/* A { } block. new_scope is 0 for a function body, whose outermost
 * block shares the parameters' scope, as in C. */
static void parse_block(int new_scope)
{
    int saved_nlocals = nlocals, saved_next = frame_next;
    enter_nest();
    if (new_scope)
        depth++;
    next();                         /* '{' */
    while (tok != '}') {
        if (tok == T_EOF)
            error("missing '}' at end of file", NULL);
        parse_statement();
    }
    next();
    if (new_scope) {
        /* The block's variables and struct tags go out of scope, and
         * the variables' frame slots can be reused by later blocks. */
        depth--;
        nlocals = saved_nlocals;
        frame_next = saved_next;
        tag_hide(depth);
    }
    nest--;
}

/* ---------- Declarations ---------- */

/* Parse a parameter list starting at '('. Returns the parameter count,
 * or -1 for "()" (unspecified in a prototype, none in a definition).
 * Names go into locals[]; unnamed parameters get an empty name. */
static int parse_params(void)
{
    int n = 0;
    nlocals = 0;
    frame_next = frame_max = 0;
    depth = 1;
    next();                         /* '(' */
    if (tok == ')') {
        next();
        return -1;
    }
    for (;;) {
        char name[MAX_NAME + 1];
        int type;
        if (tok == T_ELLIPSIS)
            error("variable argument lists are not supported yet", NULL);
        if (tok == T_KEYWORD)
            not_yet();
        if (!is_type_start(tok))
            error("expected a parameter type, not", tok_desc());
        type = parse_pointers(parse_base_type());
        if (unqual(type) == TY_VOID) {
            if (type == TY_VOID && n == 0 && tok == ')') {
                next();
                return 0;           /* (void): no parameters */
            }
            error("a parameter can't have type void", NULL);
        }
        if (n >= MAX_PARAMS)
            fatal("too many parameters", NULL);
        name[0] = '\0';             /* a prototype may leave it out */
        if (tok == T_ID) {
            strcpy(name, tok_text);
            next();
        }
        /* An array parameter is a pointer to its first element, as in
         * C: int a[] and int a[10] are int *a, and int m[][4] is a
         * pointer to arrays of 4 ints. */
        type = parse_dims(type, 1);
        if (is_array(type))
            type = pointer_to(type_sub(type));
        check_byval(type, "passed");
        /* ENTER puts the parameters' words in consecutive frame words,
         * one each for a scalar (a char is then read as its low byte),
         * and a struct's words in its own layout. Parameters are the
         * first variables allocated, so add_local puts them there. */
        add_local(name, type, is_struct(type) ? 2 * struct_words(type) : 2);
        n++;
        if (tok != ',')
            break;
        next();
    }
    expect(')', "expected ')' after parameters, not");
    return n;
}

/* ---------- Global initializers ----------
 *
 * A global's initial value is computed at compile time and written to
 * the data stream as the global's bytes (approach.md, "M5 design"). An
 * integer goes through the expression parser with code output off, and
 * must fold to a constant. An address goes through parse_addr(), a
 * small parser of its own, which covers what real code uses: a string
 * literal, &name with any constant indexes, an array's name, casts,
 * and + or - a constant. Anything else (2 + buf, &*p) is an error. */

static const char not_const[] = "initializer is not a constant ccish supports";

/* An initializer's value: an integer, or an address (the start of the
 * global or string at target, in kind's space) plus val bytes. */
typedef struct {
    int kind;       /* 0 for an integer, else REL_DATA or REL_STR */
    int target;     /* data or string pool offset */
    int val;        /* the integer, or the bytes added to the address */
    int max;        /* REL_STR: the largest val that stays in the string */
    int type;
} Init;

/* Parse a constant expression in an initializer, at precedence prec
 * (or only a unary expression, if prec is above PREC_MUL), with code
 * output off. It must fold to a constant. */
static void parse_const(int prec, Operand *e)
{
    int saved = nheld;
    if (prec > PREC_MUL) {
        suppress++;
        parse_unary(e);
        suppress--;
        nheld = saved;
    } else {
        parse_skipped(prec, e);
    }
    if (e->kind != K_CONST)
        error(not_const, NULL);
    if (e->nonice)
        error(not_ice, "(an initializer here must be one)");
}

/* Parse an address constant for an initializer into *a, or an integer
 * constant if what follows doesn't start one. With unary set, parse
 * only a unary expression (a cast's operand); otherwise also any + or -
 * constants after it, scaled by the element size, as in C. */
static void parse_addr(Init *a, int unary)
{
    Operand e;
    Sym *s = tok == T_ID ? find_var(tok_text) : NULL;
    int t, op;

    enter_nest();
    a->kind = a->target = a->val = 0;
    if (tok == '(') {
        next();
        if (is_type_start(tok)) {
            /* A cast: of an address, to a pointer type, or of an
             * integer, to any type. */
            t = parse_pointers(parse_base_type());
            expect(')', "expected ')' after the type in a cast, not");
            parse_addr(a, 1);
            if (a->kind == 0) {
                e.kind = K_CONST;
                e.val = a->val;
                e.type = a->type;
                e.nonice = 0;
                cast(&e, t);
                if (e.kind != K_CONST)
                    error(not_const, NULL);
                a->val = e.val;
            } else if (!is_pointer(t)) {
                error(not_const, NULL);
            }
            a->type = unqual(t);
        } else {
            parse_addr(a, 0);
            expect(')', "expected ')', not");
        }
    } else if (tok == T_STR) {
        a->kind = REL_STR;
        a->target = data_string(tok_text, tok_len);
        a->max = tok_len + 1;
        a->type = pointer_to(TY_CHAR);
        next();
    } else if (tok == '&') {
        /* &name, then any constant indexes: &m[1][2]. */
        next();
        s = tok == T_ID ? find_var(tok_text) : NULL;
        if (!s || s->kind != S_GLOBAL)
            error(not_const, NULL);
        next();
        t = s->type;
        a->kind = REL_DATA;
        a->target = s->offset;
        while (tok == '[' && is_array(t)) {
            next();
            parse_const(PREC_COMMA, &e);
            if (is_pointer(e.type))
                error("array subscript is not an integer", NULL);
            expect(']', "expected ']' after an array index, not");
            t = type_sub(t);
            a->val += e.val * type_size(t);
        }
        a->type = pointer_to(t);
    } else if (s && s->kind == S_GLOBAL && is_array(s->type)) {
        /* An array's name: the address of its first element. */
        a->kind = REL_DATA;
        a->target = s->offset;
        a->type = value_type(s->type);
        next();
    } else {
        /* An integer, or a pointer from a cast of one. */
        parse_const(unary ? PREC_MUL + 1 : PREC_ASSIGN, &e);
        a->val = e.val;
        a->type = e.type;
    }
    while (!unary && (tok == '+' || tok == '-')) {
        op = tok;
        next();
        parse_const(PREC_MUL, &e);
        if (is_pointer(e.type))
            error(not_const, NULL);
        if (is_pointer(a->type))
            e.val *= elem_size(a->type);
        a->val = wrap16((unsigned)a->val
                        + (unsigned)(op == '+' ? e.val : -e.val));
    }
    nest--;
}

/* Parse the initializer of one scalar of type type (an int, char or
 * pointer), check it as an assignment, and write it to the data stream.
 * Sets init_known and init_val. */
static void parse_scalar_init(int type)
{
    Init a;
    Operand o;
    if (is_pointer(type)) {
        parse_addr(&a, 0);
    } else {
        parse_const(PREC_ASSIGN, &o);
        a.kind = 0;
        a.val = o.val;
        a.type = o.type;
    }
    o.kind = a.kind ? K_VALUE : K_CONST;
    o.val = a.val;
    o.type = a.type;
    o.nonice = 0;
    o.is_bool = 0;
    check_assign(type, &o, "initialization");
    init_known = a.kind == 0;
    init_val = a.val;
    /* An address must stay within the data written so far (one past
     * the end is allowed, as in C), or within its string. */
    if ((a.kind == REL_DATA && (a.target + a.val < 0
                                || a.target + a.val > data_offset()))
        || (a.kind == REL_STR && (a.val < 0 || a.val > a.max)))
        error("initializer address is out of range", NULL);
    if (type_size(type) == 1)
        data_byte(a.val);
    else if (a.kind)
        data_addr(a.kind, a.target + a.val);
    else
        data_word(a.val);
}

/*
 * An array's or struct's initializer is parsed the same way for a
 * global and a local; only where its elements go differs. A global's
 * are written to the data stream, in order. A local's are stores into
 * the frame (approach.md, "M5 design"), at frame offset init_base plus
 * each element's offset: CN 1 SLW t. Two neighboring char constants are
 * stored as one word (CW SLW, 5 bytes, not 8), and the zeros after the
 * last initializer are stored with a chain of DLWs, or by memset when
 * that is shorter.
 */
static int init_local;          /* initializing a local array */
static int init_base;           /* its frame offset */
static const char *init_name;   /* its name, for messages */
static int held_char = -1;      /* a char constant to be stored with the
                                 * next one, at frame offset held_at */
static int held_at;

/* Make sure the frame reaches offset end, for a local array whose size
 * comes from its initializer, so that it grows as elements are added
 * (and temporaries come after it). */
static void reserve(int end)
{
    if (end <= frame_next)
        return;
    if (end > 255)
        error("local array doesn't fit in the 255-byte frame"
              " (make it static or global):", quote(init_name));
    frame_next = end;
    if (frame_next > frame_max)
        frame_max = frame_next;
}

/* Store the held char constant, if any, by itself. */
static void flush_char(void)
{
    if (held_char < 0)
        return;
    put(IR_CONST, held_char, NULL);
    put(IR_SETLOCALB, held_at, NULL);
    put(IR_DROP, 0, NULL);
    held_char = -1;
}

/* Initialize the byte at offset off of the array to v: write it to the
 * data stream, or, for a local, store it (with the byte before it, if
 * that is a held constant). */
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
        put(IR_CONST, held_char | (v << 8), NULL);
        put(IR_SETLOCAL, held_at, NULL);
        put(IR_DROP, 0, NULL);
        held_char = -1;
        return;
    }
    flush_char();
    held_char = v;
    held_at = at;
}

/* The CMDSYS function memset, declared from the CMDSYS table if the
 * program hasn't used it yet; or NULL if the program has its own. */
static Sym *cmdsys_memset(void)
{
    Sym *s = find_func("memset");
    int c = find_cmdsys("memset");
    if (!s) {
        s = add_func("memset");
        declare_cmdsys(s, c);
    } else if (s->defined || !matches_cmdsys(s, c)) {
        return NULL;
    }
    return s;
}

/* Initialize n bytes at offset off of the array to 0: write them to the
 * data stream, or, for a local, store them. Up to 3 words take
 * CN 0 DLW ... SLW (1 + 2 bytes per word); more take
 * LLA CN 0 CB n CALL memset (7 or 8 bytes). */
static void init_zero(int n, int off)
{
    int at = init_base + off, words = n / 2, i;
    Sym *s;
    if (!init_local) {
        data_zero(n);
        return;
    }
    if (n <= 0)
        return;
    reserve(at + n);
    if (held_char == 0 && held_at == at - 1) {
        /* A held 0 just before (a string's NUL) joins the zeros. */
        held_char = -1;
        at--;
        n++;
        words = n / 2;
    }
    if (words + n % 2 > 3 && (s = cmdsys_memset()) != NULL) {
        put(IR_LOCALADDR, at, NULL);
        put(IR_CONST, 0, NULL);
        put(IR_CONST, n, NULL);
        s->called = 1;
        put(IR_CALL, 3, s);
        return;
    }
    put(IR_CONST, 0, NULL);
    for (i = 0; i < words; i++)
        put(IR_SETLOCAL, at + 2 * i, NULL);
    if (n % 2)
        put(IR_SETLOCALB, at + n - 1, NULL);
    put(IR_DROP, 0, NULL);
}

/* Parse the initializer of one scalar element of type type, at offset
 * off of the array. A global's must be constant (parse_scalar_init); a
 * local's may be any expression, as C99 allows. */
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
    check_assign(type, &e, "initialization");
    if (type == TY_CHAR && e.kind == K_CONST) {
        init_byte(off, e.val);
    } else {
        rvalue(&e);
        v.kind = K_VAR;
        v.type = type;
        v.off = at;
        v.global = 0;
        store_var(&v);
        put(IR_DROP, 0, NULL);
    }
    end_expr();
}

/* Initialize the array, from offset off, with the string literal (the
 * current token): a char array with room for max chars, or for any
 * number if max is 0. Its NUL is included if there is room, as in C.
 * Returns the number of bytes initialized. */
static int init_string(int max, int off)
{
    int i, n = tok_len;
    if (max && n > max)
        error("initializer string is too long for", quote(init_name));
    for (i = 0; i < n; i++)
        init_byte(off + i, tok_text[i]);
    if (!max || n < max) {
        init_byte(off + n, 0);
        n++;
    }
    next();
    return n;
}

/* Set by init_flat() when the initializer list's '}' is reached. */
static int init_done;
/* The end of what has been initialized so far: the offset after the
 * last scalar (or string). With no padding, the scalars of an
 * aggregate are contiguous, so everything before it is initialized. */
static int init_end;

/* After one scalar (or string) of a brace list: skip a ',', and note
 * whether the list ends here. */
static void after_leaf(void)
{
    if (tok == ',')
        next();
    else if (tok != '}')
        error("expected '}' or ',' in an initializer, not", tok_desc());
    init_done = tok == '}';
}

/* Initialize the object of type t at offset off (of the variable being
 * initialized) from a brace list, as far as the list goes. The list is
 * flat, as C allows: the scalars of arrays and structs, in order, at any
 * depth (struct pt ps[2] = {1, 2, 3, 4};). A char array in it may be
 * given a string literal instead ({"abc", 5}). An array of unknown size
 * (count 0, only at the top) takes as many elements as the list gives. */
static void init_flat(int t, int off)
{
    int n, i, m, tag;
    if (is_array(t) && unqual(type_sub(t)) == TY_CHAR && tok == T_STR) {
        n = type_count(t);
        i = init_string(n, off);
        init_zero(n - i, off + i);
        init_end = off + (n ? n : i);
        after_leaf();
    } else if (is_array(t)) {
        n = type_count(t);
        for (i = 0; (n == 0 || i < n) && !init_done; i++)
            init_flat(type_sub(t), off + i * type_size(type_sub(t)));
    } else if (is_struct(t)) {
        tag = struct_tag(t);
        for (m = member_next(tag, -1); m >= 0 && !init_done;
             m = member_next(tag, m))
            init_flat(member_type(m), off + member_off(m));
    } else {
        if (tok == '{')
            error("nested braces in initializers are not supported yet",
                  NULL);
        init_elem(unqual(t), off);
        init_end = off + type_size(t);
        after_leaf();
    }
}

/* Parse the initializer of an array or struct of type type, named name,
 * for a global or a local (init_local): a brace list (init_flat()), or,
 * for a char array, a string literal. Returns the type, with an array
 * size that was left out filled in from the initializer. Whatever the
 * list doesn't reach is zero. */
static int parse_aggr_init(int type, const char *name)
{
    int esize;

    init_name = name;
    init_end = 0;
    if (is_array(type) && unqual(type_sub(type)) == TY_CHAR
        && tok == T_STR) {
        init_end = init_string(type_count(type), 0);
    } else {
        expect('{', is_array(type)
                    ? "expected '{' to start an array initializer, not"
                    : "expected '{' to start a struct initializer, not");
        init_done = tok == '}';
        init_flat(type, 0);
        if (!init_done)
            error("too many initializers for", quote(name));
        next();                     /* '}' */
    }
    if (is_array(type) && type_count(type) == 0) {
        if (init_end == 0)
            error("an array of unknown size needs initializers:",
                  quote(name));
        esize = type_size(type_sub(type));
        type = array_of(type_sub(type), (init_end + esize - 1) / esize);
    }
    init_zero(type_size(type) - init_end, init_end);    /* the rest */
    return type;
}

/* Parse the initializer of a global of type type, named name, and
 * write its bytes to the data stream. Returns the type, with an array
 * size that was left out filled in. */
static int parse_global_init(int type, const char *name)
{
    if (is_array(type) || is_struct(type)) {
        init_local = 0;
        init_base = 0;
        return parse_aggr_init(type, name);
    }
    if (tok == '{') {                   /* int x = {5}; is valid C */
        next();
        parse_scalar_init(type);
        if (tok == ',')
            next();
        expect('}', "expected '}' after the initializer, not");
    } else {
        parse_scalar_init(type);
    }
    return type;
}

/* Parse the brace initializer of a local array or struct of type type,
 * named name, at frame offset base, as stores into the frame. Returns
 * the type, with an array size that was left out filled in. */
static int parse_local_init(int type, const char *name, int base)
{
    init_local = 1;
    init_base = base;
    held_char = -1;
    type = parse_aggr_init(type, name);
    flush_char();
    init_local = 0;
    reserve(base + type_size(type));
    return type;
}

/* Parse global variable declarations, after the base type base and
 * the first declarator's type and name: int x = 1, *p = &x, buf[100];
 * A global without an initializer is zero when the module loads. */
static void parse_globals(int base, int type, char *name)
{
    Sym *s;
    for (;;) {
        type = parse_var_type(type, 1);
        if (tok == '(')
            error("declare functions separately from variables", NULL);
        /* In C, the name is in scope in its own initializer. */
        s = add_global(name, type);
        if (tok == '=') {
            next();
            s->type = parse_global_init(type, name);
            if (init_known)
                set_known(s, init_val);
        } else {
            if (is_array(type) && type_count(type) == 0)
                error("array size missing", NULL);
            data_zero(type_size(type));
        }
        if (tok != ',')
            break;
        next();
        type = parse_declarator(base, name);
    }
    expect(';', "expected ';' after declaration, not");
}

/* Parse one top-level item: global variable declarations, a function
 * prototype, or a function definition. Any may start with "static",
 * which is accepted and, for now, changes nothing: globals and
 * functions are already private to the module, since ccish exports
 * nothing yet (approach.md, "M5 design"). */
static void parse_toplevel(void)
{
    int base, type, nparams, pwords, i;
    char name[MAX_NAME + 1];
    Sym *fn;

    depth = 0;                      /* file scope, for struct tags */
    if (tok == T_STATIC) {
        next();
        if (!is_type_start(tok))
            error("expected a type after 'static', not", tok_desc());
    }
    if (tok == T_KEYWORD)
        not_yet();
    if (!is_type_start(tok))
        error("expected a declaration or function definition, not",
              tok_desc());
    base = parse_base_type();
    if (tok == ';' && is_struct(base)) {
        next();                     /* struct pt { ... }; declares only */
        return;
    }
    type = parse_declarator(base, name);
    if (tok != '(') {
        parse_globals(base, type, name);
        return;
    }
    type = unqual(type);    /* const int f(void) returns an int */
    check_byval(type, "returned");
    if (find_global(name))
        error("redeclaration of", quote(name));
    fn = find_func(name);
    if (!fn)
        fn = add_func(name);

    cur_fn = fn;                    /* for messages about its variables */
    in_params = 1;
    nparams = parse_params();
    in_params = 0;
    pwords = frame_next / 2;
    if (tok == ';') {
        /* A prototype. "()" leaves the parameter count unspecified.
         * A call before any declaration assumed int, and its code
         * already depends on that. */
        if ((fn->declared || fn->called) && fn->type != type)
            error("conflicting declaration of", quote(fn->name));
        fn->type = type;
        if (nparams >= 0) {
            if (fn->nparams >= 0 && fn->nparams != nparams)
                error("conflicting parameter count for", quote(fn->name));
            fn->nparams = nparams;
            set_param_types(fn, nparams);
        }
        fn->declared = 1;
        next();
        cur_fn = NULL;
        tag_hide(0);                /* tags declared in the parameters */
        return;
    }
    if (nparams < 0)
        nparams = 0;
    if (tok != '{')
        error("expected '{' or ';' after function header, not", tok_desc());
    for (i = 0; i < nlocals; i++)
        if (!locals[i].name[0])
            error("parameter name missing in definition of",
                  quote(fn->name));
    if (fn->defined)
        error("redefinition of", quote(fn->name));
    if ((fn->declared || fn->called) && fn->type != type)
        error("conflicting declaration of", quote(fn->name));
    if (fn->nparams >= 0 && fn->nparams != nparams)
        error("parameter count differs from earlier use of",
              quote(fn->name));
    if (!strcmp(fn->name, "main") && nparams != 0)
        error("main with parameters is not supported yet", NULL);
    set_param_types(fn, nparams);
    fn->type = type;
    fn->nparams = nparams;
    fn->declared = 1;
    fn->defined = 1;

    nlabels = 0;
    put(IR_FUNC, pwords, fn);       /* ENTER takes the parameters' words */
    parse_block(0);
    /* Falling off the end of an int or char function returns 0. If the
     * last statement was a return, the peephole stage drops this as
     * dead code. */
    if (type != TY_VOID)
        push_zero_result(type);
    put(IR_RETURN, 0, NULL);
    put(IR_ENDFUNC, frame_max, fn);
    cur_fn = NULL;
    nlocals = 0;
    tag_hide(0);
}

/* Stage 1's entry: parse the whole source file, then check that every
 * function called but not defined is a CMDSYS function. */
void parse_program(void)
{
    int i, c;
    while (tok != T_EOF)
        parse_toplevel();

    /* Every function called but not defined here must come from
     * CMDSYS, with the signature CMDSYS has. */
    src_line = 0;
    for (i = 0; i < nsyms; i++) {
        Sym *s = &syms[i];
        if (s->defined || !s->called)
            continue;
        c = find_cmdsys(s->name);
        if (c < 0)
            error("call to undefined function", quote(s->name));
        if (!matches_cmdsys(s, c))
            error("declaration doesn't match the CMDSYS function",
                  quote(s->name));
    }
}
