/*
 * Stage 1: the parser. Recursive descent for declarations and
 * statements, precedence climbing for expressions (approach.md, issue
 * 5). It emits IR ops as it goes (ir_put) and builds no syntax tree.
 *
 * Language so far (milestones 1 and 2):
 *   - function definitions and prototypes returning int or void, with
 *     int parameters;
 *   - int local variables, declared anywhere in a block, with optional
 *     initializers, and block scope;
 *   - statements: return, expression statements, empty statements and
 *     nested blocks;
 *   - expressions: integer and character constants, string literals,
 *     variables, function calls, parentheses, the pstr() builtin,
 *     unary - + ~ ! ++ --, postfix ++ --, binary * / % + - << >> & ^ |,
 *     and = += -= *= /= %= &= |= ^= <<= >>=.
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

static const char *quote(const char *name)
{
    static char buf[MAX_NAME + 3];
    strcpy(buf, "'");
    strcat(buf, name);
    strcat(buf, "'");
    return buf;
}

static void not_yet(void)
{
    error("not supported yet:", tok_desc());
}

static void expect(int t, const char *what)
{
    if (tok != t)
        error(what, tok_desc());
    next();
}

static void enter_nest(void)
{
    if (++nest > MAX_NEST)
        fatal("nesting too deep", NULL);
}

static Sym *find_func(const char *name)
{
    int i;
    for (i = 0; i < nsyms; i++)
        if (!strcmp(syms[i].name, name))
            return &syms[i];
    return NULL;
}

static Sym *add_func(const char *name)
{
    Sym *s;
    if (nsyms >= MAX_SYMS)
        fatal("too many functions", NULL);
    s = &syms[nsyms++];
    memset(s, 0, sizeof(*s));
    strcpy(s->name, name);
    s->kind = S_FUNC;
    s->nparams = -1;
    s->returns_value = 1;   /* C89 implicit declarations return int */
    s->import_idx = -1;
    return s;
}

/*
 * The CMDSYS functions a program may call, from src/inc/cmdsys.plh,
 * with their parameter counts and whether they return a value. Their
 * names and meanings are PLASMA's, not C's: puts() takes a PLASMA
 * string and adds no newline. divmod() returns two values and is left
 * out.
 */
static const struct { const char *name; int nparams, returns_value; }
cmdsys_funcs[] = {
    {"putc", 1, 0}, {"putln", 0, 0}, {"puts", 1, 0}, {"puti", 1, 0},
    {"putb", 1, 0}, {"puth", 1, 0}, {"getc", 0, 1}, {"gets", 1, 1},
    {"call", 5, 1}, {"syscall", 2, 1}, {"heapmark", 0, 1},
    {"heapallocalign", 3, 1}, {"heapalloc", 1, 1}, {"heaprelease", 1, 1},
    {"heapavail", 0, 1}, {"memset", 3, 0}, {"memcpy", 3, 0},
    {"strcpy", 2, 1}, {"strcat", 2, 1}, {"toupper", 1, 1}, {"sext", 1, 1},
    {"isugt", 2, 1}, {"isuge", 2, 1}, {"isult", 2, 1}, {"isule", 2, 1},
    {NULL, 0, 0}
};

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

/* Declare a local (or parameter) at the current depth and give it a
 * frame slot. An unnamed prototype parameter has the name "". */
static Sym *add_local(const char *name)
{
    Sym *s = find_local(name);
    if (name[0] && s && s->depth == depth)
        error("redeclaration of", quote(name));
    if (nlocals >= MAX_LOCALS)
        fatal("too many variables in scope", NULL);
    if (frame_next + 2 > 255)
        error("too many local variables in", quote(cur_fn->name));
    s = &locals[nlocals++];
    memset(s, 0, sizeof(*s));
    strcpy(s->name, name);
    s->kind = S_LOCAL;
    s->depth = depth;
    s->offset = frame_next;
    frame_next += 2;
    if (frame_next > frame_max)
        frame_max = frame_next;
    return s;
}

/* ---------- Expressions ---------- */

/* An operand on its way through the expression parser. */
enum {
    K_VALUE,        /* its value is on the stack */
    K_VOID,         /* a void call: nothing on the stack */
    K_LOCAL         /* a local variable, not loaded yet (off = offset) */
};

typedef struct {
    int kind;
    int off;
} Operand;

