/*
 * Stage 1: the parser. Recursive descent for declarations and
 * statements, precedence climbing for expressions (approach.md, issue
 * 5). It emits IR ops as it goes (ir_put) and builds no syntax tree.
 *
 * Language so far (milestones 1 to 3, and M4 step 1):
 *   - function definitions and prototypes returning int, char or void,
 *     with int and char parameters;
 *   - int and char local variables, declared anywhere in a block (or in
 *     a for statement's first clause), with optional initializers, and
 *     block scope;
 *   - casts to int, char and void;
 *   - statements: return, if/else, while, do, for, break, continue,
 *     expression statements, empty statements and nested blocks;
 *   - expressions: integer and character constants, string literals,
 *     variables, function calls, parentheses, the pstr() builtin,
 *     unary - + ~ ! ++ --, postfix ++ --, binary * / % + - << >> & ^ |,
 *     comparisons, && || ?:, and = += -= *= /= %= &= |= ^= <<= >>=.
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
 * needed (approach.md 4(a), option A).
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
 * with their parameter counts and return types (void or int). Their
 * names and meanings are PLASMA's, not C's: puts() takes a PLASMA
 * string and adds no newline. divmod() returns two values and is left
 * out.
 */
static const struct { const char *name; int nparams, type; }
cmdsys_funcs[] = {
    {"putc", 1, TY_VOID}, {"putln", 0, TY_VOID}, {"puts", 1, TY_VOID},
    {"puti", 1, TY_VOID}, {"putb", 1, TY_VOID}, {"puth", 1, TY_VOID},
    {"getc", 0, TY_INT}, {"gets", 1, TY_INT}, {"call", 5, TY_INT},
    {"syscall", 2, TY_INT}, {"heapmark", 0, TY_INT},
    {"heapallocalign", 3, TY_INT}, {"heapalloc", 1, TY_INT},
    {"heaprelease", 1, TY_INT}, {"heapavail", 0, TY_INT},
    {"memset", 3, TY_VOID}, {"memcpy", 3, TY_VOID}, {"strcpy", 2, TY_INT},
    {"strcat", 2, TY_INT}, {"toupper", 1, TY_INT}, {"sext", 1, TY_INT},
    {"isugt", 2, TY_INT}, {"isuge", 2, TY_INT}, {"isult", 2, TY_INT},
    {"isule", 2, TY_INT},
    {NULL, 0, 0}
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

/* Parameter types of declared functions, n at a time; Sym.ptypes says
 * where a function's start. */
static int param_types[MAX_PARAM_TYPES];
static int nparam_types;

/* Record that fn's n parameters have the types of locals[0..n-1] (the
 * parameters just parsed), or check them against the types an earlier
 * declaration gave. The caller has already checked the count. */
static void set_param_types(Sym *fn, int n)
{
    int i;
    if (fn->ptypes >= 0) {
        for (i = 0; i < n; i++)
            if (param_types[fn->ptypes + i] != locals[i].type)
                error("conflicting parameter types for", quote(fn->name));
        return;
    }
    if (nparam_types + n > MAX_PARAM_TYPES)
        fatal("too many function parameters in all", NULL);
    fn->ptypes = nparam_types;
    for (i = 0; i < n; i++)
        param_types[nparam_types++] = locals[i].type;
}

/* Is t a token that starts a type name? */
static int is_type_start(int t)
{
    return t == T_INT || t == T_CHAR || t == T_VOID;
}

/* Parse a basic type name (int, char or void) and return its type. */
static int parse_base_type(void)
{
    int t = TY_INT;
    if (tok == T_CHAR)
        t = TY_CHAR;
    else if (tok == T_VOID)
        t = TY_VOID;
    else if (tok != T_INT)
        error("expected a type, not", tok_desc());
    next();
    return t;
}

/* ---------- Constants ---------- */

/* Left operands held back for folding; see the comment at the top. */
static int held[MAX_NEST + 1];
static int nheld;

/* Above 0 while parsing code that can never run, such as the right side
 * of 0 && x: it is checked, but put() emits nothing. */
static int suppress;

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

/* An operand on its way through the expression parser. */
enum {
    K_VALUE,        /* its value is on the stack */
    K_VOID,         /* a void call: nothing on the stack */
    K_LOCAL,        /* a local variable, not loaded yet (off = offset) */
    K_CONST         /* a constant, not emitted yet (val = value) */
};

typedef struct {
    int kind;
    int type;       /* its C type. A char value on the stack is always
                     * 0 to 255, so it needs no conversion to char. */
    int off;
    int val;
    int is_bool;    /* K_VALUE: known to be 1 or 0 (a comparison, !, &&) */
} Operand;

/* C precedence levels (higher binds tighter). */
enum {
    PREC_COMMA = 1, PREC_ASSIGN, PREC_COND, PREC_OROR, PREC_ANDAND,
    PREC_OR, PREC_XOR, PREC_AND, PREC_EQUALITY, PREC_RELATIONAL,
    PREC_SHIFT, PREC_ADD, PREC_MUL
};

static void parse_expr(int min_prec, Operand *o);
static void parse_unary(Operand *o);

/* Push the value of the local variable o (a char is one byte). */
static void load_local(Operand *o)
{
    put(o->type == TY_CHAR ? IR_LOCALB : IR_LOCAL, o->off, NULL);
}

/* Store the top value into the local variable o, keeping the stored
 * value (for a char, its low byte) on the stack. */
static void store_local(Operand *o)
{
    put(o->type == TY_CHAR ? IR_SETLOCALB : IR_SETLOCAL, o->off, NULL);
}

/* Make sure the operand's value is on the stack. */
static void rvalue(Operand *o)
{
    if (o->kind == K_VOID)
        error("void value used in an expression", NULL);
    if (o->kind == K_LOCAL) {
        load_local(o);
        o->is_bool = 0;
    } else if (o->kind == K_CONST) {
        put(IR_CONST, o->val, NULL);
        o->is_bool = o->val == 0 || o->val == 1;
    }
    o->kind = K_VALUE;
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

/* Apply the cast (type) to o. A cast's result is never an lvalue. To
 * void, the value is discarded. */
static void cast(Operand *o, int type)
{
    if (type == TY_VOID) {
        if (o->kind == K_VALUE)
            put(IR_DROP, 0, NULL);
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

/* Parse an expression that must produce a value, at precedence
 * min_prec or above. */
static void parse_value(int min_prec)
{
    Operand o;
    parse_expr(min_prec, &o);
    rvalue(&o);
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

/* Report an error unless o is a variable that can be assigned to; what
 * says how it was used. */
static void need_variable(Operand *o, const char *what)
{
    if (o->kind != K_LOCAL)
        error("lvalue required as", what);
}

/* Parse a call to fn, starting at '(': emit the arguments, check their
 * count, then emit the call. */
static void parse_call(Sym *fn, Operand *o)
{
    int nargs = 0;
    next();                         /* '(' */
    if (tok != ')') {
        for (;;) {
            parse_value(PREC_ASSIGN);
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
    put(IR_CALL, nargs, fn);
    o->kind = fn->type != TY_VOID ? K_VALUE : K_VOID;
    o->type = fn->type;
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
    off = data_string(tok_text, tok_len);
    next();
    expect(')', "expected ')' after pstr argument, not");
    put(IR_DATAADDR, off - 1, NULL);
}

/* Parse a primary expression: a constant, string, variable, call, or
 * expression in parentheses. Constants and variables stay pending. A
 * cast is parsed here too, since it also starts with '('. */
static void parse_primary(Operand *o)
{
    char name[MAX_NAME + 1];
    Sym *s;
    int c;

    o->kind = K_VALUE;
    o->type = TY_INT;
    o->is_bool = 0;
    switch (tok) {
    case T_NUM:
        o->kind = K_CONST;          /* emit it later, if needed */
        o->val = wrap16((unsigned)tok_val);
        next();
        return;
    case T_STR:
        put(IR_DATAADDR, data_string(tok_text, tok_len), NULL);
        next();
        return;
    case '(':
        enter_nest();
        next();
        if (is_type_start(tok)) {
            /* A cast: (type) unary-expression. */
            c = parse_base_type();
            if (tok == '*')
                error("pointers are not supported yet", NULL);
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
                return;
            }
            if (find_local(name))
                error("called object is not a function:", quote(name));
            s = find_func(name);
            if (!s) {
                s = add_func(name);
                c = find_cmdsys(name);
                if (c >= 0) {
                    /* Declared by the CMDSYS table. */
                    s->nparams = cmdsys_funcs[c].nparams;
                    s->type = cmdsys_funcs[c].type;
                    s->declared = 1;
                }
                /* Otherwise an implicit declaration: returns int, and
                 * this call fixes the parameter count. */
            }
            parse_call(s, o);
            return;
        }
        s = find_local(name);
        if (s) {
            o->kind = K_LOCAL;      /* load it later, if needed */
            o->type = s->type;
            o->off = s->offset;
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

/* ++x / --x: x = x + 1, and the value is the new x. */
static void pre_incdec(Operand *o, int ir_op)
{
    need_variable(o, "operand of ++ or --");
    load_local(o);
    put(IR_CONST, 1, NULL);
    put(ir_op, 0, NULL);
    store_local(o);
    o->kind = K_VALUE;
    o->is_bool = 0;
}

/* x++ / x--: the value is the old x. Keep a copy of it below the
 * update; when the value is unused, the peephole stage removes the
 * copy. */
static void post_incdec(Operand *o, int ir_op)
{
    need_variable(o, "operand of ++ or --");
    load_local(o);
    put(IR_DUP, 0, NULL);
    put(IR_CONST, 1, NULL);
    put(ir_op, 0, NULL);
    store_local(o);
    put(IR_DROP, 0, NULL);
    o->kind = K_VALUE;
    o->is_bool = 0;
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
        parse_unary(o);
        pre_incdec(o, t == T_INC ? IR_ADD : IR_SUB);
        return;
    case '*': case '&':
        not_yet();
    }
    parse_primary(o);
    for (;;) {
        switch (tok) {
        case T_INC:
        case T_DEC:
            t = tok;
            next();
            post_incdec(o, t == T_INC ? IR_ADD : IR_SUB);
            continue;
        case '[': case '.': case T_ARROW: case '(':
            not_yet();
        }
        return;
    }
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
            return;
        }
        /* 1 && x and 0 || x are x, as 1 or 0. */
        parse_expr(prec + 1, &r);
        to_bool(&r);
        *o = r;
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

/* The type of b ? b : c: their common type, or int if they differ
 * (char and int; C promotes both to int). */
static int cond_type(Operand *b, Operand *c)
{
    return b->type == c->type ? b->type : TY_INT;
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
    int else_l, end_l;

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
        *o = take_b ? b : c;
        if (o->kind == K_LOCAL)
            rvalue(o);              /* the result is not an lvalue */
        o->type = cond_type(&b, &c);
        return;
    }
    rvalue(o);
    else_l = new_label();
    end_l = new_label();
    branch(IR_BRFALSE, else_l);
    parse_expr(PREC_COMMA, &b);
    if (!is_void(&b))
        rvalue(&b);
    branch(IR_JUMP, end_l);
    expect(':', "expected ':' in conditional expression, not");
    place_forward(else_l);
    parse_expr(PREC_COND, &c);
    if (!is_void(&c))
        rvalue(&c);
    if (is_void(&b) != is_void(&c))
        error("only one side of '?:' is void", NULL);
    place_forward(end_l);
    o->kind = b.kind;
    o->type = cond_type(&b, &c);
    o->is_bool = b.is_bool && c.is_bool;
}

/*
 * Precedence climbing: parse an expression whose operators all have
 * precedence min_prec or higher. The result is left in *o, possibly
 * still pending (a bare variable).
 */
static void parse_expr(int min_prec, Operand *o)
{
    int ir_op, supported, prec;
    Operand r;

    enter_nest();
    parse_unary(o);

    if (assign_op(&ir_op)) {
        /* Assignment is right-associative and binds loosest of all but
         * ','; it is only possible directly after a unary expression. */
        if (min_prec > PREC_ASSIGN)
            error("lvalue required as", "left operand of assignment");
        need_variable(o, "left operand of assignment");
        next();
        if (ir_op >= 0)
            load_local(o);
        parse_expr(PREC_ASSIGN, &r);
        if (ir_op >= 0 && r.kind == K_CONST && !suppress)
            check_right_const(ir_op, r.val);
        rvalue(&r);
        if (ir_op >= 0)
            put(ir_op, 0, NULL);
        store_local(o);
        o->kind = K_VALUE;
        o->is_bool = ir_op < 0 && r.is_bool;
    }

    for (;;) {
        int mark = -1;
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
        if (o->kind == K_CONST) {
            /* Hold it back: it may fold with the right operand. */
            if (nheld > MAX_NEST)
                fatal("nesting too deep", NULL);
            mark = nheld;
            held[nheld++] = o->val;
        } else {
            rvalue(o);
        }
        next();
        parse_expr(prec + 1, &r);   /* all binary ops are left-assoc */
        if (r.kind == K_CONST && !suppress)
            check_right_const(ir_op, r.val);
        if (mark >= 0 && nheld > mark) {
            /* The right operand emitted nothing, so the left constant
             * is still held. */
            if (r.kind == K_CONST) {
                nheld--;
                o->val = fold(ir_op, o->val, r.val);
                o->type = TY_INT;
                continue;
            }
            if (r.kind == K_LOCAL && swapped_op(ir_op) >= 0) {
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
        o->kind = K_VALUE;
        o->type = TY_INT;
        rvalue(&r);
        put(ir_op, 0, NULL);
        o->is_bool = is_compare(ir_op);
    }
    nest--;
}

/* ---------- Statements ---------- */

static void parse_block(int new_scope);

/* int a, b = 1, c;  or  char c = 'x'; */
static void parse_declaration(void)
{
    int type = parse_base_type();
    if (type == TY_VOID)
        error("a variable can't have type void", NULL);
    for (;;) {
        Operand v;
        Sym *s;
        if (tok == '*')
            error("pointers are not supported yet", NULL);
        if (tok != T_ID)
            error("expected a variable name, not", tok_desc());
        /* In C, the name is in scope in its own initializer. */
        s = add_local(tok_text, type, type_size(type));
        next();
        if (tok == '[')
            error("arrays are not supported yet", NULL);
        if (tok == '(')
            error("local function declarations are not supported", NULL);
        if (tok == '=') {
            next();
            parse_value(PREC_ASSIGN);
            v.kind = K_LOCAL;
            v.type = type;
            v.off = s->offset;
            store_local(&v);
            put(IR_DROP, 0, NULL);
        }
        if (tok != ',')
            break;
        next();
    }
    expect(';', "expected ';' after declaration, not");
}

/* Discard the value of an expression used as a statement. A bare
 * variable or constant ("x;") emitted nothing, so needs nothing. */
static void discard(Operand *o)
{
    if (o->kind == K_VALUE)
        put(IR_DROP, 0, NULL);
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

/* Parse "( condition )" and branch to label n as cond_branch does. */
static void parse_paren_cond(int n, int on_true)
{
    Operand o;
    expect('(', "expected '(' before condition, not");
    parse_expr(PREC_COMMA, &o);
    expect(')', "expected ')' after condition, not");
    cond_branch(&o, n, on_true);
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
    Operand o;

    next();                         /* "for" */
    expect('(', "expected '(' after 'for', not");
    depth++;
    if (is_type_start(tok)) {
        parse_declaration();        /* includes the ';' */
    } else {
        if (tok != ';') {
            parse_expr(PREC_COMMA, &o);
            discard(&o);
        }
        expect(';', "expected ';' after 'for' initialization, not");
    }
    top = new_label();
    brk = new_label();
    place_label(top);
    if (tok != ';') {
        parse_expr(PREC_COMMA, &o);
        cond_branch(&o, brk, 0);
    }
    expect(';', "expected ';' after 'for' condition, not");
    cont = top;
    if (tok != ')') {
        body = new_label();
        cont = new_label();
        branch(IR_JUMP, body);
        place_label(cont);
        parse_expr(PREC_COMMA, &o);
        discard(&o);
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
                put(IR_CONST, 0, NULL);  /* C89 allows this */
        } else {
            if (cur_fn->type == TY_VOID)
                error("void function returns a value:", quote(cur_fn->name));
            parse_expr(PREC_COMMA, &o);
            if (cur_fn->type == TY_CHAR)
                to_char(&o);        /* a char function returns 0 to 255 */
            rvalue(&o);
        }
        expect(';', "expected ';' after return value, not");
        put(IR_RETURN, 0, NULL);
        return;
    case T_INT:
    case T_CHAR:
    case T_VOID:
        parse_declaration();
        return;
    case T_KEYWORD:
        not_yet();
        return;
    }
    parse_expr(PREC_COMMA, &o);
    discard(&o);
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
        /* The block's variables go out of scope, and their frame slots
         * can be reused by later blocks. */
        depth--;
        nlocals = saved_nlocals;
        frame_next = saved_next;
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
    if (tok == T_VOID) {
        next();
        if (tok == ')') {
            next();
            return 0;
        }
        error("a parameter can't have type void", NULL);
    }
    for (;;) {
        int type;
        if (tok == T_ELLIPSIS)
            error("variable argument lists are not supported yet", NULL);
        if (tok == T_KEYWORD)
            not_yet();
        if (!is_type_start(tok))
            error("expected a parameter type, not", tok_desc());
        type = parse_base_type();
        if (type == TY_VOID)
            error("a parameter can't have type void", NULL);
        if (tok == '*' || tok == '[')
            error("pointer parameters are not supported yet", NULL);
        if (n >= MAX_PARAMS)
            fatal("too many parameters", NULL);
        /* ENTER puts parameter n at offset 2n, as a word even for a
         * char (which is then read as its low byte). Parameters are the
         * first variables allocated, so add_local puts them there. */
        if (tok == T_ID) {
            add_local(tok_text, type, 2);
            next();
        } else {
            add_local("", type, 2);
        }
        n++;
        if (tok != ',')
            break;
        next();
    }
    expect(')', "expected ')' after parameters, not");
    return n;
}

/* Parse one top-level item: a function prototype or a function
 * definition. */
static void parse_toplevel(void)
{
    int type, nparams, i;
    Sym *fn;

    if (tok == T_KEYWORD)
        not_yet();
    if (!is_type_start(tok))
        error("expected a function definition, not", tok_desc());
    type = parse_base_type();
    if (tok == '*')
        error("pointer return types are not supported yet", NULL);
    if (tok != T_ID)
        error("expected a function name, not", tok_desc());
    fn = find_func(tok_text);
    if (!fn)
        fn = add_func(tok_text);
    next();
    if (tok != '(')
        error("global variables are not supported yet", NULL);

    cur_fn = fn;                    /* for messages about its variables */
    nparams = parse_params();
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
    put(IR_FUNC, nparams, fn);
    parse_block(0);
    /* Falling off the end of an int or char function returns 0. If the
     * last statement was a return, the peephole stage drops this as
     * dead code. */
    if (type != TY_VOID)
        put(IR_CONST, 0, NULL);
    put(IR_RETURN, 0, NULL);
    put(IR_ENDFUNC, frame_max, fn);
    cur_fn = NULL;
    nlocals = 0;
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
        if (s->nparams != cmdsys_funcs[c].nparams
            || s->type != cmdsys_funcs[c].type)
            error("declaration doesn't match the CMDSYS function",
                  quote(s->name));
    }
}