/* C precedence levels (higher binds tighter). */
enum {
    PREC_COMMA = 1, PREC_ASSIGN, PREC_COND, PREC_OROR, PREC_ANDAND,
    PREC_OR, PREC_XOR, PREC_AND, PREC_EQUALITY, PREC_RELATIONAL,
    PREC_SHIFT, PREC_ADD, PREC_MUL
};

static void parse_expr(int min_prec, Operand *o);
static void parse_unary(Operand *o);

/* Make sure the operand's value is on the stack. */
static void rvalue(Operand *o)
{
    if (o->kind == K_VOID)
        error("void value used in an expression", NULL);
    if (o->kind == K_LOCAL)
        ir_put(IR_LOCAL, o->off, NULL);
    o->kind = K_VALUE;
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
    }
    *supported = 0;
    switch (tok) {
    case '<': case '>': case T_LE: case T_GE: return PREC_RELATIONAL;
    case T_EQ: case T_NE:                     return PREC_EQUALITY;
    case T_ANDAND:                            return PREC_ANDAND;
    case T_OROR:                              return PREC_OROR;
    case '?':                                 return PREC_COND;
    case ',':                                 return PREC_COMMA;
    }
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

static void need_variable(Operand *o, const char *what)
{
    if (o->kind != K_LOCAL)
        error("lvalue required as", what);
}

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
    ir_put(IR_CALL, nargs, fn);
    o->kind = fn->returns_value ? K_VALUE : K_VOID;
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
    ir_put(IR_DATAADDR, off - 1, NULL);
}

static void parse_primary(Operand *o)
{
    char name[MAX_NAME + 1];
    Sym *s;
    int c;

    o->kind = K_VALUE;
    switch (tok) {
    case T_NUM:
        ir_put(IR_CONST, tok_val, NULL);
        next();
        return;
    case T_STR:
        ir_put(IR_DATAADDR, data_string(tok_text, tok_len), NULL);
        next();
        return;
    case '(':
        /* The result may still be a pending variable: (x) = 1 is valid. */
        enter_nest();
        next();
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
                    s->returns_value = cmdsys_funcs[c].returns_value;
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
    ir_put(IR_LOCAL, o->off, NULL);
    ir_put(IR_CONST, 1, NULL);
    ir_put(ir_op, 0, NULL);
    ir_put(IR_SETLOCAL, o->off, NULL);
    o->kind = K_VALUE;
}

/* x++ / x--: the value is the old x. Keep a copy of it below the
 * update; when the value is unused, the peephole stage removes the
 * copy. */
static void post_incdec(Operand *o, int ir_op)
{
    need_variable(o, "operand of ++ or --");
    ir_put(IR_LOCAL, o->off, NULL);
    ir_put(IR_DUP, 0, NULL);
    ir_put(IR_CONST, 1, NULL);
    ir_put(ir_op, 0, NULL);
    ir_put(IR_SETLOCAL, o->off, NULL);
    ir_put(IR_DROP, 0, NULL);
    o->kind = K_VALUE;
}

static void parse_unary(Operand *o)
{
    int t = tok;
    switch (t) {
    case '-': case '+': case '~': case '!':
        next();
        parse_unary(o);
        rvalue(o);
        if (t == '-')
            ir_put(IR_NEG, 0, NULL);
        else if (t == '~')
            ir_put(IR_COMP, 0, NULL);
        else if (t == '!')
            ir_put(IR_NOT, 0, NULL);
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
 * Precedence climbing: parse an expression whose operators all have
 * precedence min_prec or higher. The result is left in *o, possibly
 * still pending (a bare variable).
 */
static void parse_expr(int min_prec, Operand *o)
{
    int ir_op, supported, prec;

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
            ir_put(IR_LOCAL, o->off, NULL);
        parse_value(PREC_ASSIGN);
        if (ir_op >= 0)
            ir_put(ir_op, 0, NULL);
        ir_put(IR_SETLOCAL, o->off, NULL);
        o->kind = K_VALUE;
    }

    for (;;) {
        Operand r;
        if (assign_op(&ir_op))
            error("lvalue required as", "left operand of assignment");
        prec = binary_prec(&ir_op, &supported);
        if (prec == 0 || prec < min_prec)
            break;
        if (!supported)
            not_yet();
        rvalue(o);
        next();
        parse_expr(prec + 1, &r);   /* all binary ops are left-assoc */
        rvalue(&r);
        ir_put(ir_op, 0, NULL);
    }
    nest--;
}

/* ---------- Statements ---------- */

static void parse_block(int new_scope);

/* int a, b = 1, c; */
static void parse_declaration(void)
{
    next();                         /* "int" */
    for (;;) {
        Sym *s;
        if (tok == '*')
            error("pointers are not supported yet", NULL);
        if (tok != T_ID)
            error("expected a variable name, not", tok_desc());
        /* In C, the name is in scope in its own initializer. */
        s = add_local(tok_text);
        next();
        if (tok == '[')
            error("arrays are not supported yet", NULL);
        if (tok == '(')
            error("local function declarations are not supported", NULL);
        if (tok == '=') {
            next();
            parse_value(PREC_ASSIGN);
            ir_put(IR_SETLOCAL, s->offset, NULL);
            ir_put(IR_DROP, 0, NULL);
        }
        if (tok != ',')
            break;
        next();
    }
    expect(';', "expected ';' after declaration, not");
}

static void parse_statement(void)
{
    Operand o;
    switch (tok) {
    case '{':
        parse_block(1);
        return;
    case ';':
        next();
        return;
    case T_RETURN:
        next();
        if (tok == ';') {
            if (cur_fn->returns_value)
                ir_put(IR_CONST, 0, NULL);  /* C89 allows this */
        } else {
            if (!cur_fn->returns_value)
                error("void function returns a value:", quote(cur_fn->name));
            parse_value(PREC_COMMA);
        }
        expect(';', "expected ';' after return value, not");
        ir_put(IR_RETURN, 0, NULL);
        return;
    case T_INT:
        parse_declaration();
        return;
    case T_VOID:
        error("a variable can't have type void", NULL);
        return;
    case T_KEYWORD:
        not_yet();
        return;
    }
    parse_expr(PREC_COMMA, &o);
    /* A bare variable ("x;") has no effect, so nothing was emitted. */
    if (o.kind == K_VALUE)
        ir_put(IR_DROP, 0, NULL);
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
        if (tok == T_ELLIPSIS)
            error("variable argument lists are not supported yet", NULL);
        if (tok == T_KEYWORD)
            not_yet();
        expect(T_INT, "expected a parameter type, not");
        if (tok == '*' || tok == '[')
            error("pointer parameters are not supported yet", NULL);
        if (n >= MAX_PARAMS)
            fatal("too many parameters", NULL);
        /* ENTER puts parameter n at offset 2n. Parameters are the
         * first variables allocated, so add_local puts them there. */
        if (tok == T_ID) {
            add_local(tok_text);
            next();
        } else {
            add_local("");
        }
        n++;
        if (tok != ',')
            break;
        next();
    }
    expect(')', "expected ')' after parameters, not");
    return n;
}

static void parse_toplevel(void)
{
    int returns_value, nparams, i;
    Sym *fn;

    if (tok == T_INT)
        returns_value = 1;
    else if (tok == T_VOID)
        returns_value = 0;
    else if (tok == T_KEYWORD) {
        not_yet();
        return;
    } else {
        error("expected a function definition, not", tok_desc());
        return;
    }
    next();
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
        if ((fn->declared || fn->called)
            && fn->returns_value != returns_value)
            error("conflicting declaration of", quote(fn->name));
        fn->returns_value = returns_value;
        if (nparams >= 0) {
            if (fn->nparams >= 0 && fn->nparams != nparams)
                error("conflicting parameter count for", quote(fn->name));
            fn->nparams = nparams;
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
    if ((fn->declared || fn->called) && fn->returns_value != returns_value)
        error("conflicting declaration of", quote(fn->name));
    if (fn->nparams >= 0 && fn->nparams != nparams)
        error("parameter count differs from earlier use of",
              quote(fn->name));
    if (!strcmp(fn->name, "main") && nparams != 0)
        error("main with parameters is not supported yet", NULL);
    fn->returns_value = returns_value;
    fn->nparams = nparams;
    fn->declared = 1;
    fn->defined = 1;

    ir_put(IR_FUNC, nparams, fn);
    parse_block(0);
    /* Falling off the end of an int function returns 0. If the last
     * statement was a return, the peephole stage drops this as dead
     * code. */
    if (returns_value)
        ir_put(IR_CONST, 0, NULL);
    ir_put(IR_RETURN, 0, NULL);
    ir_put(IR_ENDFUNC, frame_max, fn);
    cur_fn = NULL;
    nlocals = 0;
}

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
            || s->returns_value != cmdsys_funcs[c].returns_value)
            error("declaration doesn't match the CMDSYS function",
                  quote(s->name));
    }
}
